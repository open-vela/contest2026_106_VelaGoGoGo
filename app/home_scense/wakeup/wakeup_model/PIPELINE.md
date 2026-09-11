# 唤醒词模型训练与部署流程（run9 / run10 复用手册）

> 2026-09 整理。本文是 `train_run9.log` / `train_run10.log` 两次完整迭代的流程总结，
> 也是下一次重训的执行手册。`README.md` 是早期 tflite 路线的概述，其模型结构与
> 阈值章节已过时（那是 8/16/32 通道 + 阈值 0.933 的旧模型），以本文为准。

## 0. 当前部署快照（2026-09-05 定稿）

| 项 | 值 |
|---|---|
| 模型 | **run10 seed42**（`model_s42.keras`，已复制为 `model.keras`），Conv 16/32/64 + Dense32，~25k 参数，浮点推理 |
| 判决参数 | `WAKE_THR=0.880`，`WAKE_CONSEC=4`（连续 4 窗 ×100ms 超阈才唤醒），hop=100ms，`WAKE_MIN_DIST=12`（两次唤醒冷却 1.2s） |
| 部署点 1 | `/home/mi/sp_vela/apps/wake_demo/` — 独立 NSH 程序（文件/麦克风模式），带 host_test |
| 部署点 2 | `/home/mi/openvela/contest2026_106_VelaGoGoGo/app/home_scense/wakeup/` — 比赛工程集成模块（后台线程，唤醒后触发 `doubao_voice_start()`），`CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP` |
| 验收结论 | 误报流（val+test 负样本 0/+6/+12dB，neg_zh/neg_jul/neg_board/mixed 分目录）全 0；rec.pcm 两句真唤醒 0dB/-6dB 均 **2/2**；2.pcm 连续念唤醒词 **33/33**；TEST 误报=0 漏报=1（acc 0.9923） |

**硬原则（FA-first）**：误报 = 0 是硬约束，唤醒率可以牺牲。
板端 `rec.pcm` 必须恰好唤醒 2 次且位置落在两句真唤醒上；`1.pcm` / 7月29 / 7月28
必须恰好 0 次。任何参数或模型改动都要重新过这条验收。

## 1. 录音资产清单（谁是谁）

训练数据 2026-09-07 起全部收纳在本工程内：

```
wake_model/
├── dataset/      训练语料（原 /home/mi/doc/dataset）
└── recordings/   原始录音（原 /home/mi/doc 根目录散放的 pcm/wav + rec_segments/
                  target 源 + rec_segs/ 重建段 + rec28_16k.pcm 填充噪声源）
```

> **rec.pcm 是重建件（2026-09-11）**：原 39s 板端验收录音在 2026-09-09 被
> /home/mi/log 的新一轮拉取（6ch/48k/32bit 麦阵原始数据）覆盖，全盘确认无
> 副本。现文件由 `refill_rec_gaps.py` 从幸存的 10 个 VAD 段（`rec_segs/`，
> 含两句真唤醒原音）按原时间偏移重建，间隙用 7月28 板端纯噪声
> （`rec28_16k.pcm`，电平对齐段内环境噪声）回填。验收：check_margins 全网格
> 0.88–0.94 × k3–5 均 2 次落区间，−6dB 亦 2/2。**回填禁用 rec.wav**
> （44.1k 立体声，插值重采样产生混叠失真，真唤醒边界窗分数会塌）。
> /home/mi/log 的新拉取件是麦阵数据，**不要再覆盖本文件**。

上游语料留在原处未收纳：`/home/mi/doc/wake_speech/`（5.5GB Speech Commands，
`dataset/unknown` 的来源）；`/home/mi/doc/target/`（30 段旧唤醒词录音，与现
dataset 的 target 无 md5 重叠）。

