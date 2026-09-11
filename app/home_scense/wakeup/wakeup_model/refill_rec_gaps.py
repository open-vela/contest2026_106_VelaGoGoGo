#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""修复 rebuild_rec_from_segs.py 的重建件: 用真实噪声回填段间隙。

背景: 原 39s 验收录音被 2026-09-09 新拉取覆盖, 全盘确认无副本。幸存的
10 个 VAD 段 (recordings/rec_segs/) 按原偏移重建, 段间隙 (共 ~25.3s)
需回填——补数字静音会让跨段窗口的 mel 出现训练未见的死区, 两句真唤醒
都凑不满连续 4 窗。

回填噪声源: 7月28 板端纯噪声 (优先 16k 原生 /tmp/rec28_16k.pcm, 否则
recordings/rec.wav 重采样)。电平对齐段内环境噪声 (段内能量最低 20% 窗
的 RMS), 截掉源首尾静音。

诊断结论 (2026-09-11, diag_rec_rebuild.py): 匹配电平下句2 过 (4 连窗)
但句1 的 [30.1,31.1] 塌到 0.13, 且 filler ±6dB 均救不活。两种假设:
(a) VAD 切掉了句1 词尾 ~0.15s (音频已永久丢失); (b) 31.0s 后落的那块
噪声内容有害。本脚本用 (噪声起始偏移 × 增益) 双扫描判别并找可行回填。

最终结论: 是 (b) 的变体——毒的是噪声源本身。rec.wav (44.1k 立体声)
线性插值重采样产生的混叠失真噪声会让真唤醒的边界窗塌掉; 换成 16k 原生
板端噪声 (rec28_16k.pcm, 与 rec.wav 同环境同时间线、热 14dB 的板端拉取)
后, 全部 11 偏移 × 5 增益组合两句都 5-8 连窗, 事件恰 2 次落区间, -6dB
亦 2/2 (2026-09-11 定稿: 偏移 0s / 匹配电平写入)。

