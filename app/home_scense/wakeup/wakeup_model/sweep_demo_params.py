#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""在 demo 精确参数 (100ms hop, 浮点模型, 1s 窗) 下扫描 阈值 × 连续窗数。

流:
  mixed   : val+test 全部负样本拼接 — 常规误报流
  按目录  : neg_zh (1.pcm 同嗓音) / neg_jul (7月29 同嗓音其他短语, 最难)
            / neg_board (板端假语音) — 各自专项流
  target  : val+test target 唯一段拼接 — 逐段唤醒率 (唯一段划分后即新念法泛化)

demo 判决: 连续 k 窗 (hop 100ms) 超阈值才唤醒。
"""
import os
import sys

import numpy as np
from tensorflow import keras

from eval_far import build_stream, load_split, run_lengths
import features as F

HOP = 1600          # demo: 100ms
MIX_GAINS = (0.0, 6.0, 12.0)
TGT_GAINS = (0.0, -6.0, -12.0)
THR_GRID = (0.80, 0.82, 0.84, 0.86, 0.88, 0.90, 0.92, 0.94, 0.95, 0.96, 0.97, 0.98)
K_GRID = (2, 3, 4, 5, 6)
DIRS = ("neg_zh", "neg_jul", "neg_board")


def stream_probs(model, stream):
    n_win = 1 + (len(stream) - F.N_SAMPLES) // HOP
    X = np.empty((n_win, F.N_FRAMES, F.N_MELS, 1), np.float32)
    for w in range(n_win):
        X[w] = F.samples_to_mel(
            stream[w * HOP:w * HOP + F.N_SAMPLES])[..., None]
    return model.predict(X, verbose=0)[:, 0]


def apply_gain(stream, db):
    if not db:
        return stream
    return np.clip(stream * 10.0 ** (db / 20.0), -1.0, 1.0).astype(np.float32)


def n_events(probs, thr, k):
    return sum(1 for L in run_lengths(probs > thr) if L >= k)


def main():
    model = keras.models.load_model("model.keras")
    neg, tgt = load_split()
    by_dir = {d: [p for p in neg if os.path.basename(os.path.dirname(p)) == d]
              for d in DIRS}
    print(f"neg {len(neg)}: " + " ".join(f"{d}={len(v)}" for d, v in by_dir.items())
          + f", tgt {len(tgt)}")

    mixed = build_stream(neg, np.random.RandomState(7))
    dir_streams = {d: build_stream(v, np.random.RandomState(7))
                   for d, v in by_dir.items() if v}
    print(f"mixed 流 {len(mixed)/F.SR/60:.1f}min, "
          + " ".join(f"{d} {len(s)/F.SR:.0f}s" for d, s in dir_streams.items()))

    mixed_p = [stream_probs(model, apply_gain(mixed, db)) for db in MIX_GAINS]
    dir_p = {d: [stream_probs(model, apply_gain(s, db)) for db in MIX_GAINS]
             for d, s in dir_streams.items()}
    hours = len(mixed) / F.SR / 3600.0

    order = np.random.RandomState(7).permutation(len(tgt))
    bounds, pos = [], 0
    for i in order:
        n = len(F.read_wav(tgt[i])[1])
        bounds.append((pos, pos + n))
        pos += n
    tstream = build_stream(tgt, np.random.RandomState(7))
    tgt_p = [stream_probs(model, apply_gain(tstream, db)) for db in TGT_GAINS]

    hdr = " ".join(f"{d.split('_')[1]:>6s}" for d in dir_streams)
    print(f"\n{'阈值':>5} {'k':>2} | {'mixed/h':>7} | {hdr} | "
          f"{'唤醒 0dB':>8} {'-6dB':>7} {'-12dB':>7}")
    for thr in THR_GRID:
        for k in K_GRID:
            fa_mix = sum(n_events(p, thr, k) for p in mixed_p) / (len(MIX_GAINS) * hours)
            fas = [sum(n_events(p, thr, k) for p in dir_p[d]) for d in dir_streams]
            wakes = []
            for p in tgt_p:
                hits = sum(any(L >= k for L in
                               run_lengths(p[a // HOP:min(len(p), (b + HOP - 1) // HOP + 1)] > thr))
                           for a, b in bounds)
                wakes.append(hits / len(bounds))
            print(f"{thr:5.2f} {k:2d} | {fa_mix:7.1f} | "
                  + " ".join(f"{v:6d}" for v in fas) + " | "
                  + f"{wakes[0]:7.1%} {wakes[1]:6.1%} {wakes[2]:6.1%}")


if __name__ == "__main__":
    sys.exit(main())
