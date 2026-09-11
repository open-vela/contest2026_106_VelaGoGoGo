#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""一次性抢救脚本 (2026-09-09): 重建 39s 验收录音 recordings/rec.pcm。

背景: /home/mi/log/rec.pcm (39s 板端, 6 段假语音 + 2 句真唤醒 29.9s/33.5s,
验收硬地板) 在 2026-09-09 上午被新一轮板端拉取 (6ch/48k/32bit 麦阵原始数据,
rec.pcm/rec1.pcm/rec2.pcm/rec.wav) 覆盖, 原文件丢失。
幸存副本: /tmp/rec_segs/ 的 10 个 VAD 切段 (9月5日 诊断会话产物, 含两句真唤醒
seg_008_29.9s.wav / seg_009_33.5s.wav)。/tmp 重启即失, 本脚本把它们:

  1. 抢救到 recordings/rec_segs/ (永久保存)
  2. 按段原始时间偏移重建 39s 流 (段间补数字静音) -> recordings/rec.pcm
  3. 用 model.keras 在部署参数 (0.88 / 连续4窗 / hop 100ms) 下验证:
     必须恰好 2 次事件, 且分别落在真唤醒区间 (28-32s, 32-36.5s)

注: 重建件与原版的差别仅是段间静音为数字零 (原为环境噪声), 模型对静音
打分 ~0, 不影响验收语义。若日后找回原始 39s 文件, 直接覆盖 recordings/rec.pcm。
"""
import glob
import os
import re
import shutil
import wave

import numpy as np
from tensorflow import keras

import features as F

SRC_DIR = "/tmp/rec_segs"
KEEP_DIR = "recordings/rec_segs"
OUT_PCM = "recordings/rec.pcm"
HOP = 1600
THR = 0.88
K = 4
TRUE_WAKE_SPANS = ((28.0, 32.0), (32.0, 36.5))


def main():
    segs = sorted(glob.glob(os.path.join(SRC_DIR, "*.wav")))
    if not segs:
        print(f"!! {SRC_DIR} 为空 (tmp 已被清?), 无法重建")
        return 1
    info = []
    for p in segs:
        with wave.open(p) as w:
            assert (w.getsampwidth() == 2 and w.getnchannels() == 1
                    and w.getframerate() == 16000), (p, w.getparams())
            x = np.frombuffer(w.readframes(w.getnframes()), "<i2")
        m = re.search(r"_(\d+(?:\.\d+)?)s\.wav$", os.path.basename(p))
        start = float(m.group(1))
        info.append((start, x, p))
        print(f"{os.path.basename(p):22s} 起点 {start:5.1f}s  时长 {len(x)/16000:4.1f}s")

    # 1) 抢救
    os.makedirs(KEEP_DIR, exist_ok=True)
    for p in segs:
        shutil.copy2(p, KEEP_DIR)
    print(f"\n[1] 已抢救 {len(segs)} 段 -> {KEEP_DIR}")

    # 2) 重建
    total = int(max(s + len(x) / 16000 for s, x, _ in info) * 16000)
    out = np.zeros(total, "<i2")
    for s, x, _ in info:
        off = int(s * 16000)
        out[off:off + len(x)] = x
    out.tofile(OUT_PCM)
    print(f"[2] 重建 {OUT_PCM}: {total/16000:.1f}s, {out.nbytes} 字节")

    # 3) 部署参数验证 (demo 语义: 裸概率连续 K 窗超阈)
    model = keras.models.load_model("model.keras")
    x = out.astype(np.float32) / 32768.0
    n = 1 + (len(x) - F.N_SAMPLES) // HOP
    X = np.empty((n, F.N_FRAMES, F.N_MELS, 1), np.float32)
    for w in range(n):
        X[w] = F.samples_to_mel(x[w * HOP:w * HOP + F.N_SAMPLES])[..., None]
    p = model.predict(X, verbose=0)[:, 0]
    events, cur, run_start = [], 0, 0
    for i, v in enumerate(p):
        if v > THR:
            if cur == 0:
                run_start = i
            cur += 1
            if cur == K:
                events.append(run_start)
                cur = 0  # 触发后重置 (demo 语义)
        else:
            cur = 0
    ts = [e * HOP / 16000 for e in events]
    print(f"[3] 全流 prob max={p.max():.3f}")
    for lo, hi in TRUE_WAKE_SPANS:
        seg_p = p[int(lo*10):int(hi*10)]
        print(f"    真唤醒区间 {lo}-{hi}s: 峰值 {seg_p.max():.3f} @ {int(lo*10+np.argmax(seg_p))*0.1:.1f}s")
    print(f"    事件 ({len(ts)} 次): {['%.1fs' % t for t in ts]}")

    ok = len(ts) == 2 and all(
        any(lo <= t <= hi for lo, hi in TRUE_WAKE_SPANS) for t in ts)
    print(f"\n验收: {'PASS - 恰好 2 次且都在真唤醒区间' if ok else 'FAIL - 检查上面的输出'}")
    return 0 if ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