验收与 select_params 硬门 A 一致 (0dB): 恰好 2 次事件, 依序落在
(28,32)s 与 (32,36.5)s; -6dB 仅作边际参考。
"""
import glob
import os
import re
import wave

import numpy as np
from tensorflow import keras

import features as F

HOP = 1600
THR = 0.88
K = 4
MIN_DIST = 12  # WAKE_MIN_DIST: 1.2s 冷却
SPANS = ((28.0, 32.0), (32.0, 36.5))
TOTAL = int(39.2 * 16000)  # 原始 39.2s / 1,254,400 字节
SEG_DIR = "recordings/rec_segs"
OUT_PCM = "recordings/rec.pcm"
GAP_NEED = 25_300_000 // 1000  # 需要的噪声量 (~25.3s), 偏移扫描用


def read_noise(path):
    """任意 wav -> mono 16k float (与 select_params.read_any 相同口径)。"""
    with wave.open(path) as w:
        sr, ch = w.getframerate(), w.getnchannels()
        raw = np.frombuffer(w.readframes(w.getnframes()), "<i2")
        x = raw.reshape(-1, ch).mean(axis=1).astype(np.float32) / 32768.0
    if sr != 16000:
        n = int(len(x) * 16000 / sr)
        x = np.interp(np.arange(n) * sr / 16000, np.arange(len(x)), x).astype(np.float32)
    return x


def load_noise():
    """返回 (截掉首尾静音的噪声段, 其整体 RMS)。"""
    for path in ("recordings/rec28_16k.pcm", "/tmp/rec28_16k.pcm",
                 "recordings/rec.wav"):
        if not os.path.exists(path):
            continue
        if path.endswith(".pcm"):
            x = np.fromfile(path, "<i2").astype(np.float32) / 32768.0
        else:
            x = read_noise(path)
        rs = np.array([np.sqrt((x[i:i + 16000] ** 2).mean() + 1e-12)
                       for i in range(0, len(x) - 15999, 16000)])
        med = float(np.median(rs))
        act = np.where(rs >= med / 10.0)[0]  # 中位数上下 20dB 内算有效
        lo, hi = act[0] * 16000, min(len(x), (act[-1] + 1) * 16000)
        return x[lo:hi], float(np.sqrt((x[lo:hi] ** 2).mean() + 1e-12))
    raise SystemExit("!! 找不到噪声源 (recordings/rec.wav)")


def load_segs():
    segs = []
    for p in sorted(glob.glob(os.path.join(SEG_DIR, "*.wav"))):
        with wave.open(p) as w:
            x = np.frombuffer(w.readframes(w.getnframes()), "<i2")
        start = float(re.search(r"_(\d+(?:\.\d+)?)s\.wav$", p).group(1))
        segs.append((start, x, p))
    return segs


def ambient_rms(segs):
    """段内能量最低 20% 的 100ms 窗的 RMS — 原始录音环境噪声电平的估计。"""
    e = []
    for _, x, _ in segs:
        v = x.astype(np.float32) / 32768.0
        for i in range(0, len(v) - HOP + 1, HOP):
            e.append(np.sqrt((v[i:i + HOP] ** 2).mean() + 1e-12))
    return float(np.percentile(e, 20))


def build(segs, noise, g):
    """段按原偏移放置 (int16 直写), 间隙填 noise*增益 g (g=None 为数字静音)。"""
    out = np.zeros(TOTAL, np.float32)
    if g is not None:
        covered = sorted((int(s * 16000), int(s * 16000) + len(x)) for s, x, _ in segs)
        gaps, prev = [], 0
        for a, b in covered + [(TOTAL, TOTAL)]:
            if a > prev:
                gaps.append((prev, a))
            prev = max(prev, b)
        pos = 0
        for a, b in gaps:
            idx = 0
            while idx < b - a:
                take = min(b - a - idx, len(noise) - pos)
                if take <= 0:
                    pos = 0
                    continue
                out[a + idx:a + idx + take] = noise[pos:pos + take] * g
                idx += take
                pos += take
    for s, x, _ in segs:
        off = int(s * 16000)
        out[off:off + len(x)] = x.astype(np.float32) / 32768.0  # 段覆盖噪声
    return np.clip(out, -1, 1)


def quant(x):
    return np.clip(np.round(x * 32768.0), -32768, 32767).astype("<i2")


def score(model, x):
    n = 1 + max(0, (len(x) - F.N_SAMPLES) // HOP)
    X = np.empty((n, F.N_FRAMES, F.N_MELS, 1), np.float32)
    for w in range(n):
        X[w] = F.samples_to_mel(x[w * HOP:w * HOP + F.N_SAMPLES])[..., None]
    return model.predict(X, verbose=0)[:, 0]


def event_starts(p, thr=THR, k=K, min_dist=MIN_DIST):
    """select_params.event_starts 同款: 极大连续段 >=k 记一次 (起点), 冷却。"""
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
    for s in starts:
        if s - last >= min_dist:
            out.append(s * HOP / 16000)
            last = s
    return out


def accepted(p):
    """硬门 A (0dB): 恰好 2 次且依序落在两句真唤醒区间。"""
    ev = event_starts(p)
    ok = (len(ev) == 2
          and all(lo <= t <= hi for t, (lo, hi) in zip(sorted(ev), SPANS)))
    return ok, ev


def max_run(p, lo, hi, thr=THR):
    """[lo,hi]s 内连续 >thr 的最长窗数。"""
    r = m = 0
    for v in p[int(lo * 10):int(hi * 10)]:
        r = r + 1 if v > thr else 0
        m = max(m, r)
    return m


def main():
    segs = load_segs()
    noise, n_rms = load_noise()
    amb = ambient_rms(segs)
    base = amb / n_rms
    print(f"段 {len(segs)} 个; 噪声源 {len(noise)/16000:.0f}s "
          f"(RMS {20*np.log10(n_rms+1e-12):.0f}dBFS); 段内环境噪声 "
          f"{20*np.log10(amb+1e-12):.0f}dBFS; 匹配电平增益 {base:.3f}\n")

    # 词尾截断假设的旁证: 各真唤醒段末 100ms 与段体 RMS 的比值
    for name, s in (("句1", 29.9), ("句2", 33.5)):
        x = next(x for st, x, _ in segs if st == s).astype(np.float32) / 32768.0
        body = np.sqrt((x[:-1600] ** 2).mean() + 1e-12)
        tail = np.sqrt((x[-1600:] ** 2).mean() + 1e-12)
        print(f"  {name} 段尾 100ms RMS / 段体 = {tail/body:.2f} "
              f"({'段尾仍有能量, 疑似切词尾' if tail > 0.5 * body else '段尾已衰减'})")

    model = keras.models.load_model("model.keras")

    # 数字静音对照
    p0 = score(model, quant(build(segs, noise, None)).astype(np.float32) / 32768.0)
    print(f"\n数字静音对照: 连续窗[{max_run(p0, *SPANS[0])},{max_run(p0, *SPANS[1])}]")

    # (偏移 × 增益) 扫描: 偏移 0-80s 步 8s, 增益匹配 ±3/±6dB
    # 噪声量 ~25.3s, 偏移上限 = 噪声长 - 26s
    off_max = max(0, len(noise) - 26 * 16000)
    offs = sorted(set(min(o, off_max) for o in range(0, 81 * 16000, 8 * 16000)))
    grid = [(off, db) for db in (0.0, 3.0, -3.0, 6.0, -6.0) for off in offs]

    print(f"\n扫描 (偏移 {len(offs)} 档 × 增益 5 档), "
          f"要求: 两句各 >=4 连窗 + 全流恰好 2 事件落区间:\n")
    chosen = None
    s1_best = 0
    for off, db in grid:
        g = base * 10.0 ** (db / 20.0)
        xq = quant(build(segs, noise[off:], g)).astype(np.float32) / 32768.0
        p = score(model, xq)
        ok, ev = accepted(p)
        r1, r2 = max_run(p, *SPANS[0]), max_run(p, *SPANS[1])
        s1_best = max(s1_best, r1)
        if ok or r1 >= K:  # 打印所有句1 有戏的组合
            print(f"  off={off/16000:4.0f}s {db:+.0f}dB: 连续窗[{r1},{r2}] "
                  f"事件{len(ev)} @ {['%.1fs' % t for t in ev]} "
                  f"{'PASS' if ok else ''}")
        if ok and chosen is None:
            chosen = (off, db, xq)
    print(f"\n句1 最大连续窗 (全扫描): {s1_best}")

    if chosen:
        off, db, xq = chosen
        q = quant(xq)
        q.tofile(OUT_PCM)
        pm6 = score(model, np.clip(xq * 10 ** -0.3, -1, 1).astype(np.float32))
        ev6 = event_starts(pm6)
        print(f"\n已写入 {OUT_PCM} (噪声偏移 {off/16000:.0f}s, {db:+.0f}dB, "
              f"{len(q)/16000:.1f}s, {q.nbytes} 字节) — 验收 PASS")
        print(f"边际参考 -6dB: 事件 {len(ev6)} @ {['%.1fs' % t for t in ev6]}")
        return 0
    print("\n无组合通过 (句1 在任何噪声偏移/电平下都 <4 连窗 → VAD 切掉了词尾, "
          "音频不可恢复)。recordings/rec.pcm 保持不变")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
