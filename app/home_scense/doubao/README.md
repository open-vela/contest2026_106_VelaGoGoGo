# 豆包实时语音模块

本目录实现比赛项目所需的单轮按键说话（PTT）语音闭环：`DMIC PCM → 豆包 RealtimeAPI → 文本/TTS PCM`。

## 模块边界

- `doubao_protocol.*`：豆包二进制消息封包/解析；不依赖音频或 UI。
- `voice_transport.*`：基于 libcurl 的 TLS WebSocket 连接与帧收发；不理解豆包事件。
- `voice_capture.*`：从 `/dev/audio/pcm0c` 采集固定 640 B（20 ms）PCM 包。
- `voice_player.*`：将 24 kHz s16le PCM 流送到 `/dev/audio/pcm0p`。
- `doubao_voice.*`：唯一的会话协调层，向 LVGL 发布线程安全快照。

## 团队共享凭证

比赛期间团队共享同一凭证，已直接纳入 `doubao_secret.h` 提交到代码仓。外部使用请参考 `doubao_secret.h.example` 配置自己的凭证。

## 真机前置检查

```bash
./scripts/wifi.sh status
adb shell "date"
adb shell "ls -l /dev/pcm*"
adb shell "ping -c 1 openspeech.bytedance.com"
```

期望至少有 `/dev/audio/pcm0c` 和 `/dev/audio/pcm0p`。TLS 连接依赖正确的系统时间和系统 CA 证书路径。

## 当前 MVP 限制

- 一次只允许一个会话；再次点击结束录音，等待本轮回复完成。
- 不包含唤醒词、AEC、打断、自动重连和多轮持久化。
- 当前播放器先请求 24 kHz PCM；如真机不支持，需要增加 24 kHz → 48 kHz 的 2x 样本复制兼容路径。
- RealtimeAPI 协议实现基于官方事件约定与公开参考客户端；首次有效凭证联调时请保留串口中的 `X-Tt-Logid`、服务 event/code 和已截断的错误 payload，以核对控制台实际开放的模型、音色和请求字段。日志不会输出凭证。

## 构建开关

`LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE` 控制整个客户端和 AI 入口。关闭后，Doubao 源文件、初始化、轮询和 UI 入口都不会参与构建；应用其余功能仍可运行。