| 文件 | 时长/内容 | 角色 |
|---|---|---|
| `recordings/rec.pcm` | 39s **重建件**（见上方注），6 段假语音 + **2 句真唤醒**（VAD 段起点 29.9s / 33.5s） | **验收硬地板**：必须恰 2 次命中真唤醒区间 (28–32s, 32–36.5s)，其余 0 |
| `recordings/rec_segs/` | 10 个 VAD 段（原 39s 录音的全部幸存内容，两句真唤醒在其中） | rec.pcm 重建源，只读勿动 |
| `recordings/rec28_16k.pcm` | 116s 7月28 板端纯噪声（16k 单声道，电平热 −6dBFS，轻微削顶） | rec.pcm 间隙回填源；真实板端信道噪声 |
| `recordings/2.pcm` | 118s 连续念唤醒词 ~33 次 | 唤醒率参考（不进训练、不进挖矿）：0.88/k4 下 33/33 |
| `recordings/1.pcm` | 247s 同嗓音中文负样本 | 最难负样本源：切段进 `neg_zh`、整条进 `neg_stream`、挖掘源；验收必须 0 唤醒 |
| `recordings/rec_16k_16bit_1ch.pcm` | 124s 7月29 同嗓音其他短语 | 同上 → `neg_jul` / `neg_stream`；旧模型曾打 0.75–0.93 |
| `recordings/rec.wav` | 116s 7月28 立体声纯噪声（与 rec28_16k 同环境 PC 侧采集，冷 14dB） | 切片进 `neg_stereo`（不训练，只作基准）；验收必须 0 唤醒；**勿作回填源** |
| `recordings/rec_segments/rec.pcm` | 186s，切出 31 段 target 源 | `dataset/target/` 的源头（1000 个文件 md5 去重后就是这 31 段） |
| `dataset/target_board/` | 32 段板端补录唤醒词 | 与 target 混信道训练（target 唯一段共 63：train 50 / val 6 / test 7） |

## 2. 复用总览（完整命令序列）

```bash
cd /home/mi/doc/wake_model
# 0) 依赖（一次性）
pip3 install --user --break-system-packages tensorflow-cpu numpy scipy scikit-learn matplotlib

# 1) 数据准备（数据有增删后必须重跑）
python3 prepare_data.py

# 2) 训练（6 种子 × ~200 epoch，产物 model_s*.keras / model.keras / metrics.json）
python3 train.py 2>&1 | tee train_runN.log

# 3) 种子选型（FA-first：硬门全过才算候选，不要只看 val score）
python3 select_params.py model_s42.keras     # 对 6 个种子逐一跑，比较输出
python3 select_params.py model_s7.keras      # （select_params.py 接受模型路径参数）

# 4) 若选型后误报仍不干净 → 难例挖掘，回第 1 步重训（见 §4.4 闭环）
python3 mine_hard_negs.py                    # 先改脚本里 MODEL= 为选型模型

# 5) 流式参数定型（THR × K 扫描，误报 0 优先）
python3 sweep_demo_params.py                 # 用 model.keras（= 选定种子）
python3 check_margins.py                     # 部署前边际预检

# 6) 导出部署权重
python3 export_weights_h.py                  # -> wake_model_weights.h（浮点 C 数组）
python3 convert.py                           # （可选）int8 tflite 路线，当前部署不用

# 7) 同步两个部署点 + 主机 C 验证 + 设备构建（见 §4.6–4.8）
```

## 3. 三端一致性"宪法"（绝不允许漂移）

训练 `features.py`、导出 `wake_model_weights.h`、设备端 `mel_features.c` + CNN 前向，
三处的数值必须逐 bit 对得上（主机 C 验证已证明 wake 时间戳与 Python 一致）。任何
一处改动都要重跑全链验收：

- **特征前端**：16kHz，1s=16000 样本窗，hamming，frame_len=400 (25ms)，
  frame_step=320 (20ms)，n_fft=512，40 mel (0–8kHz)，`log10(power+1e-10)` → `(49,40)`
- **模型结构**：`Conv2D(16,3)→pool2 → Conv2D(32,3)→pool2 → Conv2D(64,3) → GAP →
  Dense(32,relu) → Dense(3,softmax)`，取 **index 0（target 类）概率**做判决
