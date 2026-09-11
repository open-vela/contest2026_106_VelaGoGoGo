#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""FA-first (阈值, 连续窗数) 选择器 (demo 精确参数: 1s 窗, hop 1600, 浮点模型)。

硬门 (全部满足才算候选, 任一失败即淘汰):
  A. 基准录音:
     - rec.pcm 恰好 2 次且位置落在两句真唤醒 (29.9s / 33.5s 附近)
       (防"2 次但醒的是假语音段": 6 段假语音在 0~29s)
     - 1.pcm / 7月29 / 7月28噪声 恰好 0 次
  B. 负样本流按目录 (neg_zh/neg_jul/neg_board), 0/+6/+12dB 全部 0 事件
     (裸拼接含跨段窗, 保守估计)

候选内按 target 唤醒率排序: 0dB > -6dB > -12dB (val+test 唯一段, demo 精确计数)。
"""
import os
import sys
import wave

import numpy as np
from tensorflow import keras

import features as F
from eval_far import build_stream, load_split, run_lengths

HOP = 1600
MIX_GAINS = (0.0, 6.0, 12.0)
TGT_GAINS = (0.0, -6.0, -12.0)
THR_GRID = tuple(round(t, 2) for t in np.arange(0.78, 0.99, 0.02))
K_GRID = (2, 3, 4, 5, 6, 7, 8)
DIRS = ("neg_zh", "neg_jul", "neg_board")

# rec.pcm 两句真唤醒 (VAD 段起点 29.9s / 33.5s), 事件起点容忍区间
REAL_WAKE_SPANS = ((28.0, 32.0), (32.0, 36.5))

BENCH = [
    ("recordings/rec.pcm", 2, "板端: 6假+2真唤醒词"),
    ("recordings/1.pcm", 0, "同嗓音全负样本 247s"),
    ("recordings/rec_16k_16bit_1ch.pcm", 0, "7月29 同嗓音其他短语"),
    ("recordings/rec.wav", 0, "7月28 立体声纯噪声"),
]


def read_any(path):
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


def stream_probs(model, stream):
    n = 1 + max(0, (len(stream) - F.N_SAMPLES) // HOP)
    X = np.empty((n, F.N_FRAMES, F.N_MELS, 1), np.float32)
    for w in range(n):
        X[w] = F.samples_to_mel(stream[w * HOP:w * HOP + F.N_SAMPLES])[..., None]
    return model.predict(X, verbose=0)[:, 0]


def gain(x, db):
    if not db:
        return x
    return np.clip(x * 10.0 ** (db / 20.0), -1.0, 1.0).astype(np.float32)


def n_events(p, thr, k):
    return len(event_starts(p, thr, k))


def event_starts(p, thr, k, min_dist=12):
    """事件起点秒列表, demo 语义: 连续 k 窗触发一次, 冷却 1.2s (WAKE_MIN_DIST=12)。

    一个长 run 只计一次 (首次达到 k); 相邻事件至少间隔 min_dist 窗。
    """
    starts, i, n = [], 0, len(p)
    while i < n:
        if p[i] > thr:
            j = i
            while j < n and p[j] > thr:
                j += 1
            if j - i >= k:
                starts.append(i)
            i = j
        else:
            i += 1
    out, last = [], -10 ** 9
    for s_idx in starts:
        if s_idx - last >= min_dist:
            out.append(s_idx * HOP / 16000)
            last = s_idx
    return out


def main():
    model_path = sys.argv[1] if len(sys.argv) > 1 else "model.keras"
    model = keras.models.load_model(model_path)
    print(f"模型: {model_path}")
    neg, tgt = load_split()
    by_dir = {d: [p for p in neg if os.path.basename(os.path.dirname(p)) == d]
              for d in DIRS}
    print(f"neg {len(neg)} (zh {len(by_dir['neg_zh'])} / jul {len(by_dir['neg_jul'])} / "
          f"board {len(by_dir['neg_board'])}), tgt 唯一段 {len(tgt)}")

    # ---- 预计算所有概率 ----
    bench_p = {}
    for path, _, _ in BENCH:
        bench_p[path] = stream_probs(model, read_any(path))
    dir_p = {d: [stream_probs(model, gain(build_stream(v, np.random.RandomState(7)), db))
                 for db in MIX_GAINS] for d, v in by_dir.items() if v}

    # target 流: val+test 唯一段顺序拼接, 记录每段边界 (窗号)
    bounds, pos = [], 0
    for p in tgt:
        n = len(F.read_wav(p)[1])
        bounds.append((pos, pos + n))
        pos += n
    tstream = build_stream(tgt, np.random.RandomState(7))
    tgt_p = [stream_probs(model, gain(tstream, db)) for db in TGT_GAINS]

    def wake_rate(p, thr, k):
        hits = sum(any(L >= k for L in
                       run_lengths(p[a // HOP:min(len(p), (b + HOP - 1) // HOP + 1)] > thr))
                   for a, b in bounds)
        return hits / len(bounds)

    # ---- 扫描 ----
    rows = []
    for thr in THR_GRID:
        for k in K_GRID:
            # 硬门 A: 基准录音
            ok, why = True, []
            rec_ev = event_starts(bench_p[BENCH[0][0]], thr, k)
            if len(rec_ev) != 2:
                ok = False
                why.append(f"rec={len(rec_ev)}")
            else:
                for t, (lo, hi) in zip(sorted(rec_ev), REAL_WAKE_SPANS):
                    if not (lo <= t <= hi):
                        ok = False
                        why.append(f"rec位置{t:.1f}s")
            for path, want, _ in BENCH[1:]:
                ev = n_events(bench_p[path], thr, k)
                if ev != want:
                    ok = False
                    why.append(f"{os.path.basename(path)}={ev}")
            # 硬门 B: 负样本流
            for d, ps in dir_p.items():
                fa = sum(n_events(p, thr, k) for p in ps)
                if fa:
                    ok = False
                    why.append(f"{d}流={fa}")
            wakes = [wake_rate(p, thr, k) for p in tgt_p]
            rows.append((ok, thr, k, wakes, why))

    cands = [r for r in rows if r[0]]
    print(f"\n{len(rows)} 组参数, 硬门全过: {len(cands)} 组")
    if not cands:
        print("无候选。最接近的 8 组 (按唤醒率排, 失败原因列出):")
        near = sorted(rows, key=lambda r: -(r[3][0] + r[3][1]))
        for ok, thr, k, wakes, why in near[:8]:
            print(f"  thr={thr:.2f} k={k}  唤醒 0dB={wakes[0]:.0%} -6dB={wakes[1]:.0%} "
                  f"| ✗ {'; '.join(why)}")
        return 1

    print(f"\n{'thr':>5} {'k':>2} | {'唤醒 0dB':>8} {'-6dB':>6} {'-12dB':>6} | 安全边际")
    for ok, thr, k, wakes, why in sorted(cands, key=lambda r: -(r[3][0] * 10 + r[3][1])):
        # 边际: 真唤醒两段的峰值最小值 - thr; 负基准最大峰 - thr
        rec_pk = min(bench_p[BENCH[0][0]].max(),
                     bench_p[BENCH[0][0]][290:340].max(), bench_p[BENCH[0][0]][330:360].max())
        neg_pk = max(bench_p[p].max() for p, _, _ in BENCH[1:])
        print(f"{thr:5.2f} {k:2d} | {wakes[0]:8.0%} {wakes[1]:6.0%} {wakes[2]:6.0%} | "
              f"真唤醒峰-min vs thr: {rec_pk - thr:+.3f}, 负峰-thr: {neg_pk - thr:+.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
