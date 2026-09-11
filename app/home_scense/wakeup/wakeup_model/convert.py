#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""int8 量化导出 model.tflite, 生成 TFLite Micro 的 model.cc/model.h,
labels.txt, feature_config.json (含输入/输出 quant 参数)。
"""
import os
import json
import numpy as np
import tensorflow as tf
from tensorflow import keras

import features as F

MODEL = "model.keras"
OUT_TFLITE = "model.tflite"
OUT_CC = "model.cc"
OUT_H = "model.h"
N_REP = 300  # representative 样本数
LEVEL_RMS_DB = (-40.0, -12.0)  # 与 train.py 一致: 随机电平归一范围


def representative_dataset(d):
    bg_paths = d["bg_paths"]
    paths = d["train_paths"]
    labels = d["train_labels"]
    n = min(N_REP, len(paths))
    idx = np.random.RandomState(0).choice(len(paths), n, replace=False)
    for k in idx:
        x = F.read_wav(paths[k])[1]
        # 随机裁剪 + 随机噪声混合 + 随机电平 (与训练分布一致)
        if len(x) >= F.N_SAMPLES:
            off = np.random.randint(0, len(x) - F.N_SAMPLES + 1)
            x = x[off:off + F.N_SAMPLES]
        else:
            x = np.pad(x, (0, F.N_SAMPLES - len(x)))
        if labels[k] in (0, 1) and np.random.rand() < 0.6:
            bp = bg_paths[np.random.randint(len(bg_paths))]
            nse = F.read_wav(bp)[1]
            if len(nse) >= F.N_SAMPLES:
                o2 = np.random.randint(0, len(nse) - F.N_SAMPLES + 1)
                nse = nse[o2:o2 + F.N_SAMPLES]
            else:
                nse = np.pad(nse, (0, F.N_SAMPLES - len(nse)))
            sp = np.mean(x ** 2) + 1e-12
            npw = np.mean(nse ** 2) + 1e-12
            snr = np.random.uniform(-5, 15)
            g = np.sqrt(sp / (npw * 10 ** (snr / 10)))
            x = np.clip(x + g * nse, -1, 1).astype(np.float32)
        # 随机电平归一 (与 train.py set_random_level 一致)
        rms = np.sqrt(np.mean(x ** 2) + 1e-12)
        level_rms = 10.0 ** (np.random.uniform(*LEVEL_RMS_DB) / 20.0)
        x = np.clip(x * (level_rms / rms), -1.0, 1.0).astype(np.float32)
        m = F.samples_to_mel(x)[..., None].astype(np.float32)
        yield [np.expand_dims(m, 0)]


def to_c_array(tflite_bytes, var="model_tflite"):
    toks = [f"0x{b:02x}" for b in tflite_bytes]
    lines = []
    for i in range(0, len(toks), 16):  # 每行 16 字节
        lines.append("  " + ",".join(toks[i:i + 16]))
    body = ",\n".join(lines)
    return (f"/* 自动生成, 勿手动编辑。共 {len(tflite_bytes)} 字节 */\n"
            f"#include <cstdint>\n\n"
            f"const unsigned char {var}[] = {{\n{body}\n}};\n"
            f"const unsigned int {var}_len = {len(tflite_bytes)};\n")


def main():
    np.random.seed(0)
    d = np.load("data.npz", allow_pickle=True)
    model = keras.models.load_model(MODEL)

    conv = tf.lite.TFLiteConverter.from_keras_model(model)
    conv.optimizations = [tf.lite.Optimize.DEFAULT]
    conv.representative_dataset = lambda: representative_dataset(d)
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    conv.target_spec.supported_types = [tf.int8]
    conv.inference_input_type = tf.int8
    conv.inference_output_type = tf.int8
    tflite = conv.convert()

    with open(OUT_TFLITE, "wb") as fp:
        fp.write(tflite)
    print(f"{OUT_TFLITE}: {len(tflite)} 字节")

    # C 数组
    with open(OUT_CC, "w") as fp:
        fp.write(to_c_array(tflite))
    with open(OUT_H, "w") as fp:
        fp.write((f"#ifndef MODEL_H_\n#define MODEL_H_\n"
                  f"extern const unsigned char model_tflite[];\n"
                  f"extern const unsigned int model_tflite_len;\n#endif\n"))
    print(f"{OUT_CC} / {OUT_H} 已生成")

    # 读取 quant 参数
    interp = tf.lite.Interpreter(model_content=tflite)
    interp.allocate_tensors()
    ind = interp.get_input_details()[0]
    outd = interp.get_output_details()[0]
    print("input :", ind["shape"], ind["dtype"], ind["quantization"])
    print("output:", outd["shape"], outd["dtype"], outd["quantization"])

    classes = list(d["classes"])
    with open("labels.txt", "w") as fp:
        fp.write("\n".join(classes) + "\n")

    thr = None
    if os.path.exists("metrics.json"):
        met = json.load(open("metrics.json"))
        thr = (met.get("threshold") or {}).get("deployment_threshold") \
            or met.get("target_best_threshold")

    cfg = {
        "classes": classes,
        "feature": {"sr": F.SR, "n_fft": F.N_FFT, "frame_len": F.FRAME_LEN,
                    "frame_step": F.FRAME_STEP, "n_mels": F.N_MELS,
                    "n_frames": F.N_FRAMES, "fmin": F.FMIN, "fmax": F.FMAX,
                    "window": "hamming", "log": "log10", "eps": F.EPS},
        "input": {"name": ind["name"], "shape": ind["shape"].tolist(),
                  "dtype": "int8", "scale": float(ind["quantization"][0]),
                  "zero_point": int(ind["quantization"][1])},
        "output": {"name": outd["name"], "shape": outd["shape"].tolist(),
                   "dtype": "int8", "scale": float(outd["quantization"][0]),
                   "zero_point": int(outd["quantization"][1]),
                   "note": "dequant: prob = scale*(q - zero_point)"},
        "target_threshold": thr,
    }
    with open("feature_config.json", "w") as fp:
        json.dump(cfg, fp, indent=2, ensure_ascii=False)
    print("feature_config.json / labels.txt 已生成")


if __name__ == "__main__":
    main()
