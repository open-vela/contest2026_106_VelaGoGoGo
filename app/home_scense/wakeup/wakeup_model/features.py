#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""log-mel 频谱特征提取 (numpy/scipy 实现, 便于移植 C)。

参数固定, 训练/量化/MCU 端必须一致:
  SR         = 16000
  N_FFT      = 512     (25ms 窗 400 样本零填到 512)
  FRAME_LEN  = 400     (25ms)
  FRAME_STEP = 320     (20ms)  -> 1s 产生 49 帧
  N_MELS     = 40
  FMIN/FMAX  = 0 / 8000 Hz
输出: float32, shape (49, 40), 取 log10(mel_power + eps)。
"""
import wave
import numpy as np

SR = 16000
N_FFT = 512
FRAME_LEN = 400
FRAME_STEP = 320
N_MELS = 40
FMIN = 0.0
FMAX = 8000.0
EPS = 1e-10
WINDOW_SEC = 1.0
N_SAMPLES = SR  # 16000
N_FRAMES = 1 + (N_SAMPLES - FRAME_LEN) // FRAME_STEP  # 49


def read_wav(path):
    """读 16-bit 单声道 wav, 返回 float32 [-1,1]。"""
    wf = wave.open(path, "rb")
    assert wf.getsampwidth() == 2 and wf.getnchannels() == 1, f"{path}: 需 16-bit 单声道"
    sr = wf.getframerate()
    raw = wf.readframes(wf.getnframes())
    wf.close()
    x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    return sr, x


def hz_to_mel(f):
    return 2595.0 * np.log10(1.0 + f / 700.0)


def mel_to_hz(m):
    return 700.0 * (10.0 ** (m / 2595.0) - 1.0)


def mel_filterbank(sr, n_fft, n_mels, fmin, fmax):
    """三角 mel 滤波器组, shape (n_mels, n_fft//2+1)。"""
    fft_bins = n_fft // 2 + 1
    mmin, mmax = hz_to_mel(fmin), hz_to_mel(fmax)
    mel_points = np.linspace(mmin, mmax, n_mels + 2)
    hz_points = mel_to_hz(mel_points)
    bin_points = np.floor((n_fft + 1) * hz_points / sr).astype(int)
    fb = np.zeros((n_mels, fft_bins), dtype=np.float32)
    for m in range(n_mels):
        left, center, right = bin_points[m], bin_points[m + 1], bin_points[m + 2]
        for k in range(left, center):
            if center > left:
                fb[m, k] = (k - left) / (center - left)
        for k in range(center, right):
            if right > center:
                fb[m, k] = (right - k) / (right - center)
    return fb


_FB = mel_filterbank(SR, N_FFT, N_MELS, FMIN, FMAX)
_WINDOW = np.hamming(FRAME_LEN).astype(np.float32)


def samples_to_mel(x):
    """x: 1D float32, 期望长度 16000。返回 (49, 40) float32。"""
    if len(x) < N_SAMPLES:
        x = np.pad(x, (0, N_SAMPLES - len(x)))
    x = x[:N_SAMPLES]
    # 分帧
    n_frames = 1 + (len(x) - FRAME_LEN) // FRAME_STEP
    frames = np.lib.stride_tricks.as_strided(
        x,
        shape=(n_frames, FRAME_LEN),
        strides=(x.strides[0] * FRAME_STEP, x.strides[0]),
    ).copy()
    frames = frames * _WINDOW
    # FFT 功率谱
    spec = np.fft.rfft(frames, n=N_FFT)
    power = (np.abs(spec) ** 2).astype(np.float32)
    # mel
    mel = power @ _FB.T  # (n_frames, n_mels)
    mel = np.maximum(mel, EPS)
    log_mel = np.log10(mel).astype(np.float32)
    return log_mel  # (49, 40)


def wav_to_mel(path):
    sr, x = read_wav(path)
    assert sr == SR, f"{path}: 采样率需 {SR}"
    return samples_to_mel(x)


def to_1s_window(x, offset=None):
    """从任意长度样本取 1s(16000) 窗口, 不足补零。offset=None 取居中。"""
    if len(x) >= N_SAMPLES:
        if offset is None:
            offset = (len(x) - N_SAMPLES) // 2
        return x[offset:offset + N_SAMPLES]
    else:
        return np.pad(x, (0, N_SAMPLES - len(x)))


if __name__ == "__main__":
    import sys
    m = wav_to_mel(sys.argv[1])
    print("mel shape:", m.shape, "dtype:", m.dtype)
    print("min/max/mean:", m.min(), m.max(), m.mean())
