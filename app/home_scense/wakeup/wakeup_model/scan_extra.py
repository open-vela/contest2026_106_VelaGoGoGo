#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""扫描候选录音: VAD 切段 + 用当前模型打分 (背题型模型: 见过的 31 段 ≥0.9,
新说唤醒词 ~0.3, 非唤醒词语音更低)。判断有没有漏网的新唤醒词样本。"""
import wave

import numpy as np
import webrtcvad
from tensorflow import keras

import features as F

model = keras.models.load_model("model.keras")

FILES = [
    "recordings/rec_segments/rec.pcm",        # 31 段 target 的源头 (186s)
    "recordings/rec_16k_mono.wav",            # 116s, = rec.wav 降采样?
    "recordings/rec_16k_16bit_1ch.pcm",       # 99s
    "recordings/t0.wav",                      # 10s 48k 立体声
    "recordings/t.wav",                       # 5s
    "recordings/t1.wav",                      # 10s 16k 单声道
    "recordings/rec.pcm",                     # 39s 板端录音 (参照: 真=0.27/0.37)
]


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
    """webrtcvad, 参数同 extract_voice.py (aggressiveness 2, bridge 0.3s, min 0.3s)。"""
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


def score(x):
    m = F.samples_to_mel(F.to_1s_window(x))[..., None]
    return float(model.predict(m[None], verbose=0)[0, 0])


for path in FILES:
    try:
        x, sr = read_any(path)
        x = resample(x, sr)
    except Exception as e:
        print(f"{path}: 读取失败 {e}")
        continue
    segs = vad_segs(x)
    print(f"\n== {path}  ({len(x)/16000:.1f}s, {len(segs)} 段) ==")
    for s, e in segs:
        seg = x[int(s * 16000):int(e * 16000)]
        print(f"  {s:7.1f}-{e:7.1f}s  dur={e-s:4.1f}s  prob={score(seg):.3f}")
