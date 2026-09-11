#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证 int8 tflite 模型: 加载 model.tflite, 对 test 集算 mel -> int8 -> 推理,
对比 Keras 模型, 确认量化掉点在可接受范围; 并核对 model.cc 字节数。"""
import os
import json
import numpy as np
import tensorflow as tf
from tensorflow import keras

import features as F


def dequant(q, scale, zp):
    return scale * (q.astype(np.float32) - zp)


def main():
    cfg = json.load(open("feature_config.json"))
    d = np.load("data.npz", allow_pickle=True)
    classes = list(d["classes"])
    in_scale = cfg["input"]["scale"]
    in_zp = cfg["input"]["zero_point"]
    out_scale = cfg["output"]["scale"]
    out_zp = cfg["output"]["zero_point"]

    # Keras 模型
    kmodel = keras.models.load_model("model.keras")

    # tflite
    interp = tf.lite.Interpreter(model_path="model.tflite")
    interp.allocate_tensors()
    ind = interp.get_input_details()[0]
    outd = interp.get_output_details()[0]
    assert ind["dtype"] == np.int8, "输入非 int8"
    print("tflite input :", ind["shape"], ind["dtype"], "q", ind["quantization"])
    print("tflite output:", outd["shape"], outd["dtype"], "q", outd["quantization"])

    paths = d["test_paths"]
    labels = d["test_labels"]
    n = len(paths)
    kpred = np.zeros((n, 3), np.float32)
    tpred = np.zeros((n, 3), np.float32)

    for i, p in enumerate(paths):
        x = F.to_1s_window(F.read_wav(p)[1])
        mel = F.samples_to_mel(x)[..., None].astype(np.float32)
        # keras
        kpred[i] = kmodel.predict(mel[None], verbose=0)[0]
        # tflite (int8 输入)
        q = np.clip(np.round(mel / in_scale) + in_zp, -128, 127).astype(np.int8)
        interp.set_tensor(ind["index"], q[None])
        interp.invoke()
        out = interp.get_tensor(outd["index"])[0]
        tpred[i] = dequant(out, out_scale, out_zp)

    kacc = (kpred.argmax(1) == labels).mean()
    tacc = (tpred.argmax(1) == labels).mean()
    # 概率一致性
    diff = np.abs(kpred - tpred).max()
    print(f"\nKeras test acc : {kacc:.4f}")
    print(f"TFLite test acc: {tacc:.4f}")
    print(f"掉点           : {kacc - tacc:+.4f}")
    print(f"最大概率偏差   : {diff:.4f}")

    # 核对 model.cc 字节数
    tflite_size = os.path.getsize("model.tflite")
    cc = open("model.cc").read()
    n_hex = cc.count("0x")
    print(f"\nmodel.tflite = {tflite_size} 字节; model.cc 中 0x 计数 = {n_hex}")
    print("一致" if n_hex == tflite_size else "不一致!")

    # 抽样展示
    print("\n样本  keras_pred  tflite_pred  true")
    for i in range(0, n, max(1, n // 8)):
        print(f"  {classes[kpred[i].argmax()]:10s} {classes[tpred[i].argmax()]:10s} {classes[labels[i]]}")


if __name__ == "__main__":
    main()