- **判决语义**：1s 滑窗，hop 100ms，**裸概率**连续 `WAKE_CONSEC` 窗 > `WAKE_THR`
  才算一次唤醒（无平滑），冷却 `WAKE_MIN_DIST`=12 帧。C 端缓冲区尺寸全部由
  权重头的 `CONV*_OUT` / `DENSE1_OUT` 宏决定，**换层宽只需换头文件重编译**。

## 4. 分步详解

### 4.1 数据准备 `prepare_data.py` → `data.npz`

数据根 `dataset/`（已随工程收纳，所有脚本均相对 wake_model 目录引用），目录 → 用途（2026-09 现状）：

| 目录 | 文件数(唯一) | 类别 | train 重复 | 说明 |
|---|---|---|---|---|
| `target` + `target_board` | 1000(31) + 32 | 0 | ×8 | md5 去重后按**唯一段** 80/10/10 |
| `unknown` | 2000 | 1 | — | 英文 Speech Commands，分层 80/10/10 |
| `background` | 398 | 2 | — | 噪声，同时是训练时混噪源 |
| `neg_zh` | 60 | 1 | ×12 | 1.pcm VAD 切段 |
| `neg_jul` | 33 | 1 | ×12 | 7月29 切段（最难负样本） |
| `neg_board` | 8 | 1 | ×6 | 板端假语音（真唤醒 2 段**不在**此，留作验收） |
| `neg_hard` | 72 | 1 | ×10 | **train-only**，挖掘的高分负窗 |
| `neg_stream` | 2 | 1 | ×40 | **train-only**，整条负录音（zh_full 247s + jul_full 124s） |
| `neg_stereo` | 30 | — | 不训练 | 纯噪声，只作 `check_margins.py` 基准 |

关键规则（都有血的教训）：

1. **唯一段划分**：同一波形 md5 相同的文件必须去重后再 80/10/10，否则 val 指标是
   "背诵"不是泛化（曾给出 val 召回 90% 但板端新念法只有 0.3 分）。
2. **负样本划分种子独立**（`NEG_SPLIT_SEED=7`，与 target 的 SEED=42 分开），train
   路径重复加权、val/test 只放 1 份。
3. **train-only 目录**：`neg_hard` / `neg_stream` 只进 train，不污染 val/test 的
   独立性；`neg_stream` 用整条录音随机裁剪，覆盖 **VAD 切片永远产生不了的跨段窗**
   （run9 教训：1.pcm 133.1s 处跨段负簇连续 7 窗 0.999，只差一窗破防）。
4. run10 的 train 规模：target 400 / unknown 3324 / background 318（对比 run9
   3242，差值 +800 = neg_hard 72×10 + neg_stream 2×40）。

采集工具箱（按需）：
- `slice_pcm.py` — VAD 切 raw pcm 成 wav 段（静音判据 50ms 帧 RMS < -40dB）
- `scan_extra.py` — 新录音筛查：切段 + 现模型打分，找漏网唤醒词样本
- `channel_cluster.py` — 各来源 log-mel 谱距离，判断信道归属
- `diagnose_rec.py` — rec.pcm 逐段得分/电平/增益响应诊断

### 4.2 训练 `train.py`

- **增强**（都写在 `train.py` 顶部常量）：60% 概率按 SNR −5~15dB 混背景噪声；
  **随机电平归一** RMS→[−40,−12]dBFS（target 录音 RMS 集中在 −24.6dBFS 跨度仅
  2.6dB，不切断"够响=target"捷径就会噪声大误唤醒、声音小漏唤醒）；SpecAug 时/频
  屏蔽（各最多 8 帧/8 带）；0.9–1.1 变速。
