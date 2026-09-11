#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""诊断: rec.pcm 各语音段得分/电平/增益响应; 1.pcm 133s 尖峰段归属与得分;
target 训练段得分作参照。"""
import glob
import os
import wave

import numpy as np
from tensorflow import keras

import features as F

model = keras.models.load_model("model.keras")


def read_wav(p):
    with wave.open(p) as w:
        return np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float32) / 32768.0


def score(x, db=0.0):
    x = F.to_1s_window(x)
    if db:
        x = np.clip(x * 10.0 ** (db / 20.0), -1.0, 1.0).astype(np.float32)
    m = F.samples_to_mel(x)[..., None]
    return float(model.predict(m[None], verbose=0)[0, 0])


def rms_db(x):
    return 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)


print("== rec.pcm 各段 (真唤醒词: 29.9s / 33.5s 两个) ==")
for p in sorted(glob.glob("/tmp/rec_segs/*.wav")):
    x = read_wav(p)
    probs = [score(x, db) for db in (0, 6, 12, 20)]
    print(f"{os.path.basename(p):22s} len={len(x)/F.SR:4.1f}s rms={rms_db(x):6.1f}dB "
          f"prob: 0dB={probs[0]:.3f} +6={probs[1]:.3f} +12={probs[2]:.3f} +20={probs[3]:.3f}")

print("\n== 1.pcm 133s 尖峰段 ==")
d = np.load("data.npz", allow_pickle=True)
all_splits = {"train": list(d["train_paths"]), "val": list(d["val_paths"]),
              "test": list(d["test_paths"])}
for p in sorted(glob.glob("dataset/neg_zh/*.wav")):
    t = float(os.path.basename(p).split("_")[2].rstrip("s.wav"))
    x = read_wav(p)
    if t <= 133.0 <= t + len(x) / F.SR or abs(t - 133.0) < 4.5:
        where = [k for k, v in all_splits.items() if p in v]
        dup = len([q for q in all_splits["train"] if q == p])
        print(f"{os.path.basename(p):26s} 起 {t:6.1f}s len={len(x)/F.SR:4.1f}s "
              f"split={where} 训练重复{dup}次 prob(0dB)={score(x):.3f}")

print("\n== 参照: val target 段得分分布 (0dB) ==")
tgt_val = [p for p, l in zip(d["val_paths"], d["val_labels"]) if l == 0]
ps = np.array([score(read_wav(p)) for p in tgt_val])
print(f"n={len(ps)} min={ps.min():.3f} p10={np.percentile(ps,10):.3f} "
      f"p50={np.percentile(ps,50):.3f} max={ps.max():.3f}")
