#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""误唤醒率流式仿真: 用 val+test 的 unknown/background 拼成长音频流,
int8 tflite 推理 (1s 窗, hop 0.25s), 统计各增益 (0/+6/+12dB) 下:
  - 超阈值窗口数、误报事件数 (连续超阈值窗口记 1 次事件)
  - 连续 k 窗口平滑 (k=2,3) 后的事件数, 折算 次/小时
并测 target 各增益 (0/-6/-12dB) 的逐段唤醒率。
用法: python3 eval_far.py [模型目录]   # 目录需含 model.tflite + feature_config.json
只用 val+test 数据 (两个模型都没直接训练过), 保证新旧模型可比。
"""
import sys
import os
import json
import numpy as np
import tensorflow as tf

import features as F

HOP = 4000           # 250ms 滑窗步进 (MCU 端通常更密, 此处偏保守)
NEG_GAINS = (0.0, 6.0, 12.0)
TGT_GAINS = (0.0, -6.0, -12.0)
SEED = 7


def load_split():
    d = np.load("data.npz", allow_pickle=True)
    paths = list(d["val_paths"]) + list(d["test_paths"])
    labels = list(d["val_labels"]) + list(d["test_labels"])
    neg = [p for p, l in zip(paths, labels) if l != 0]
    tgt = [p for p, l in zip(paths, labels) if l == 0]
    return neg, tgt


def build_stream(paths, rng):
    """随机顺序拼接片段 -> 长音频流 (float32)。"""
    order = rng.permutation(len(paths))
    return np.concatenate([F.read_wav(paths[i])[1] for i in order]).astype(np.float32)


class Runner:
    """int8 tflite 推理, 与 MCU 部署路径一致。"""

    def __init__(self, tflite_path):
        self.interp = tf.lite.Interpreter(model_path=tflite_path)
        self.interp.allocate_tensors()
        ind = self.interp.get_input_details()[0]
        outd = self.interp.get_output_details()[0]
        self.ii, self.oi = ind["index"], outd["index"]
        self.in_scale, self.in_zp = ind["quantization"]
        self.out_scale, self.out_zp = outd["quantization"]

    def prob(self, x):
        """1s 窗口样本 -> target 概率。"""
        if len(x) < F.N_SAMPLES:
            x = np.pad(x, (0, F.N_SAMPLES - len(x)))
        m = F.samples_to_mel(x)[..., None].astype(np.float32)
        q = np.clip(np.round(m / self.in_scale) + self.in_zp,
                    -128, 127).astype(np.int8)
        self.interp.set_tensor(self.ii, q[None])
        self.interp.invoke()
        out = self.interp.get_tensor(self.oi)[0]
        return float(self.out_scale * (out.astype(np.float32) - self.out_zp)[0])

    def stream_probs(self, stream):
        """滑窗推理, 返回各窗口 target 概率。"""
        n_win = 1 + max(0, len(stream) - F.N_SAMPLES) // HOP
        return np.array([self.prob(stream[w * HOP:w * HOP + F.N_SAMPLES])
                         for w in range(n_win)], np.float32)


def run_lengths(flags):
    """bool 数组 -> 各连续 True 段长度。"""
    lens, cur = [], 0
    for f in flags:
        if f:
            cur += 1
        elif cur:
            lens.append(cur)
            cur = 0
    if cur:
        lens.append(cur)
    return lens


def eval_stream(runner, paths, gains, thr, rng):
    stream = build_stream(paths, rng)
    hours = len(stream) / F.SR / 3600.0
    print(f"  流时长 {len(stream)/F.SR/60:.1f} 分钟, 窗口 hop {HOP/F.SR*1000:.0f}ms, "
          f"阈值 {thr:.3f}")
    for db in gains:
        s = np.clip(stream * 10.0 ** (db / 20.0), -1.0, 1.0).astype(np.float32)
        p = runner.stream_probs(s)
        lens = run_lengths(p > thr)
        ev = len(lens)
        ev2 = sum(1 for L in lens if L >= 2)
        ev3 = sum(1 for L in lens if L >= 3)
        print(f"  +{db:>4.0f}dB: 超阈窗口 {int((p>thr).sum()):4d}/{len(p)}  "
              f"p_max={p.max():.3f}  事件/小时: 原始 {ev/hours:6.1f} | "
              f"连续2窗 {ev2/hours:6.1f} | 连续3窗 {ev3/hours:6.1f}")


def eval_target(runner, paths, gains, thr):
    for db in gains:
        hits = 0
        for p in paths:
            x = F.to_1s_window(F.read_wav(p)[1])
            if db:
                x = np.clip(x * 10.0 ** (db / 20.0), -1.0, 1.0).astype(np.float32)
            hits += runner.prob(x) > thr
        print(f"  target {db:+3.0f}dB: 唤醒率 {hits}/{len(paths)} = {hits/len(paths):.1%}")


def sweep(runner, neg, tgt, thr_default):
    """阈值扫描: 各候选阈值下的误报/小时 (3 增益合并) 与各增益唤醒率。"""
    stream = build_stream(neg, np.random.RandomState(SEED))
    probs = [runner.stream_probs(np.clip(stream * 10.0 ** (db / 20.0),
                                         -1.0, 1.0).astype(np.float32))
             for db in NEG_GAINS]
    hours = len(NEG_GAINS) * len(stream) / F.SR / 3600.0
    tgt_p = {}
    for db in TGT_GAINS:
        ps = []
        for p in tgt:
            x = F.to_1s_window(F.read_wav(p)[1])
            if db:
                x = np.clip(x * 10.0 ** (db / 20.0), -1.0, 1.0).astype(np.float32)
            ps.append(runner.prob(x))
        tgt_p[db] = np.array(ps)

    print("阈值权衡 (误报=负样本流 3 增益合并; 唤醒率=target 逐段):")
    print(f"{'阈值':>6} | {'误报/h 原始':>10} | {'连续2窗':>8} | {'连续3窗':>8} | "
          + " | ".join(f"唤醒{db:+.0f}dB" for db in TGT_GAINS))
    cands = sorted({round(v, 3) for v in list(np.arange(0.80, 0.99, 0.03)) + [thr_default]})
    for thr in cands:
        ev = [0, 0, 0]  # 原始 / >=2 / >=3 连续
        for p in probs:
            lens = run_lengths(p > thr)
            ev[0] += len(lens)
            ev[1] += sum(1 for L in lens if L >= 2)
            ev[2] += sum(1 for L in lens if L >= 3)
        wakes = " | ".join(f"{(tgt_p[db] > thr).mean():9.1%}" for db in TGT_GAINS)
        mark = " <-- 部署" if abs(thr - thr_default) < 0.002 else ""
        print(f"{thr:6.3f} | {ev[0]/hours:10.1f} | {ev[1]/hours:8.1f} | {ev[2]/hours:8.1f} | "
              f"{wakes}{mark}")


def main():
    mdir = sys.argv[1] if len(sys.argv) > 1 else "."
    do_sweep = "--sweep" in sys.argv
    cfg = json.load(open(os.path.join(mdir, "feature_config.json")))
    thr = cfg["target_threshold"]
    runner = Runner(os.path.join(mdir, "model.tflite"))
    neg, tgt = load_split()

    print(f"== 模型: {mdir}  (阈值 {thr:.4f}) ==")
    print(f"误唤醒流 (unknown+background {len(neg)} 段):")
    eval_stream(runner, neg, NEG_GAINS, thr, np.random.RandomState(SEED))
    print(f"唤醒率 (target {len(tgt)} 段, 居中 1s 窗):")
    eval_target(runner, tgt, TGT_GAINS, thr)
    if do_sweep:
        print()
        sweep(runner, neg, tgt, thr)


if __name__ == "__main__":
    main()
