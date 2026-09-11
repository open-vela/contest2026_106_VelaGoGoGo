#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""VAD 切 raw PCM (16k/16bit/1ch) 成 wav 语音段, 供负样本采集。

用法: python3 slice_pcm.py <in.pcm> <out_dir> [--min-db -40] [--gap 0.35]
                             [--min-len 0.4] [--chunk 4.0]

静音判据: 50ms 帧 RMS < min-db; 连续静音 >= gap 秒断段; 段长 >= min-len。
超过 chunk 秒的长段均分成多段 (mid-word 切割对负样本无害), 便于按唯一段
划分 train/val/test。
"""
import argparse
import os
import wave

import numpy as np

SR = 16000
W = 800  # 50ms 分析帧


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pcm")
    ap.add_argument("out_dir")
    ap.add_argument("--min-db", type=float, default=-40.0)
    ap.add_argument("--gap", type=float, default=0.35)
    ap.add_argument("--min-len", type=float, default=0.4)
    ap.add_argument("--chunk", type=float, default=4.0)
    a = ap.parse_args()

    x = np.fromfile(a.pcm, dtype="<i2")
    os.makedirs(a.out_dir, exist_ok=True)

    nb = len(x) // W
    r = np.array([20 * np.log10(np.sqrt(np.mean(
        (x[i * W:(i + 1) * W].astype(np.float32) / 32768.0) ** 2)) + 1e-12)
        for i in range(nb)])
    on = r > a.min_db
    bursts, s, gap = [], None, 0
    need = int(a.gap * SR / W)
    for i, v in enumerate(on):
        if v:
            if s is None:
                s = i
            gap = 0
        elif s is not None:
            gap += 1
            if gap >= need:
                bursts.append((s * W, i * W - gap * W))
                s = None
                gap = 0
    if s is not None:
        bursts.append((s * W, nb * W))
    bursts = [(p, q) for p, q in bursts if (q - p) >= int(a.min_len * SR)]

    n = 0
    for p, q in bursts:
        segs = [(p, q)]
        if a.chunk and (q - p) > a.chunk * SR:
            k = int(np.ceil((q - p) / (a.chunk * SR)))
            step = (q - p) // k
            segs = [(p + i * step, p + (i + 1) * step if i < k - 1 else q)
                    for i in range(k)]
        for j, (p2, q2) in enumerate(segs):
            out = os.path.join(a.out_dir, f"seg_{n:03d}_{p2 / SR:.1f}s.wav")
            with wave.open(out, "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(SR)
                w.writeframes(x[p2:q2].tobytes())
            n += 1
    print(f"{a.pcm}: {len(bursts)} 个语音段 -> {n} 个 wav (chunk {a.chunk}s) -> {a.out_dir}")


if __name__ == "__main__":
    main()
