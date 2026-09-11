#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""部署前预检 (demo 精确参数: 1s 窗, hop 1600, 浮点模型):
  1. 负样本流 (val+test, 按目录) 最大 prob -> 离候选阈值的边际
  2. 基准录音回归 (demo 精确仿真):
     rec.pcm  39s  板端, 8 段假语音 + 2 段真唤醒词(29.9s/33.5s) -> 期望恰好 2 次
     1.pcm   247s  同嗓音全负样本                          -> 期望 0 次
     7月29   124s  同嗓音其他短语 (旧模型曾打 0.75-0.93)    -> 期望 0 次
     7月28   116s  立体声转单声道, 其他内容                 -> 期望 0 次
"""
import os
import wave

import numpy as np
from tensorflow import keras

import features as F
from eval_far import build_stream, load_split, run_lengths

HOP = 1600
GAINS = (0.0, 6.0, 12.0)
DIRS = ("neg_zh", "neg_jul", "neg_board")


def stream_probs(model, stream):
    n_win = 1 + max(0, (len(stream) - F.N_SAMPLES) // HOP)
    X = np.empty((n_win, F.N_FRAMES, F.N_MELS, 1), np.float32)
    for w in range(n_win):
        X[w] = F.samples_to_mel(
            stream[w * HOP:w * HOP + F.N_SAMPLES])[..., None]
    return model.predict(X, verbose=0)[:, 0]


def apply_gain(x, db):
    if not db:
        return x
    return np.clip(x * 10.0 ** (db / 20.0), -1.0, 1.0).astype(np.float32)


def read_any(path):
    """raw pcm (16k/mono/16bit) 或 wav (任意采样率/声道) -> 16k mono float32。"""
    if path.endswith(".pcm"):
        return np.fromfile(path, "<i2").astype(np.float32) / 32768.0
    with wave.open(path) as w:
        sr, ch = w.getframerate(), w.getnchannels()
        raw = np.frombuffer(w.readframes(w.getnframes()), "<i2")
        x = raw.reshape(-1, ch).mean(axis=1).astype(np.float32) / 32768.0
    if sr != 16000:
        n = int(len(x) * 16000 / sr)
        x = np.interp(np.arange(n) * sr / 16000, np.arange(len(x)), x).astype(np.float32)
    return x


def events(p, thr, k):
    """返回 (事件起点窗号列表, 起点对应秒)。"""
    out, cur = [], 0
    for i, v in enumerate(p):
        if v > thr:
            cur += 1
            if cur == k:
                out.append(i - k + 1)
        else:
            cur = 0
    return out, [i * HOP / 16000 for i in out]


def main():
    model = keras.models.load_model("model.keras")
    neg, _ = load_split()

    print("== 负样本流最大 prob (3 增益) ==")
    for d in DIRS:
        ps = [p for p in neg if os.path.basename(os.path.dirname(p)) == d]
        if not ps:
            continue
        s = build_stream(ps, np.random.RandomState(7))
        mx = max(stream_probs(model, apply_gain(s, db)).max() for db in GAINS)
        print(f"  {d:10s} {len(ps):3d} 段  max={mx:.3f}")
    mx = max(stream_probs(model, apply_gain(
        build_stream(neg, np.random.RandomState(7)), db)).max() for db in GAINS)
    print(f"  {'mixed':10s} {len(neg):3d} 段  max={mx:.3f}")

    bench = [
        ("recordings/rec.pcm", 2, "板端: 6假+2真唤醒词"),
        ("recordings/1.pcm", 0, "同嗓音全负样本 247s"),
        ("recordings/rec_16k_16bit_1ch.pcm", 0, "7月29 同嗓音其他短语"),
        ("recordings/rec.wav", 0, "7月28 立体声纯噪声(不入训练)"),
    ]
    probs = {}
    print("\n== 基准录音 (demo 精确仿真) ==")
    for path, want, desc in bench:
        x = read_any(path)
        p = stream_probs(model, x)
        probs[path] = p
        print(f"  {os.path.basename(path):26s} {len(x)/16000:5.0f}s  "
              f"max={p.max():.3f} @ {int(np.argmax(p))*HOP/16000:.1f}s  "
              f"期望{want}次 ({desc})")

    print(f"\n{'thr':>5} {'k':>2} | " + " | ".join(
        f"{os.path.basename(p):>16s}" for p, _, _ in bench))
    for thr in (0.88, 0.90, 0.92, 0.94, 0.96):
        for k in (3, 4, 5):
            row = []
            for path, want, _ in bench:
                ev, ts = events(probs[path], thr, k)
                ok = "✓" if len(ev) == want else "✗"
                row.append(f"{len(ev):2d}{ok} {','.join(f'{t:.0f}' for t in ts):>10s}"
                           [:16])
            print(f"{thr:5.2f} {k:2d} | " + " | ".join(f"{c:>16s}" for c in row))


if __name__ == "__main__":
    main()
