# TFLite Micro 唤醒词模型

> **当前流程以 [PIPELINE.md](PIPELINE.md) 为准**（run9/run10 复用手册）。
> 本文的模型结构 (8/16/32) 与判决阈值 (0.933/0.86) 是早期版本的记录，
> 现役模型为 run10 seed42 (16/32/64)，部署阈值 0.88 + 连续 4 窗。

3 类 (target / unknown / background) 的小型 CNN 唤醒词模型，导出为 int8 量化的
`.tflite` 与 TFLite Micro 可 `#include` 的 C 数组头文件。

## 目录产物
| 文件 | 说明 |
|---|---|
| `features.py` | log-mel 特征提取（训练/量化/MCU 端共用） |
| `prepare_data.py` | 扫描 `dataset/`（已随工程收纳），80/10/10 划分，缓存 `data.npz` |
| `train.py` | 训练 CNN（噪声混合 + 随机电平增强，多种子选优），保存 `model.keras`、`metrics.json`、`confusion_matrix.png` |
| `convert.py` | int8 量化导出 `model.tflite`、`model.cc/h`、`labels.txt`、`feature_config.json` |
| `export_weights_h.py` | 导出浮点权重 C 数组 `wake_model_weights.h`（手写 C 推理用） |
| `verify.py` | 对比 Keras 与 int8 tflite 在 test 集的一致性 |
| `eval_far.py` | 误唤醒率流式仿真 + 阈值扫描（`python3 eval_far.py . --sweep`） |
| `model.tflite` | int8 量化模型 |
| `model.cc` / `model.h` | TFLite Micro 用的 C 数组 |
| `feature_config.json` | 前端参数 + 输入/输出量化参数 + 推荐阈值 |
| `labels.txt` | 类别顺序（0=target, 1=unknown, 2=background） |

## 流程
```bash
pip3 install --user --break-system-packages tensorflow-cpu numpy scipy scikit-learn matplotlib
python3 prepare_data.py
python3 train.py
python3 convert.py
python3 export_weights_h.py   # 如需浮点 C 权重
```

## 特征前端（必须与 MCU 端一致）
- 采样率 16000，1s = 16000 样本
- 加 hamming 窗，frame_len=400 (25ms)，frame_step=320 (20ms)，n_fft=512
- 40 mel 滤波器 (0–8000 Hz)，取 `log10(power + 1e-10)`
- 输出 `(49, 40)` float，加 channel 维 → `(49, 40, 1)`

MCU 端得到 float mel 后，按 `feature_config.json` 中的 `input.scale/zero_point`
量化为 int8：`q = clip(round(f / scale) + zero_point, -128, 127)`。

## 模型
`Conv2D(8)+pool → Conv2D(16)+pool → Conv2D(32) → GAP → Dense(16) → Dense(3)`
softmax 输出 3 类概率。权重 int8，体积约十几 KB。

训练增强（`train.py`）：
- 随机 1s 裁剪 + 60% 概率按 −5~15dB SNR 混入背景噪声
- **随机电平归一**：每个训练窗口的 RMS 归一到 [−40, −12] dBFS 均匀分布。
  target 录音 RMS 集中在 −24.6dBFS（跨度仅 ~2.6dB），不加此增强模型会学到
  "够响 = target" 的捷径，导致噪声一大就误唤醒、声音一小就漏唤醒
- 多种子训练（TF CPU 非确定性大，单次训练可能抽到差解），在 val 上按
  0/−6dB 召回选优，阈值也在 val 上选（负样本 0/+6/+12dB 的 p99.5 与
  F1 最优取大），不接触 test

## 判决
取 target 类（index 0）概率，超过 `feature_config.json.target_threshold` 判为唤醒。

**强烈建议 MCU 端做滑窗平滑**：连续 2 个窗口（hop ≤250ms）超阈值才判唤醒。
流式仿真（`eval_far.py`，val+test 负样本 7.9 分钟 × 0/+6/+12dB）结果：

| 配置 | 误报/小时 | 唤醒率 0dB / −6dB / −12dB |
|---|---|---|
| 阈值 0.933，连续 2 窗平滑 | **0** | 92.5% / 92.0% / 76.5% |
| 阈值 0.86，连续 3 窗平滑 | **0** | 93.0% / 93.0% / 83.5% |

不实现平滑时误报约 0~38 次/小时（依音量），误报均为孤立单窗尖峰。

## 局限
- `target` 全部来自同一段录音会话，跨说话人泛化仍需更多数据验证
- 音量鲁棒范围约 ±12dB（更大动态范围可考虑在 MCU 特征前端加逐窗均值归一 CMVN，
  可获得精确的增益不变性，但需同步修改 `features.py` 与量化流程）
- 误唤醒仿真基于 Speech Commands 词表 + 背景噪声拼接流，未覆盖电视/音乐等
  真实长时场景，建议上线前用实际环境录音回放测试