- **裁剪锚定**（run7/8 的核心修正）：唤醒词短语 1.05–1.7s 装不进 1s 窗，
  - target 一律**末尾锚定**（判别性音段在尾部，部署滑窗必然扫过该对齐）；
  - 语音负样本（≤10s 段）**50% 也尾锚**——负样本只用随机裁剪时，段尾对齐窗是
    盲区，且模型会学"句尾能量衰减=target"捷径（流误报全在段边界）；
  - 长流文件（neg_stream）一律随机裁剪，覆盖全部对齐含跨段窗。
- **类权重**：`compute_class_weight("balanced")`，随数据分布自动变化。
- **早停**：monitor `val_loss`（不是 val_accuracy——val 里 target 仅 ~6/255，全
  reject 也有 97.7% accuracy，监控对漏 target 失明），patience=20。
- **多种子**：`TRAIN_SEEDS=(42, 7, 2026, 123, 999, 3407)` 全部保存
  `model_s<seed>.keras`。TF CPU 训练有线程级非确定性，单次训练可能抽到差解。
  train.py 自己按 val 召回 score 选一个 `model.keras`，但**这只是初选**——
  正式选型用 §4.3。
- **阈值初选** `choose_threshold()`：在 val 上取 `max(负样本 0/+6/+12dB 的 p99.5,
  F1 最优)`。这是模型自己的"部署阈值"，用于报指标；**最终设备阈值由 §4.5 流式
  扫参定**，两者不是一回事。

### 4.3 种子选型 `select_params.py`（FA-first，别用 val score 定生死）

run8 教训：seed 3407 负样本 max=0.013 但 val 分低而被丢弃；val 里 target 只有
6 个，score 噪声很大。所以选型必须用基准测试硬门：

**硬门（全部满足才算候选，任一失败即淘汰）：**
- 门 A 基准录音（demo 精确语义：1s 窗 / hop 1600 / 浮点模型）：
  - `rec.pcm` 恰好 2 次，且位置落在真唤醒区间 (28–32s) 和 (32–36.5s)
    （防"2 次但醒的是假语音段"）
  - `1.pcm` / 7月29 / 7月28 恰好 0 次
- 门 B 负样本流按目录（neg_zh / neg_jul / neg_board，裸拼接含跨段窗），
  0/+6/+12dB 全部 0 事件

**候选内排序**：target 唤醒率 0dB > −6dB > −12dB（val+test 唯一段，逐段计数），
并列看边际（真唤醒峰值 − thr 的最小值 vs 负峰 − thr）。

用法：`python3 select_params.py model_s<seed>.keras`，对 6 个种子逐一跑，
选硬门全过且边际最大的。run9 就是这么把 val-score 首位的 seed 7（TEST 误报=2）
换成 seed 3407（同嗓音负样本 max=0.080 全场最低）的。

### 4.4 难例挖掘闭环 `mine_hard_negs.py`（run9 → run10 的关键一步）

用**选型后的模型**扫完整负录音的所有 1s 对齐（hop 100ms），把高分窗
（`PROB_MIN=0.55`）存为 `../dataset/neg_hard/` 的 1s wav，加入训练后重训。

规则（脚本里写死，改前想清楚）：
- 只挖 `1.pcm` 与 7月29 录音；**rec.pcm 保持纯净不挖**（它是验收地板，挖了就
  自证清白了）；
- **排除与 val/test 负样本段时间区间重叠的窗**（按文件名 `seg_XXX_<起点>s.wav`
  反推区间），保持留出集诚实；
- 跑之前改 `MODEL =` 为当前选型模型；跑完 `prepare_data.py` → `train.py` 重训。

闭环示意：

```
train.py (run9) ──select_params──> seed3407 ──mine_hard_negs──> neg_hard/ (72 窗)
                                   │                                │
                                   └── 1.pcm/jul 整条 ──> neg_stream/ (2 条)
                                                                    ↓
                                              prepare_data.py + train.py (run10)
                                              → 全种子 FA=0，TEST 误报=0
```

### 4.5 流式参数定型（定 WAKE_THR / WAKE_CONSEC）

