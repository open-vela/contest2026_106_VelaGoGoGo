/****************************************************************************
 * app/home_scense/doubao/doubao_config.h
 * Shared, non-sensitive configuration for the Doubao voice client.
 ****************************************************************************/

#ifndef HOME_SCENSE_DOUBAO_CONFIG_H
#define HOME_SCENSE_DOUBAO_CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* 生命周期日志开关。发布可置 0 静默;错误路径始终用 fprintf(stderr) 输出。 */
#ifndef DOUBAO_LOG_ENABLED
#  define DOUBAO_LOG_ENABLED 1
#endif

#if DOUBAO_LOG_ENABLED
#  define DOUBAO_LOG(fmt, ...) \
     fprintf(stderr, "doubao: " fmt "\n", ##__VA_ARGS__)
#else
#  define DOUBAO_LOG(fmt, ...) ((void)0)
#endif

#define DOUBAO_WS_URL                 "https://openspeech.bytedance.com/api/v3/realtime/dialogue"
#define DOUBAO_RESOURCE_ID            "volc.speech.dialog"
#define DOUBAO_APP_KEY                "PlgvMymc7f3tQnJ6"

#define DOUBAO_CAPTURE_RATE           16000
#define DOUBAO_CAPTURE_CHANNELS       1
#define DOUBAO_CAPTURE_BITS           16
#define DOUBAO_CAPTURE_PACKET_BYTES   640
#define DOUBAO_CAPTURE_PACKET_MS      20

#define DOUBAO_TTS_RATE               24000
#define DOUBAO_TTS_CHANNELS           1
#define DOUBAO_TTS_BITS               16

/* ---- 全双工连续会话参数 ----
 * 从 push_to_talk 半双工改为服务端 VAD 自动断句的全双工连续对话。 */

/* 服务端 ASR 端点静音断句窗口(ms)。豆包默认 1500,取值 [500,50000];
 * 调小 → 用户停顿更短即判定说完、回复更快,但过小易被句中停顿误切。 */
#define DOUBAO_ASR_END_SMOOTH_WINDOW_MS  800

/* barge-in(打断)总开关。外放喇叭无硬件 AEC,AI 播放期间麦克风一律不上传
 * (根治自问自答自循环);本开关=1 时额外用本地能量 VAD 检测"用户开口",
 * 命中即立即停播并恢复上传。真机回声导致假打断严重则置 0。 */
#define DOUBAO_BARGE_IN_ENABLED          0

/* 播放结束后的静音门延长期(ms)。外放无 AEC:TTS 播完瞬间门控立即恢复上传,
 * 但房间残响 + 硬件缓冲尾音仍在,会被首波上传灌回 → ASR 误识别 → 自问自答
 * 死循环。故 player 关闭后再压制上传本时长盖住残响窗口。 */
#define DOUBAO_POST_TTS_MUTE_MS          1500

/* 本地能量 VAD 判决阈值:20ms 帧内 |sample| 均值超过此值计一帧命中。
 * 需真机标定,设在"AI 外放回声峰值"之上、"正常人声"之下。 */
#define DOUBAO_VAD_ON_THRESHOLD          2500

/* 连续命中帧数迟滞:需连续 N 帧(≈N×20ms)超阈才判定用户真开口。8 帧 ≈ 160ms。 */
#define DOUBAO_VAD_ON_FRAMES             8

/* CHAT_ENDED 后继续等待滞后 TTS 音频的静默超时(ms)。 */
#define DOUBAO_DRAIN_TIMEOUT_MS          5000

/* 开麦前等待唤醒线程释放麦克风的上限(ms)。正常交接 ~200ms 内完成(唤醒
 * 线程每圈轮询 talking, 看到即停); 超时说明唤醒线程异常卡死, 放弃等待
 * 强开麦克风(退回无仲裁旧行为)以免会话永远起不来。 */
#define DOUBAO_MIC_HANDOFF_TIMEOUT_MS    3000

#define DOUBAO_TEXT_MAX               512
/* 单轮 AI 回复累加缓冲。中文 UTF-8 每字 3 字节,豆包长回复常达数百字,
 * 2048 只够约 680 字;放大到 8192(≈2700 字)覆盖正常长回复。 */
#define DOUBAO_REPLY_MAX              8192
#define DOUBAO_ERROR_MAX              256
/* TTS 音频消息会被 curl 分片,完整 envelope 可达十几 KB;缓冲需足够容纳
 * 一条重组后的完整帧,否则重组时溢出丢帧。worker 栈相应加大。 */
#define DOUBAO_WS_BUFFER_SIZE         65536
#define DOUBAO_CONNECT_TIMEOUT_MS     10000

/* 同一 dialog_id 让实时对话服务在新 session 中加载最近的服务端上下文。
 * 只保存无业务含义的随机标识,不保存 token 或聊天正文。 */
#define DOUBAO_DIALOG_ID_PATH          "/data/doubao_dialog_id"
#define DOUBAO_DIALOG_ID_MAX           64
#define DOUBAO_HISTORY_MAX_TURNS       20

typedef struct doubao_voice_config_s
{
  const char *app_id;
  const char *access_token;
  const char *resource_id;
  const char *app_key;
  const char *model;
  const char *speaker;
  const char *capture_device;
  const char *playback_device;
  int tts_enabled;
} doubao_voice_config_t;

#endif /* HOME_SCENSE_DOUBAO_CONFIG_H */
