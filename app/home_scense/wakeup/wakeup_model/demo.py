#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""本地 demo: 用 rec_16k_mono.wav 验证唤醒模型。

流程: 1s 滑窗(步长 100ms) -> log-mel -> int8 量化 -> tflite 推理 ->
平滑 target 概率 -> 峰值检测定位唤醒事件 -> 打印时间戳 + 保存曲线图。
"""
import os
import json
import numpy as np
import wave
import tensorflow as tf

import features as F

AUDIO = "recordings/rec_16k_mono.wav"
MODEL = "model.tflite"
CFG = "feature_config.json"
HOP_SEC = 0.1          # 滑窗步长
SMOOTH_WIN = 5         # 滑动平均窗(帧数)
PEAK_THR = 0.5         # 唤醒峰值阈值
MIN_DIST_SEC = 1.2     # 两次唤醒最小间隔


def read_wav(path):
    wf = wave.open(path, "rb")
    assert wf.getsampwidth() == 2 and wf.getnchannels() == 1
    sr = wf.getframerate()
    raw = wf.readframes(wf.getnframes())
    wf.close()
    return sr, np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0


def moving_avg(x, w):
    if w <= 1:
        return x
    k = np.ones(w) / w
    return np.convolve(x, k, mode="same")


def find_peaks(prob, hop, min_dist_sec, thr):
    """简单峰值检测: 高于阈值且局部极大, 且相互间隔 >= min_dist。"""
    min_dist = int(min_dist_sec / hop)
    peaks = []
    for i in range(1, len(prob) - 1):
        if prob[i] >= thr and prob[i] >= prob[i - 1] and prob[i] >= prob[i + 1]:
            if not peaks or (i - peaks[-1]) >= min_dist:
                peaks.append(i)
            elif prob[i] > prob[peaks[-1]]:
                peaks[-1] = i
    return peaks


def main():
    cfg = json.load(open(CFG))
    in_scale = cfg["input"]["scale"]
    in_zp = cfg["input"]["zero_point"]
    out_scale = cfg["output"]["scale"]
    out_zp = cfg["output"]["zero_point"]
    labels = cfg["classes"]

    sr, x = read_wav(AUDIO)
    assert sr == F.SR, f"采样率需 {F.SR}, 实际 {sr}"
    dur = len(x) / sr
    print(f"音频: {AUDIO}  采样率={sr}  时长={dur:.2f}s")

    interp = tf.lite.Interpreter(model_path=MODEL)
    interp.allocate_tensors()
    ind = interp.get_input_details()[0]
    outd = interp.get_output_details()[0]

    hop = int(HOP_SEC * sr)
    n_win = 1 + (len(x) - F.N_SAMPLES) // hop
    print(f"滑窗: {F.N_SAMPLES}样本/{F.N_SAMPLES/sr:.1f}s, 步长 {HOP_SEC}s, 共 {n_win} 窗")

    probs = np.zeros((n_win, 3), np.float32)
    for i in range(n_win):
        seg = x[i * hop:i * hop + F.N_SAMPLES]
        mel = F.samples_to_mel(seg)[..., None].astype(np.float32)
        q = np.clip(np.round(mel / in_scale) + in_zp, -128, 127).astype(np.int8)
        interp.set_tensor(ind["index"], q[None])
        interp.invoke()
        out = interp.get_tensor(outd["index"])[0]
        probs[i] = out_scale * (out.astype(np.float32) - out_zp)

    t = np.arange(n_win) * HOP_SEC
    target_prob = moving_avg(probs[:, 0], SMOOTH_WIN)

    peaks = find_peaks(target_prob, HOP_SEC, MIN_DIST_SEC, PEAK_THR)
    print(f"\n检测到 {len(peaks)} 个唤醒事件 (阈值 {PEAK_THR}, 最小间隔 {MIN_DIST_SEC}s):")
    for p in peaks:
        cls = labels[int(probs[p].argmax())]
        print(f"  {t[p]:6.2f}s  target_prob={target_prob[p]:.3f}  argmax={cls}")

    # 画图
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(figsize=(12, 4))
        ax.plot(t, target_prob, label="target prob (smoothed)", lw=1)
        ax.axhline(PEAK_THR, color="r", ls="--", lw=1, label=f"thr={PEAK_THR}")
        for p in peaks:
            ax.axvline(t[p], color="g", alpha=0.5)
            ax.plot(t[p], target_prob[p], "g^", ms=10)
        ax.set_xlabel("time (s)")
        ax.set_ylabel("target probability")
        ax.set_title(f"{AUDIO}: wake-word detection ({len(peaks)} events)")
        ax.set_xlim(0, dur)
        ax.set_ylim(-0.05, 1.05)
        ax.legend(loc="upper right")
        plt.tight_layout()
        plt.savefig("demo_result.png", dpi=120)
        print("\n已保存曲线图 demo_result.png")
    except Exception as e:
        print("绘图失败:", e)


if __name__ == "__main__":
    main()
