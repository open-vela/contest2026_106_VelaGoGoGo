#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""硬负样本挖掘: 用当前模型扫完整负样本录音的所有 1s 对齐 (hop 100ms),
把高分窗 (模型误认为唤醒词) 存为 dataset/neg_hard/ 的 1s wav, 加入训练。

规则:
- 只挖 1.pcm 与 7月29 录音 (rec.pcm 基准保持纯净, 不挖)。
- 排除与 val/test 负样本段时间区间重叠的窗 (保持留出集诚实)。
- 1.pcm 的 28.0-36.5s 无需排除 (那是 rec.pcm 的真唤醒区间, 与 1.pcm 无关)。
"""
import glob
import os
import re
import sys
import wave

import numpy as np
from tensorflow import keras

import features as F

HOP = 1600
PROB_MIN = 0.55
MODEL = "model_s3407.keras"
OUT_DIR = "dataset/neg_hard"

RECORDINGS = [
    ("recordings/1.pcm", "neg_zh"),
    ("recordings/rec_16k_16bit_1ch.pcm", "neg_jul"),
]

# val/test 段的源位置区间 (按目录, 文件名 seg_XXX_<起点>s.wav)
def holdout_spans():
    d = np.load("data.npz", allow_pickle=True)
    spans = {}
    for split in ("val", "test"):
        for p in d[f"{split}_paths"]:
            p = str(p)
            for key in ("neg_zh", "neg_jul"):
                if key in p:
                    m = re.search(r"_(\d+(?:\.\d+)?)s\.wav$", os.path.basename(p))
                    if m:
                        start = float(m.group(1))
                        _, x = F.read_wav(p)
                        spans.setdefault(key, []).append((start, start + len(x) / 16000))
    return spans


def read_pcm(path):
    return np.fromfile(path, "<i2").astype(np.float32) / 32768.0


def save_wav(path, x):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())


def main():
    model = keras.models.load_model(MODEL)
    spans = holdout_spans()
    for key in spans:
        print(f"留出区间 [{key}]: {len(spans[key])} 段")
    os.makedirs(OUT_DIR, exist_ok=True)
    for old in glob.glob(os.path.join(OUT_DIR, "*.wav")):
        os.remove(old)

    total = kept = skipped = 0
    for path, key in RECORDINGS:
        x = read_pcm(path)
        n_win = 1 + (len(x) - F.N_SAMPLES) // HOP
        X = np.empty((n_win, F.N_FRAMES, F.N_MELS, 1), np.float32)
        for w in range(n_win):
            X[w] = F.samples_to_mel(x[w * HOP:w * HOP + F.N_SAMPLES])[..., None]
        probs = model.predict(X, verbose=0)[:, 0]
        base = os.path.basename(path).replace(".pcm", "")
        for w in range(n_win):
            total += 1
            if probs[w] < PROB_MIN:
                continue
            t0, t1 = w * HOP / 16000, (w * HOP + F.N_SAMPLES) / 16000
            # 排除与留出段重叠的窗
            if any(t0 < e and t1 > s for s, e in spans.get(key, [])):
                skipped += 1
                continue
            save_wav(os.path.join(OUT_DIR, f"hard_{base}_{t0:.1f}s.wav"),
                     x[w * HOP:w * HOP + F.N_SAMPLES])
            kept += 1
        hi = probs[probs >= PROB_MIN]
        print(f"{path}: {n_win} 窗, >= {PROB_MIN}: {len(hi)} 个 "
              f"(max={probs.max():.3f}), 收录 {kept} (排除留出 {skipped})")
        total = kept = skipped = 0  # per-recording 计数

    n = len(glob.glob(os.path.join(OUT_DIR, "*.wav")))
    print(f"\n{OUT_DIR}: 共 {n} 个硬负样本窗")


if __name__ == "__main__":
    sys.exit(main())
