#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 model.keras 导出浮点权重 C 数组 wake_model_weights.h (手写 C 推理用)。

Conv2D 权重布局: (kh,kw,in_ch,out_ch); Dense 权重布局: (in,out), 与 Keras 一致,
直接按 C 序展开即可。数值用 float32 最短往返表示, 保证精度无损。
"""
import numpy as np
from tensorflow import keras

MODEL = "model.keras"
OUT = "wake_model_weights.h"

# (层数组名, keras 层索引前缀) conv1..conv3, dense1..dense2
CONV_LAYERS = ["conv1", "conv2", "conv3"]
DENSE_LAYERS = ["dense1", "dense2"]


def fmt(arr):
    """float32 标量列表 -> 单行逗号分隔字符串 (最短往返表示)。"""
    return ",".join(str(np.float32(v)) for v in np.ravel(arr))


def main():
    model = keras.models.load_model(MODEL)
    convs = [l for l in model.layers if l.__class__.__name__ == "Conv2D"]
    denses = [l for l in model.layers if l.__class__.__name__ == "Dense"]
    assert len(convs) == len(CONV_LAYERS), "Conv2D 层数与预期不符"
    assert len(denses) == len(DENSE_LAYERS), "Dense 层数与预期不符"

    lines = [
        "#ifndef WAKE_MODEL_WEIGHTS_H_",
        "#define WAKE_MODEL_WEIGHTS_H_",
        "// 自动生成, 来自 model.keras 浮点权重 (重训)",
        "// Conv2D 权重布局: (kh,kw,in_ch,out_ch); Dense 权重布局: (in,out)",
    ]
    for name, layer in zip(CONV_LAYERS, convs):
        w, b = layer.get_weights()
        kh, kw, cin, cout = w.shape
        lines.append(f"#define {name.upper()}_KH {kh}")
        lines.append(f"#define {name.upper()}_KW {kw}")
        lines.append(f"#define {name.upper()}_IN {cin}")
        lines.append(f"#define {name.upper()}_OUT {cout}")
        lines.append(f"static const float {name}_w[{w.size}] = {{")
        lines.append(f"  {fmt(w)}")
        lines.append("};")
        lines.append(f"static const float {name}_b[{b.size}] = {{")
        lines.append(f"  {fmt(b)}")
        lines.append("};")
        lines.append("")
    for name, layer in zip(DENSE_LAYERS, denses):
        w, b = layer.get_weights()
        lines.append(f"static const float {name}_w[{w.size}] = {{")
        lines.append(f"  {fmt(w)}")
        lines.append("};")
        lines.append(f"static const float {name}_b[{b.size}] = {{")
        lines.append(f"  {fmt(b)}")
        lines.append("};")
        lines.append("")
    for name, layer in zip(DENSE_LAYERS, denses):
        lines.append(f"#define {name.upper()}_OUT {layer.units}")
    lines.append("#endif")

    with open(OUT, "w") as fp:
        fp.write("\n".join(lines) + "\n")
    n = sum(l.size for l in convs + denses for l in l.get_weights())
    print(f"{OUT} 已生成, 共 {n} 个 float 权重")


if __name__ == "__main__":
    main()
