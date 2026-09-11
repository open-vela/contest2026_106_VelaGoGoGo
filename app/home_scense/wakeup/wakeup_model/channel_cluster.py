#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""信道聚类: 各来源语音段的平均 log-mel 谱两两距离, 判断信道归属。
A=rec_segments(31, 训练源)  B=rec_16k_16bit(34, 高分新样本)
C=rec_16k_mono(30, 低分)    D=板端 rec.pcm 真唤醒词(2)
E=neg_zh 1.pcm 切片          F=板端 rec.pcm 假语音(8)"""
import glob
import os
import wave

import numpy as np
import webrtcvad

import features as F


def read_any(path):
    if path.endswith(".pcm"):
        return np.fromfile(path, "<i2").astype(np.float32) / 32768.0, 16000
    with wave.open(path) as w:
        sr, ch = w.getframerate(), w.getnchannels()
        raw = np.frombuffer(w.readframes(w.getnframes()), "<i2")
        x = raw.reshape(-1, ch).mean(axis=1).astype(np.float32) / 32768.0
    return x, sr


def resample(x, sr, tgt=16000):
    if sr == tgt:
        return x
    n = int(len(x) * tgt / sr)
    return np.interp(np.arange(n) * sr / tgt, np.arange(len(x)), x).astype(np.float32)


def vad_segs(x, sr=16000):
    frame = int(sr * 0.03)
    vad = webrtcvad.Vad(2)
    pcm = (np.clip(x, -1, 1) * 32000).astype("<i2").tobytes()
    n = len(pcm) // (frame * 2)
    voiced = [vad.is_speech(pcm[i * frame * 2:(i + 1) * frame * 2], sr)
              for i in range(n)]
    segs, i = [], 0
    while i < n:
        if voiced[i]:
            j, last = i, i
            while j < n:
                if voiced[j]:
                    last = j
                elif (j - last) * 0.03 > 0.30:
                    break
                j += 1
            if (last + 1 - i) * 0.03 >= 0.30:
                segs.append((i * 0.03, (last + 1) * 0.03))
            i = j
        else:
            i += 1
    return segs


def seg_mels(x, segs, max_n=40):
    """各段中心 1s 窗的 mel (段长>=0.8s 才要), 返回 (n,40)。"""
    out = []
    for s, e in segs:
        if e - s < 0.8:
            continue
        seg = x[int(s * 16000):int(e * 16000)]
        out.append(F.samples_to_mel(F.to_1s_window(seg)).mean(axis=0))
        if len(out) >= max_n:
            break
    return np.array(out)


def read_wav(p):
    with wave.open(p) as w:
        return np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float32) / 32768.0


groups = {}
xa, _ = read_any("recordings/rec_segments/rec.pcm")
groups["A:rec_segments(训练源)"] = seg_mels(xa, vad_segs(xa))
xb, _ = read_any("recordings/rec_16k_16bit_1ch.pcm")
groups["B:rec_16k_16bit(高分)"] = seg_mels(xb, vad_segs(xb))
xc, _ = read_any("recordings/rec_16k_mono.wav")
groups["C:rec_16k_mono(低分)"] = seg_mels(xc, vad_segs(xc))
xd, _ = read_any("recordings/rec.pcm")
segs_d = vad_segs(xd)
groups["D:板端真唤醒词"] = seg_mels(xd, segs_d[-2:])
groups["F:板端假语音"] = seg_mels(xd, segs_d[:-2])
zh = [read_wav(p) for p in sorted(glob.glob("dataset/neg_zh/*.wav"))]
groups["E:neg_zh(1.pcm)"] = np.array(
    [F.samples_to_mel(F.to_1s_window(x)).mean(axis=0) for x in zh])

keys = list(groups)
print("组间平均 log-mel 谱 L2 距离 (每格=两组中心距离, 越小越同信道):")
print("      " + "".join(f"{k.split(':')[0]:>6s}" for k in keys))
for ka in keys:
    row = []
    for kb in keys:
        d = np.linalg.norm(groups[ka].mean(0) - groups[kb].mean(0))
        row.append(f"{d:6.2f}")
    print(f"{ka.split(':')[0]:>6s} " + "".join(row))

print("\n各组中心谱 (40 mel 带, 每 4 带取样):")
for k in keys:
    m = groups[k].mean(0)
    print(f"{k:24s} " + " ".join(f"{v:+5.2f}" for v in m[::4]))