模型定了之后，设备参数在 **demo 精确参数**（1s 窗、hop 100ms、浮点模型、裸概率
consec 判决）下扫：

1. `sweep_demo_params.py`（用 `model.keras`）：THR 0.80–0.98 × K 2–6 网格；
   流包括 mixed（val+test 全负样本拼接）+ 按目录 neg_zh/neg_jul/neg_board，
   增益 0/+6/+12dB；target 唤醒率 0/−6/−12dB。**先筛误报全 0 的格点，再按唤醒率挑**。
2. `check_margins.py`：负样本流最大 prob 离候选阈值的边际（部署前预检）。
3. **rec.pcm 裸概率验收**：在选定 (THR, K) 下，rec.pcm 在 0dB 和 −6dB 增益都必须
   恰好 2 次命中真唤醒。这一步决定**阈值上限**——run10 seed42 的上限是 0.880，
   因为 rec.pcm −6dB 第二次唤醒的峰值只有 0.966，>0.88 就会漏。
4. 2.pcm 唤醒率参考（连续念 ~33 次，看能中多少）。

run10 seed42 定稿：**THR=0.880, K=4**（误报全 0；val/test 段唤醒率 84.6%
三增益一致；rec.pcm 2/2 双增益；2.pcm 33/33）。

### 4.6 导出 `export_weights_h.py` → `wake_model_weights.h`

- 读 `model.keras` 浮点权重，导出 C 数组头文件；Conv 布局 `(kh,kw,in,out)`，
  Dense `(in,out)`；头部带 `CONV*_KH/KW/IN/OUT`、`DENSE1_OUT` 宏，C 端缓冲区按
  宏定尺寸，**换层宽无需改 .c 代码**。
- `convert.py`（int8 tflite + `model.cc/h` + `feature_config.json`）是早期部署
  路线，当前两个部署点都不用它；保留作可选路径（`verify.py` 核对量化掉点）。

### 4.7 同步两个部署点（缺一不可）

| 部署点 | 要同步的文件 |
|---|---|
| `/home/mi/sp_vela/apps/wake_demo/` | `wake_model_weights.h`；`wake_demo.c` 顶部 `WAKE_THR/WAKE_CONSEC` 及调优注释 |
| `.../contest2026_106_VelaGoGoGo/app/home_scense/wakeup/` | 同上一套（`wakeup.c` + `mel_features.c` + `mic_capture.c` + 权重头） |

两处参数/权重不一致 = 主机验证通过但设备行为不同。改任何一边都要 diff 另一边。
wakeup 模块的麦克风仲裁：仅豆包 RECORDING 期间让出 `/dev/audio/pcm0c`，唤醒时
先 `mic_capture_stop` 再 `doubao_voice_start()`；`doubao_voice.c` 的
`voice_capture_open` 带 10×100ms 重试覆盖抢设备竞态。

### 4.8 主机 C 验证（推荐，防 C 移植数值漂移）

gcc 直接编译部署模块 + 桩，喂真实录音对拍 Python：

- 现成桩在 `/tmp/wakeup_host/`（wakeup 模块用）和
  `/home/mi/sp_vela/apps/wake_demo/host_test/`（demo 用）。
- **mic 桩必须跨 stop/start 保持文件读位置**（模拟连续流；每次 stop 都 fclose
  重开会把文件倒回去，造成无限唤醒循环），真 EOF 时 `exit(0)`。
- 模块本身没有 main，需要单独 harness.c。
- 验收标准：rec.pcm 恰 2 次（时间戳与 Python 窗起点 +1s 对应）、1.pcm 0 次、
  2.pcm 全中。

### 4.9 设备构建

- contest 工程：`cd /home/mi/openvela/contest2026_106_VelaGoGoGo && ./build.sh full`
  ——**新增 Kconfig 选项（如 `LVX_USE_DEMO_CONTEST2026_106_WAKEUP`）必须 full，
  增量编译不拾取**。烧录后串口看 `[wakeup]` 横幅；诊断日志 `adb pull /tmp/wakeup.log`。

