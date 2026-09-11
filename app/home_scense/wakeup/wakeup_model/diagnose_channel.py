#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""信道/嗓音指纹对比: 平均 log-mel 谱 + 基频。
组: target(val) / neg_zh(1.pcm 切片) / rec.pcm 各段。"""
import glob
import os
import wave

import numpy as np

import features as F

SR = 16000


def read_wav(p):
    with wave.open(p) as w:
        return np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float32) / 32768.0


def mean_mel(x):
    """全长 mel 均值 (40,)。"""
    m = F.samples_to_mel(F.to_1s_window(x))
    return m.mean(axis=0)


def f0_hz(x):
    """简单自相关基频 (80-400Hz)。"""
    x = x - x.mean()
    if len(x) < 1600:
        x = np.pad(x, (0, 1600 - len(x)))
    best, best_r = 0, 0
    for lag in range(SR // 400, SR // 80):
        r = np.mean(x[:-lag] * x[lag:])
        if r > best_r:
            best_r, best = r, lag
    return SR / best if best else 0


def report(name, xs):
    M = np.array([mean_mel(x) for x in xs])
    f0s = [f0_hz(x) for x in xs]
    m = M.mean(axis=0)
    # mel 频带大致中心频率 (线性 mel 映射的近似): 用 features 的 mel 滤波器
    hf = m[30:].mean()   # 高频段 (约 >4kHz)
    lf = m[:10].mean()   # 低频段 (约 <600Hz)
    print(f"{name:28s} n={len(xs):3d}  F0中位={np.median(f0s):6.0f}Hz  "
          f"mel低频={lf:5.2f} 高频={hf:5.2f}  高/低差={hf-lf:+5.2f}")
    return m


d = np.load("data.npz", allow_pickle=True)
tgt = [read_wav(p) for p, l in zip(d["val_paths"], d["val_labels"]) if l == 0][:30]
zh = [read_wav(p) for p in sorted(glob.glob("dataset/neg_zh/*.wav"))][:30]
rec_all = sorted(glob.glob("/tmp/rec_segs/*.wav"))
rec_real = [read_wav(p) for p in rec_all if p.endswith(("29.9s.wav", "33.5s.wav"))]
rec_false = [read_wav(p) for p in rec_all if not p.endswith(("29.9s.wav", "33.5s.wav"))]

m_t = report("target 训练数据(val)", tgt)
m_z = report("neg_zh (1.pcm 切片)", zh)
m_rr = report("rec.pcm 真唤醒词", rec_real)
m_rf = report("rec.pcm 假语音", rec_false)

print("\n各 mel 频带均值差 (组A - 组B), 正=该频带 A 更强:")
for na, ma in (("target", m_t), ("neg_zh", m_z), ("rec真", m_rr)):
    for nb, mb in (("target", m_t), ("neg_zh", m_z), ("rec真", m_rr)):
        if na >= nb:
            continue
        diff = ma - mb
        print(f"  {na:7s} vs {nb:7s}: " + " ".join(f"{v:+.1f}" for v in diff[::4]))
