#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""rec.pcm 重建诊断 (只读, 不写任何文件):

1. 全盘搜索 39s 原件的可能副本 (大小恰为 1,254,400 字节)
2. 噪声源 recordings/rec.wav (44.1k 立体声) 的逐秒 RMS 分布
   —— refill_rec_gaps.py 用前 1s 估 RMS 得到 3083 倍增益, 疑开头静默
3. 用稳健电平 (逐秒 RMS 中位数) 回填后, 打印两句真唤醒附近的逐窗概率
   —— 判断第二句 (seg_009) 边界窗是"差一点"(电平问题) 还是"没救"(VAD 切掉了词头)
"""
import glob
import os
import subprocess
import wave

import numpy as np
from tensorflow import keras

import features as F
from refill_rec_gaps import (HOP, SEG_DIR, TOTAL, build, read_noise,
                             ambient_rms, score, event_starts, max_run)

WANT = 1254400  # 39.2s * 16000 * 2 字节


def find_originals():
    print(f"== 1. 搜索 {WANT} 字节的原件副本 (仅 /home/mi /tmp /mnt /media, 单根 60s) ==",
          flush=True)
    hits = []
    for root in ("/home/mi", "/tmp", "/mnt", "/media"):
        if not os.path.isdir(root):
            continue
        try:
            r = subprocess.run(["find", root, "-type", "f", "-size", f"{WANT}c"],
                               capture_output=True, text=True, timeout=60)
            hits += [l for l in r.stdout.splitlines() if l]
        except subprocess.TimeoutExpired:
            print(f"  ({root} 扫描超时, 跳过)", flush=True)
    print("\n".join(hits) if hits else "  (无)", flush=True)
    # 顺带列出 >1MB 的 pcm, 供肉眼比对 (find 实现, glob 递归扫 openvela 树太慢)
    print("\n  >1MB 的 pcm 文件:", flush=True)
    for root in ("/home/mi", "/tmp"):
        try:
            r = subprocess.run(["find", root, "-type", "f", "-name", "*.pcm",
                                "-size", "+1000k"],
                               capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired:
            continue
        for p in sorted(r.stdout.splitlines()):
            sz = os.path.getsize(p)
            mark = "  <-- 大小吻合!" if sz == WANT else ""
            print(f"    {sz:>10,}  {p}{mark}", flush=True)


def noise_levels():
    print("\n== 2. 噪声源 rec.wav 逐秒 RMS 分布 ==")
    x = read_noise("recordings/rec.wav")
    rs = []
    for i in range(0, len(x) - 16000, 16000):
        rs.append(np.sqrt((x[i:i + 16000] ** 2).mean() + 1e-12))
    rs = np.array(rs)
    db = 20 * np.log10(rs + 1e-12)
    print(f"  共 {len(rs)}s, 前 5s: {np.round(db[:5], 1).tolist()}")
    print(f"  p10={np.percentile(db,10):.1f} p50={np.percentile(db,50):.1f} "
          f"p90={np.percentile(db,90):.1f} dBFS (逐秒)")
    loud = np.where(db > np.percentile(db, 50) + 20)[0]
    if len(loud):
        print(f"  注意: {len(loud)}s 比中位数响 20dB 以上 @ {loud[:10].tolist()}s...")
    return x, float(np.median(rs))


def main():
    find_originals()
    noise, med_rms = noise_levels()

    segs = []
    for p in sorted(glob.glob(os.path.join(SEG_DIR, "*.wav"))):
        with wave.open(p) as w:
            xw = np.frombuffer(w.readframes(w.getnframes()), "<i2")
        import re as _re
        segs.append((float(_re.search(r"_(\d+(?:\.\d+)?)s\.wav$", p).group(1)), xw, p))
    amb = ambient_rms(segs)
    base = amb / med_rms
    print(f"\n== 3. 稳健匹配电平回填 (增益 {base:.2f}, 段内环境噪声 "
          f"{20*np.log10(amb+1e-12):.0f}dBFS vs 噪声中位数 {20*np.log10(med_rms+1e-12):.0f}dBFS) ==")
    model = keras.models.load_model("model.keras")
    for rel in (1.0, 10 ** 0.3, 10 ** -0.3):
        g = base * rel
        raw = build(segs, noise, g)
        clip = float((np.abs(raw) > 0.999).mean())
        xq = np.clip(np.round(raw * 32768.0), -32768, 32767).astype("<i2")
        p = score(model, xq.astype(np.float32) / 32768.0)
        ev = event_starts(p)
        print(f"\n  -- 增益 {g:.2f} ({'匹配' if rel==1 else '%+.0fdB' % (20*np.log10(rel))}), "
              f"削顶占比 {clip:.2%} --")
        print(f"  事件 {len(ev)} @ {['%.1fs' % t for t in ev]}, "
              f"连续窗[{max_run(p, 28.0, 32.0)},{max_run(p, 32.0, 36.5)}]")
        for name, lo, hi in (("句1", 28.6, 31.4), ("句2", 33.0, 35.0)):
            row = p[int(lo * 10):int(hi * 10)]
            print(f"    {name} 逐窗概率 ({lo:.1f}-{hi:.1f}s, 竖线=超阈):")
            print("      " + " ".join(
                f"{'|' if v > 0.88 else ' '}{v:.2f}" for v in row))
        top = np.argsort(p[:280])[::-1][:5]
        print(f"    假区(0-28s) top5: "
              + ", ".join(f"{t*0.1:.1f}s={p[t]:.3f}" for t in sorted(top)))


if __name__ == "__main__":
    main()