## 5. run9 → run10 复盘

| | run9 | run10 |
|---|---|---|
| 数据 | train 3242（target 400 / unknown 2524 / bg 318） | train 4042（**+neg_hard 72×10 +neg_stream 2×40**） |
| class_weight | {0: 2.70, 1: 0.43, 2: 3.40} | {0: 3.37, 1: 0.41, 2: 4.24} |
| 6 种子 val 指标 | 召回 0dB 0.667 / −6dB 0.833，误报率@阈值 0% | 完全相同（数据增强不伤召回） |
| train.py 初选 | seed 7（TEST 误报=**2**） | seed 42（TEST 误报=**0**） |
| FA-first 选型 | 改选 **seed 3407**（同嗓音负样本 max=0.080 最低） | 全 6 种子负样本 max ≤ 0.223，无需纠结 |
| TEST | acc 0.9846，误报=2 漏报=1 | acc 0.9923，**误报=0** 漏报=1，target P=1.0 R=0.857 |
| 遗留问题 | 1.pcm 133.1s 跨段负簇 L=7@0.999（VAD 切片的盲区） | 无已知误报；阈值上限 0.88（rec.pcm −6dB 第二峰 0.966） |

结论：run9 的架构、增强、选型方法全部保留；run10 唯一的动作是**把模型的误报
挖出来喂回去**（neg_hard + neg_stream），就把 TEST 误报 2 → 0、同嗓音负样本
max 从 0.525 压到 0.223。下次模型误报不干净时，重复 §4.4 闭环即可。

## 6. run2 → run10 演进速查（教训都在这）

| run | 改动 | 结果 / 教训 |
|---|---|---|
| 2–3 | 基础流程，int8 路线 | run3 误报=0 但漏报=9（阈值 0.90 太紧） |
| 4 | 加入同嗓音负样本，数据大改 | 误报=1 漏报=1 |
| 5 | 重增强 | 早停 patience=6 在拟合完成前停住（target 欠拟合）→ 改 patience=20 + 盯 val_loss |
| 5–6 | 随机裁剪 | 真唤醒只撑 3 窗、负样本反撑 6 窗——随机裁剪教出"任意 60% 短语=target"的模糊边界 |
| 7 | target 改**末尾锚定** | 边界清晰化（阈值从虚高 0.80 回落到 0.57，指标更真实） |
| 8 | 语音负样本 50% 尾锚 | 段尾对齐窗不再是盲区；但 val-score 选型丢掉了负样本 max=0.013 的 seed 3407 |
| 9 | 6 种子全存 + **select_params.py FA-first 选型** | 选出 seed 3407；TEST 误报=2 → 挖出 neg_hard/neg_stream |
| 10 | **难例闭环**：neg_hard + neg_stream 进训练 | 全种子 FA=0，TEST 误报=0；seed42 以 0.88/k4 部署 |

## 7. 重训触发条件与验收清单

什么时候重训：换了唤醒词 / target 或负样本数据有增删 / 误报验收失败 /
想要更低阈值更高唤醒率。

重训后必须全过的验收（按顺序，任何一步失败回到对应步骤）：

- [ ] `train.py`：6 种子误报率@部署阈值 = 0%（val 级门）
- [ ] `select_params.py`：选定种子硬门 A+B 全过
- [ ] `sweep_demo_params.py`：选定 (THR, K) 下误报流全 0
- [ ] rec.pcm：0dB 与 −6dB 各恰 2 次，落真唤醒区间；1.pcm / 7月29 / 7月28 恰 0 次
- [ ] 2.pcm 唤醒率记录在案（参考值，不设硬线）
- [ ] `export_weights_h.py` 重新导出，**两个部署点都同步**（权重 + THR/K + 注释）
- [ ] 主机 C 验证 rec.pcm 2 次（防移植漂移）
- [ ] 设备 `./build.sh full` + 烧录 + 串口横幅 + 真人念唤醒词触发豆包
