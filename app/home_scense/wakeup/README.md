# wakeup(免提唤醒词触发)

从 `apps/wake_demo` 移植的纯 C 唤醒词检测模块(不依赖 tflite-micro),
作为 home_scense 应用的子模块常驻后台:豆包空闲时持有麦克风流式检测
唤醒词 **stone112**,检测到后释放麦克风并调用 `doubao_voice_start()`,
等效于点击"点击说话"按钮,实现免提唤醒 → 对话。

## 文件
| 文件 | 作用 |
|---|---|
| `wakeup.c/.h` | 后台线程:麦克风仲裁 + 1s 滑窗(100ms 步长)+ CNN 前向 + 连续窗判决 + 触发豆包 |
| `mel_features.c/.h` | log-mel 前端(hamming + 512 FFT + 40 mel + log10),对齐训练端 `features.py` |
| `mic_capture.c/.h` | NuttX audio ioctl 流式采集(移植自 nxrecorder),16k/mono/16bit |
| `wake_model_weights.h` | 由 `model.keras` 导出的浮点权重(seed42, 16/32/64 通道, 2026-09-05) |

## 麦克风仲裁
豆包每轮录音才打开 `/dev/audio/pcm0c`(参考 `doubao/doubao_voice.c` 的
`run_turn`),所以唤醒线程在豆包 CONNECTING/RECORDING/WAITING/PLAYING
期间 `mic_capture_stop` 让出设备、每 100ms 轮询状态,回到空闲后重新
`mic_capture_start`(滑窗清零重新积累)。反向竞态(用户点按钮瞬间唤醒
线程还持麦)由 `doubao_voice.c` 的 open 重试(10×100ms)兜底。

## 判决参数(与 sp_vela/apps/wake_demo 完全一致)
`WAKE_THR=0.88` + 连续 4 窗(400ms 持续)+ 最小间隔 1.2s。调优依据
见 `wakeup.c` 顶部注释;**改阈值必须重跑**训练工程
`/home/mi/doc/wake_model` 的 `sweep_demo_params.py` 与
`host_test/validate.py`。约束(2026-09-05):困难负样本流误报全 0、
rec.pcm 两次真唤醒 0dB/-6dB 均 2/2 命中。

## 构建
defconfig 开启(Kconfig 默认 y,依赖豆包语音):
```
CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP=y
```
`main.c` 在 UI 创建后调用 `wakeup_init()`,退出时先 `wakeup_deinit()`
再 `doubao_voice_deinit()`(唤醒线程要调豆包 API,必须先 join)。

## 诊断
事件与仲裁过程写 `/tmp/wakeup.log`(adb 可读),唤醒事件同时走 syslog。
