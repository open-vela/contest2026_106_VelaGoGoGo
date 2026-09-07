/****************************************************************************
 * app/home_scense/doubao/doubao_voice.c
 * Full-duplex continuous-conversation coordinator for Doubao RealtimeAPI.
 *
 * "开始会话" 后 session_loop 单线程内交替执行 [非阻塞采集上传] +
 * [非阻塞排空接收 ASR/Chat/TTS] + [播放],服务端 VAD 自动断句,多轮连续
 * 问答,直到 "停止会话"。连接常驻(worker 持有),待命期发静音帧保活,
 * 掉线自动重连。文字问答(ChatTextQuery=501)在待命态复用同一连接。
 *
 * 外放喇叭无硬件 AEC:AI 播放期间门控麦克风上传(改上传静音帧)以根治
 * 自问自答自循环;播完后再压制 POST_TTS_MUTE_MS 盖住残响窗口。
 *
 * 并发模型为单线程交替:transport 基于单个 curl 句柄,send/recv 不能并发;
 * 采集侧 ~20ms 节拍驱动,receive 用 timeout=0 非阻塞排空。
 *
 * NuttX adaptation — nxaudio 采集/播放,pthread worker,mutex 保护 snapshot。
 ****************************************************************************/

#include "doubao_voice.h"
#include "doubao_protocol.h"
#include "voice_capture.h"
#include "voice_player.h"
#include "voice_transport.h"
#include "../wifi_status.h"

/* 麦克风双向仲裁: 唤醒线程(wakeup)与本会话互斥使用 /dev/audio/pcm0c。
 * 本侧在开麦上升沿先等 wakeup_is_recording() 清零; wakeup 侧则在
 * talking=true 全程不开麦(见 wakeup.c assistant_busy)。 */
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP
#  include "../wakeup/wakeup.h"
#endif

#include <nuttx/config.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if __has_include("doubao_secret.h")
#  include "doubao_secret.h"
#  define DOUBAO_SECRETS_AVAILABLE 1
#else
#  define DOUBAO_SECRETS_AVAILABLE 0
#  define DOUBAO_APP_ID ""
#  define DOUBAO_ACCESS_TOKEN ""
#  define DOUBAO_MODEL ""
#  define DOUBAO_SPEAKER ""
#endif

#ifndef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_CAPTURE_DEV
#  define CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_CAPTURE_DEV "/dev/audio/pcm0c"
#endif
#ifndef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV
#  define CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV "/dev/audio/pcm0p"
#endif

#define DOUBAO_FRAME_CAPACITY (DOUBAO_WS_BUFFER_SIZE + 1024)

/* 诊断日志:DOUBAO_LOG 默认走 stderr(NuttX 下不进 dmesg/文件,难抓)。这里
 * 覆盖为写 /tmp/doubao.log(adb 可读)+ stderr,与 transport 的 DOUBAO_ERR
 * 一致,便于定位全双工时序。稳定后可移除本段。 */
#undef DOUBAO_LOG
#define DOUBAO_LOG(fmt, ...) \
  do { FILE *_f = fopen("/tmp/doubao.log", "a"); \
    if (_f) { fprintf(_f, "doubao: " fmt "\n", ##__VA_ARGS__); fclose(_f); } \
    fprintf(stderr, "doubao: " fmt "\n", ##__VA_ARGS__); } while (0)

/* session_loop 内部返回码 */
#define RC_STOP       0   /* 正常停止/服务错误,连接可正常收尾 */
#define RC_RECONNECT  (-1) /* 连接失效,需重连后继续 */

typedef struct doubao_voice_context_s
{
  pthread_mutex_t mutex;
  pthread_t worker;
  bool initialized;
  bool worker_running;
  bool shutdown;
  bool talking;          /* 全双工会话开关 */
  bool abort_playback;   /* 音乐抢占:立即中止 TTS 并关连接 */
  bool stop_playback;    /* 用户暂停:立即中止 TTS,保留连接 */
  bool query_pending;    /* 有待发送的文字 query */
  char pending_query[DOUBAO_TEXT_MAX];
  char dialog_id[DOUBAO_DIALOG_ID_MAX];
  doubao_voice_snapshot_t snapshot;
  doubao_voice_config_t config;
} doubao_voice_context_t;

static doubao_voice_context_t g_voice;

static uint64_t now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void copy_text(char *dst, size_t size, const char *src)
{
  if (!dst || size == 0)
    {
      return;
    }
  if (!src)
    {
      dst[0] = '\0';
      return;
    }
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

/*-----------------------------------------------------------------------
 * dialog_id 持久化:让服务端在新 session 加载最近上下文
 *---------------------------------------------------------------------*/
static bool dialog_id_is_valid(const char *id)
{
  size_t length;

  if (!id || !id[0])
    {
      return false;
    }
  length = strlen(id);
  if (length >= DOUBAO_DIALOG_ID_MAX)
    {
      return false;
    }
  for (size_t i = 0; i < length; i++)
    {
      unsigned char c = (unsigned char)id[i];
      if (!isalnum(c) && c != '_' && c != '-')
        {
          return false;
        }
    }
  return true;
}

static void make_dialog_id(char *out, size_t out_size)
{
  struct timespec ts;
  static unsigned int next_id;

  clock_gettime(CLOCK_REALTIME, &ts);
  snprintf(out, out_size, "vela-%lx-%lx-%x", (unsigned long)ts.tv_sec,
           (unsigned long)ts.tv_nsec, ++next_id);
}

static void load_dialog_id(void)
{
  char loaded[DOUBAO_DIALOG_ID_MAX];
  char tmp_path[sizeof(DOUBAO_DIALOG_ID_PATH) + 24];
  int fd;
  ssize_t rd;

  fd = open(DOUBAO_DIALOG_ID_PATH, O_RDONLY);
  if (fd >= 0)
    {
      rd = read(fd, loaded, sizeof(loaded) - 1);
      close(fd);
      if (rd > 0)
        {
          loaded[rd] = '\0';
          loaded[strcspn(loaded, "\r\n")] = '\0';
          if (dialog_id_is_valid(loaded))
            {
              copy_text(g_voice.dialog_id, sizeof(g_voice.dialog_id), loaded);
              DOUBAO_LOG("dialog context loaded");
              return;
            }
        }
    }

  make_dialog_id(g_voice.dialog_id, sizeof(g_voice.dialog_id));
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", DOUBAO_DIALOG_ID_PATH);
  fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd >= 0)
    {
      bool ok = write(fd, g_voice.dialog_id, strlen(g_voice.dialog_id)) >= 0 &&
                write(fd, "\n", 1) == 1;
      close(fd);
      if (ok && rename(tmp_path, DOUBAO_DIALOG_ID_PATH) == 0)
        {
          DOUBAO_LOG("dialog context created");
          return;
        }
      unlink(tmp_path);
    }
  DOUBAO_LOG("dialog id temporary; persistence unavailable");
}

/*-----------------------------------------------------------------------
 * snapshot / flag helpers
 *---------------------------------------------------------------------*/
static void set_state(doubao_voice_state_t state, const char *error)
{
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.snapshot.state = state;
  if (error)
    {
      copy_text(g_voice.snapshot.error_text,
                sizeof(g_voice.snapshot.error_text), error);
    }
  else if (state != DOUBAO_VOICE_ERROR)
    {
      g_voice.snapshot.error_text[0] = '\0';
    }
  pthread_mutex_unlock(&g_voice.mutex);
}

static bool is_talking(void)
{
  bool r;
  pthread_mutex_lock(&g_voice.mutex);
  r = g_voice.talking;
  pthread_mutex_unlock(&g_voice.mutex);
  return r;
}

static bool is_shutdown(void)
{
  bool r;
  pthread_mutex_lock(&g_voice.mutex);
  r = g_voice.shutdown;
  pthread_mutex_unlock(&g_voice.mutex);
  return r;
}

static bool take_abort_playback(void)
{
  bool r;
  pthread_mutex_lock(&g_voice.mutex);
  r = g_voice.abort_playback;
  g_voice.abort_playback = false;
  pthread_mutex_unlock(&g_voice.mutex);
  return r;
}

static bool take_stop_playback(void)
{
  bool r;
  pthread_mutex_lock(&g_voice.mutex);
  r = g_voice.stop_playback;
  g_voice.stop_playback = false;
  pthread_mutex_unlock(&g_voice.mutex);
  return r;
}

static void begin_turn_snapshot(void)
{
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.snapshot.turn_seq++;
  g_voice.snapshot.user_text[0] = '\0';
  g_voice.snapshot.assistant_text[0] = '\0';
  g_voice.snapshot.error_text[0] = '\0';
  pthread_mutex_unlock(&g_voice.mutex);
}

static void append_reply(const char *text)
{
  size_t used, avail;

  if (!text)
    {
      return;
    }
  pthread_mutex_lock(&g_voice.mutex);
  used = strlen(g_voice.snapshot.assistant_text);
  avail = sizeof(g_voice.snapshot.assistant_text) - used - 1;
  strncat(g_voice.snapshot.assistant_text, text, avail);
  pthread_mutex_unlock(&g_voice.mutex);
}

static void make_identifier(char *out, size_t size, const char *prefix)
{
  static unsigned int next_id;
  snprintf(out, size, "%s-%08x", prefix, ++next_id);
}

/*-----------------------------------------------------------------------
 * 帧发送 / JSON 构造 / VAD
 *---------------------------------------------------------------------*/
static int send_json(voice_transport_t *t, int event,
                     const char *session_id, const char *json)
{
  uint8_t frame[DOUBAO_FRAME_CAPACITY];
  size_t frame_size;
  int ret;

  ret = doubao_protocol_encode_json(event, session_id, json, frame,
                                    sizeof(frame), &frame_size);
  if (ret < 0)
    {
      return ret;
    }
  return voice_transport_send(t, VOICE_WS_BINARY, frame, frame_size);
}

static int send_audio(voice_transport_t *t, const char *session_id,
                      const uint8_t *audio, size_t audio_size)
{
  uint8_t frame[DOUBAO_CAPTURE_PACKET_BYTES + 256];
  size_t frame_size;
  int ret;

  ret = doubao_protocol_encode_audio(DOUBAO_EVENT_TASK_REQUEST, session_id,
                                     audio, audio_size, frame, sizeof(frame),
                                     &frame_size);
  if (ret < 0)
    {
      return ret;
    }
  return voice_transport_send(t, VOICE_WS_BINARY, frame, frame_size);
}

/* 把 src 转义为合法 JSON 字符串内容(不含外层引号)写入 dst。 */
static void json_escape(char *dst, size_t dst_size, const char *src)
{
  size_t o = 0;
  if (!dst || dst_size == 0)
    {
      return;
    }
  for (const char *p = src ? src : ""; *p && o + 2 < dst_size; p++)
    {
      unsigned char c = (unsigned char)*p;
      switch (c)
        {
          case '"':  dst[o++] = '\\'; dst[o++] = '"'; break;
          case '\\': dst[o++] = '\\'; dst[o++] = '\\'; break;
          case '\n': dst[o++] = '\\'; dst[o++] = 'n'; break;
          case '\r': dst[o++] = '\\'; dst[o++] = 'r'; break;
          case '\t': dst[o++] = '\\'; dst[o++] = 't'; break;
          default:
            if (c < 0x20)
              {
                if (o + 6 < dst_size)
                  {
                    static const char hex[] = "0123456789abcdef";
                    dst[o++] = '\\'; dst[o++] = 'u'; dst[o++] = '0'; dst[o++] = '0';
                    dst[o++] = hex[(c >> 4) & 0xf]; dst[o++] = hex[c & 0xf];
                  }
              }
            else
              {
                dst[o++] = (char)c;
              }
        }
    }
  dst[o] = '\0';
}

static int send_text_query(voice_transport_t *t, const char *session_id,
                           const char *query)
{
  char esc[DOUBAO_TEXT_MAX * 2 + 8];
  char json[DOUBAO_TEXT_MAX * 2 + 32];
  json_escape(esc, sizeof(esc), query);
  snprintf(json, sizeof(json), "{\"content\":\"%s\"}", esc);
  return send_json(t, DOUBAO_EVENT_CHAT_TEXT_QUERY, session_id, json);
}

static void build_start_session_json(char *out, size_t out_size)
{
  /* 全双工连续对话:dialog.extra 不带 input_mod(push_to_talk),由服务端
   * VAD 自动断句;asr.extra.end_smooth_window_ms 调静音断句阈值。 */
  snprintf(out, out_size,
           "{\"asr\":{\"extra\":{\"enable_asr_twopass\":true,"
           "\"end_smooth_window_ms\":%d}},"
           "\"tts\":{\"speaker\":\"%s\",\"audio_config\":{"
           "\"channel\":%d,\"format\":\"pcm_s16le\",\"sample_rate\":%d}},"
           "\"dialog\":{\"bot_name\":\"Vela\","
           "\"system_role\":\"你是一个简洁友好的桌面语音助手。\","
           "\"dialog_id\":\"%s\",\"speaking_style\":\"简洁自然\",\"extra\":{"
           "\"model\":\"%s\"}}}",
           DOUBAO_ASR_END_SMOOTH_WINDOW_MS, g_voice.config.speaker,
           DOUBAO_TTS_CHANNELS, DOUBAO_TTS_RATE,
           g_voice.dialog_id, g_voice.config.model);
}

/* 20ms 帧内 |sample| 均值,用于 barge-in VAD 与回声标定。 */
static unsigned frame_energy(const uint8_t *frame, size_t size)
{
  const int16_t *s = (const int16_t *)frame;
  size_t n = size / 2;
  uint64_t sum = 0;

  if (n == 0)
    {
      return 0;
    }
  for (size_t i = 0; i < n; i++)
    {
      int v = s[i];
      sum += (uint64_t)(v < 0 ? -v : v);
    }
  return (unsigned)(sum / n);
}

static bool energy_vad(const uint8_t *frame, size_t size, unsigned *run)
{
  if (frame_energy(frame, size) > DOUBAO_VAD_ON_THRESHOLD)
    {
      (*run)++;
    }
  else
    {
      *run = 0;
    }
  return *run >= DOUBAO_VAD_ON_FRAMES;
}

/*-----------------------------------------------------------------------
 * 服务端帧处理(全双工多轮)
 *---------------------------------------------------------------------*/
static void log_service_error(const doubao_packet_t *packet,
                              const voice_transport_t *transport)
{
  char payload[257];
  size_t length, i;

  length = packet->payload_size < sizeof(payload) - 1 ?
           packet->payload_size : sizeof(payload) - 1;
  for (i = 0; i < length; i++)
    {
      uint8_t v = packet->payload[i];
      payload[i] = v >= 0x20 && v <= 0x7e ? (char)v : '.';
    }
  payload[length] = '\0';
  fprintf(stderr, "doubao: service error event=%d code=%d logid=%s payload=%s\n",
          packet->event, packet->error_code,
          voice_transport_logid(transport), payload);
}

/* 处理一帧服务端数据。turn_open:本轮是否已开(用户话首开新轮);
 * chat_ended:文字回复完;tts_ended:359 本轮 TTS 全部发完;
 * last_audio_ms:最近 TTS 音频时刻;discard:挂断后丢弃残留 TTS。 */
static void handle_fd(const doubao_packet_t *packet,
                      const voice_transport_t *transport,
                      voice_player_t **player, bool *turn_open,
                      bool *chat_ended, uint64_t *last_audio_ms,
                      bool *discard, bool *tts_ended)
{
  char text[DOUBAO_REPLY_MAX];

  if (packet->kind == DOUBAO_PACKET_ERROR)
    {
      log_service_error(packet, transport);
      /* 会话中的 service error(如 DialogAudioIdleTimeout)可恢复,交由上层
       * 重连,不置 UI ERROR 避免闪屏。 */
      return;
    }

  if (packet->kind == DOUBAO_PACKET_AUDIO)
    {
      if (*discard)
        {
          return;
        }
      if (g_voice.config.tts_enabled && !*player)
        {
          if (voice_player_open(player, g_voice.config.playback_device,
                                DOUBAO_TTS_RATE, DOUBAO_TTS_CHANNELS,
                                DOUBAO_TTS_BITS) == 0)
            {
              set_state(DOUBAO_VOICE_PLAYING, NULL);
            }
        }
      if (*player)
        {
          (void)voice_player_write(*player, packet->payload,
                                   packet->payload_size);
        }
      *last_audio_ms = now_ms();
      return;
    }

  if (packet->kind != DOUBAO_PACKET_JSON)
    {
      return;
    }

  /* 用户话首 → 开新一轮 */
  if ((packet->event == DOUBAO_EVENT_ASR_START ||
       packet->event == DOUBAO_EVENT_ASR_RESPONSE) && !*turn_open)
    {
      begin_turn_snapshot();
      *turn_open = true;
      *chat_ended = false;
      if (tts_ended)
        {
          *tts_ended = false;
        }
      set_state(DOUBAO_VOICE_RECORDING, NULL);
    }

  if (packet->event == DOUBAO_EVENT_ASR_RESPONSE)
    {
      if (doubao_protocol_get_text(packet->payload, packet->payload_size,
                                   "text", text, sizeof(text)))
        {
          pthread_mutex_lock(&g_voice.mutex);
          copy_text(g_voice.snapshot.user_text,
                    sizeof(g_voice.snapshot.user_text), text);
          pthread_mutex_unlock(&g_voice.mutex);
        }
    }
  else if (packet->event == DOUBAO_EVENT_CHAT_RESPONSE)
    {
      if (doubao_protocol_get_text(packet->payload, packet->payload_size,
                                   "content", text, sizeof(text)) ||
          doubao_protocol_get_text(packet->payload, packet->payload_size,
                                   "text", text, sizeof(text)))
        {
          append_reply(text);
        }
    }
  else if (packet->event == DOUBAO_EVENT_CHAT_ENDED)
    {
      *chat_ended = true;
      *last_audio_ms = now_ms();
      *turn_open = false;
    }
  else if (packet->event == DOUBAO_EVENT_TTS_ENDED)
    {
      if (tts_ended)
        {
          *tts_ended = true;
        }
    }
}

/*-----------------------------------------------------------------------
 * 建立会话:连接 + START_CONNECTION + START_SESSION + 等待确认
 *---------------------------------------------------------------------*/
static int establish_session(voice_transport_t **out, char *session_id,
                             size_t session_id_size)
{
  voice_transport_t *transport = NULL;
  voice_transport_config_t tcfg;
  char connect_id[48];
  char start_session_json[1024];
  int ret = -EIO;

  make_identifier(session_id, session_id_size, "vela-session");
  make_identifier(connect_id, sizeof(connect_id), "vela-connect");
  memset(&tcfg, 0, sizeof(tcfg));
  tcfg.url = DOUBAO_WS_URL;
  tcfg.app_id = g_voice.config.app_id;
  tcfg.access_token = g_voice.config.access_token;
  tcfg.resource_id = g_voice.config.resource_id;
  tcfg.app_key = g_voice.config.app_key;
  tcfg.connect_id = connect_id;

  for (int attempt = 1; attempt <= 3 && !is_shutdown(); attempt++)
    {
      ret = voice_transport_connect(&transport, &tcfg, DOUBAO_CONNECT_TIMEOUT_MS);
      if (ret == 0)
        {
          break;
        }
      if (attempt < 3)
        {
          usleep(300 * 1000);
        }
    }
  if (ret < 0)
    {
      return ret;
    }
  DOUBAO_LOG("connected, logid=%s", voice_transport_logid(transport));

  build_start_session_json(start_session_json, sizeof(start_session_json));

  if (send_json(transport, DOUBAO_EVENT_START_CONNECTION, session_id, "{}") < 0 ||
      send_json(transport, DOUBAO_EVENT_START_SESSION, session_id,
                start_session_json) < 0)
    {
      voice_transport_close(transport);
      return -EIO;
    }

  /* 等待 CONNECTION_STARTED(50) + SESSION_STARTED(150)。64KB 接收缓冲堆分配,
   * 避免与 send_json 的大 frame 叠加顶高建连路径栈峰值。 */
  {
    uint8_t *buf = malloc(DOUBAO_WS_BUFFER_SIZE);
    voice_ws_opcode_t op;
    doubao_packet_t pkt;
    bool conn_ok = false, sess_ok = false;
    int tries = 0;

    if (!buf)
      {
        voice_transport_close(transport);
        return -ENOMEM;
      }
    while ((!conn_ok || !sess_ok) && tries < 50 && !is_shutdown())
      {
        ret = voice_transport_receive(transport, &op, buf,
                                      DOUBAO_WS_BUFFER_SIZE, 5000);
        if (ret <= 0 || op != VOICE_WS_BINARY)
          {
            usleep(50000);
            tries++;
            continue;
          }
        if (doubao_protocol_decode(buf, (size_t)ret, &pkt) < 0)
          {
            usleep(50000);
            tries++;
            continue;
          }
        if (pkt.event == DOUBAO_EVENT_CONNECTION_STARTED)
          {
            conn_ok = true;
          }
        if (pkt.event == DOUBAO_EVENT_SESSION_STARTED)
          {
            sess_ok = true;
          }
        tries++;
      }
    free(buf);
    if (!sess_ok)
      {
        voice_transport_close(transport);
        return -EIO;
      }
  }
  DOUBAO_LOG("session started");
  *out = transport;
  return 0;
}

/*-----------------------------------------------------------------------
 * 全双工会话主循环
 *---------------------------------------------------------------------*/
static int session_loop(voice_transport_t *transport, const char *session_id)
{
  voice_capture_t *capture = NULL;
  voice_player_t *player = NULL;
  uint8_t audio[DOUBAO_CAPTURE_PACKET_BYTES];
  const uint8_t silence[DOUBAO_CAPTURE_PACKET_BYTES] = {0};
  uint8_t received[DOUBAO_WS_BUFFER_SIZE];
  voice_ws_opcode_t opcode;
  doubao_packet_t packet;
  bool turn_open = false;
  bool chat_ended = false;
  bool tts_ended = false;
  uint64_t playback_end_ms = 0;
  bool discard_turn_audio = false;
  uint64_t last_audio_ms = 0;
  unsigned vad_run = 0;
  bool was_talking = false;
  int ret;

  set_state(is_talking() ? DOUBAO_VOICE_CONNECTING : DOUBAO_VOICE_IDLE, NULL);
  DOUBAO_LOG("full-duplex connection established, standing by");

  while (!is_shutdown())
    {
      bool talking = is_talking();

      /* 用户暂停:停止全双工,保留连接 */
      if (take_stop_playback())
        {
          if (player) { voice_player_abort(player); player = NULL; }
          if (capture) { voice_capture_close(capture); capture = NULL; }
          turn_open = false;
          chat_ended = false;
          discard_turn_audio = true;
          set_state(DOUBAO_VOICE_IDLE, NULL);
        }

      /* 音乐抢占:立即停播、丢弃残留 TTS、回 IDLE */
      if (take_abort_playback())
        {
          if (player) { voice_player_abort(player); player = NULL; }
          turn_open = false;
          chat_ended = false;
          discard_turn_audio = true;
          set_state(is_talking() ? DOUBAO_VOICE_LISTENING
                                 : DOUBAO_VOICE_IDLE, NULL);
        }

      /* 对话开始/停止边沿 */
      if (talking && !was_talking)
        {
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP
          /* 开麦前等唤醒线程释放麦克风(它每圈轮询 talking, 看到即停,
           * 正常交接 ~200ms)。超时兜底: 唤醒线程卡死时放弃等待强开,
           * 否则会话永远起不来。 */
          {
            uint64_t wait_start = now_ms();
            while (wakeup_is_recording() && !is_shutdown() &&
                   now_ms() - wait_start < DOUBAO_MIC_HANDOFF_TIMEOUT_MS)
              {
                usleep(20 * 1000);
              }
            if (wakeup_is_recording())
              {
                DOUBAO_LOG("mic handoff: wakeup still recording after %dms, "
                           "opening anyway", DOUBAO_MIC_HANDOFF_TIMEOUT_MS);
              }
            else if (now_ms() - wait_start >= 20)
              {
                DOUBAO_LOG("mic handoff: waited %llums for wakeup release",
                           (unsigned long long)(now_ms() - wait_start));
              }
          }
#endif
          ret = voice_capture_open(&capture, g_voice.config.capture_device,
                                   DOUBAO_CAPTURE_RATE, DOUBAO_CAPTURE_CHANNELS,
                                   DOUBAO_CAPTURE_BITS);
          if (ret < 0)
            {
              set_state(DOUBAO_VOICE_ERROR, "麦克风不可用");
              return RC_STOP;
            }
          discard_turn_audio = false;
          set_state(DOUBAO_VOICE_LISTENING, NULL);
          DOUBAO_LOG("talk START: capture opened, LISTENING");
        }
      else if (!talking && was_talking)
        {
          if (player) { voice_player_abort(player); player = NULL; }
          if (capture) { voice_capture_close(capture); capture = NULL; }
          turn_open = false;
          chat_ended = false;
          vad_run = 0;
          discard_turn_audio = true;
          set_state(DOUBAO_VOICE_IDLE, NULL);
        }
      was_talking = talking;

      /* 文字问答受理:仅待命(非对话)且无回复在播时取一条 pending query */
      if (!talking && !player)
        {
          char q[DOUBAO_TEXT_MAX];
          bool have = false;
          pthread_mutex_lock(&g_voice.mutex);
          if (g_voice.query_pending)
            {
              copy_text(q, sizeof(q), g_voice.pending_query);
              g_voice.query_pending = false;
              have = true;
            }
          pthread_mutex_unlock(&g_voice.mutex);
          if (have)
            {
              begin_turn_snapshot();
              discard_turn_audio = false;
              pthread_mutex_lock(&g_voice.mutex);
              copy_text(g_voice.snapshot.user_text,
                        sizeof(g_voice.snapshot.user_text), q);
              pthread_mutex_unlock(&g_voice.mutex);
              set_state(DOUBAO_VOICE_WAITING_RESPONSE, NULL);
              DOUBAO_LOG("text query: %s", q);
              if (send_text_query(transport, session_id, q) < 0)
                {
                  return RC_RECONNECT;
                }
            }
        }

      /* (A) 采集一包(非阻塞,~20ms 节拍源) */
      if (!capture)
        {
          if (send_audio(transport, session_id, silence, sizeof(silence)) < 0)
            {
              return RC_RECONNECT;
            }
          usleep(DOUBAO_CAPTURE_PACKET_MS * 1000);
          ret = -EAGAIN;
        }
      else
        {
          ret = voice_capture_read_packet(capture, audio, sizeof(audio));
        }
      if (ret == (int)sizeof(audio))
        {
          bool playing = (player != NULL);
          if (talking && playing && DOUBAO_BARGE_IN_ENABLED &&
              energy_vad(audio, sizeof(audio), &vad_run))
            {
              voice_player_abort(player);
              player = NULL;
              playing = false;
              chat_ended = false;
              vad_run = 0;
              set_state(DOUBAO_VOICE_LISTENING, NULL);
            }
          /* 上传源:待命/播放中/播完静音门内 → 静音帧;对话中且门已过 → 真音频 */
          bool post_tts_muted = playback_end_ms != 0 &&
              (now_ms() - playback_end_ms < DOUBAO_POST_TTS_MUTE_MS);
          const uint8_t *up = (talking && !playing && !post_tts_muted)
                              ? audio : silence;
          /* 诊断:统计真实音频上传帧数(每 50 帧≈1s 打一次)。 */
          if (up == audio)
            {
              static unsigned mic_frames;
              if (++mic_frames % 50 == 0)
                {
                  DOUBAO_LOG("uploading mic audio, frames=%u energy=%u",
                             mic_frames, frame_energy(audio, sizeof(audio)));
                }
            }
          if (send_audio(transport, session_id, up, sizeof(audio)) < 0)
            {
              if (player) voice_player_close(player);
              voice_capture_close(capture);
              return RC_RECONNECT;
            }
        }
      else if (ret == -EAGAIN)
        {
          /* 无新采集数据,继续排空接收 */
        }
      else
        {
          if (player) voice_player_close(player);
          voice_capture_close(capture);
          set_state(DOUBAO_VOICE_ERROR, "语音采集异常");
          return RC_STOP;
        }

      /* (B) 排空当前可读服务端帧(非阻塞) */
      for (;;)
        {
          /* AI 说话中被挂断:立即中止播放、丢弃残留 TTS 并跳出 */
          if (player && was_talking && !is_talking())
            {
              voice_player_abort(player);
              player = NULL;
              discard_turn_audio = true;
              break;
            }
          ret = voice_transport_receive(transport, &opcode, received,
                                        sizeof(received), 0);
          if (ret == 0)
            {
              break;
            }
          if (ret < 0)
            {
              if (player) voice_player_close(player);
              voice_capture_close(capture);
              return RC_RECONNECT;
            }
          if (opcode != VOICE_WS_BINARY)
            {
              continue;
            }
          if (doubao_protocol_decode(received, (size_t)ret, &packet) < 0)
            {
              continue;
            }
          if (packet.kind != DOUBAO_PACKET_AUDIO)
            {
              DOUBAO_LOG("rx event=%d kind=%d payload=%zu", packet.event,
                         packet.kind, packet.payload_size);
            }
          handle_fd(&packet, transport, &player, &turn_open, &chat_ended,
                    &last_audio_ms, &discard_turn_audio, &tts_ended);
          if (packet.kind == DOUBAO_PACKET_ERROR)
            {
              if (player) voice_player_close(player);
              voice_capture_close(capture);
              return RC_STOP;
            }
        }

      /* (C) 播放排空窗口收尾 */
      if (player &&
          (tts_ended ||
           (chat_ended && now_ms() - last_audio_ms > DOUBAO_DRAIN_TIMEOUT_MS)))
        {
          voice_player_close(player);
          player = NULL;
          chat_ended = false;
          tts_ended = false;
          playback_end_ms = now_ms();
          set_state(is_talking() ? DOUBAO_VOICE_LISTENING
                                 : DOUBAO_VOICE_IDLE, NULL);
        }

      /* 文字 query 兜底:回复结束却无 TTS,复位 IDLE 否则 ask_text 永久被拒 */
      if (chat_ended && !player && !is_talking() &&
          now_ms() - last_audio_ms > DOUBAO_DRAIN_TIMEOUT_MS)
        {
          chat_ended = false;
          tts_ended = false;
          turn_open = false;
          set_state(DOUBAO_VOICE_IDLE, NULL);
          DOUBAO_LOG("text-query turn done (no TTS), back to IDLE");
        }
    }

  if (player) voice_player_close(player);
  if (capture) voice_capture_close(capture);
  return RC_STOP;
}

/*-----------------------------------------------------------------------
 * 持久 worker
 *---------------------------------------------------------------------*/
static void *voice_worker(void *arg)
{
  voice_transport_t *transport = NULL;
  char session_id[48] = {0};
  bool connected = false;
  bool first_connect = true;

  (void)arg;

  while (!is_shutdown())
    {
      if (!connected)
        {
          if (first_connect)
            {
              set_state(DOUBAO_VOICE_CONNECTING, NULL);
            }
          while (!wifi_status_is_connected() && !is_shutdown())
            {
              usleep(500 * 1000);
            }
          if (is_shutdown())
            {
              break;
            }
          if (establish_session(&transport, session_id,
                                sizeof(session_id)) == 0)
            {
              connected = true;
              first_connect = false;
              set_state(DOUBAO_VOICE_IDLE, NULL);
            }
          else
            {
              if (is_shutdown())
                {
                  break;
                }
              if (first_connect)
                {
                  set_state(DOUBAO_VOICE_ERROR, "无法连接豆包服务");
                }
              usleep(1000 * 1000);
              continue;
            }
        }

      if (session_loop(transport, session_id) == RC_RECONNECT)
        {
          voice_transport_close(transport);
          transport = NULL;
          connected = false;
          continue;
        }
      /* RC_STOP:一般是致命采集/服务错误,关连接后重连 */
      voice_transport_close(transport);
      transport = NULL;
      connected = false;
    }

  if (transport)
    {
      (void)send_json(transport, DOUBAO_EVENT_FINISH_SESSION, session_id, "{}");
      (void)send_json(transport, DOUBAO_EVENT_FINISH_CONNECTION, session_id, "{}");
      voice_transport_close(transport);
    }

  pthread_mutex_lock(&g_voice.mutex);
  g_voice.worker_running = false;
  pthread_mutex_unlock(&g_voice.mutex);
  return NULL;
}

/*-----------------------------------------------------------------------
 * public API
 *---------------------------------------------------------------------*/
int doubao_voice_init(void)
{
  int ret;

  memset(&g_voice, 0, sizeof(g_voice));
  pthread_mutex_init(&g_voice.mutex, NULL);

  g_voice.initialized = true;
#if DOUBAO_SECRETS_AVAILABLE
  g_voice.config.app_id = DOUBAO_APP_ID;
  g_voice.config.access_token = DOUBAO_ACCESS_TOKEN;
  g_voice.config.resource_id = DOUBAO_RESOURCE_ID;
  g_voice.config.app_key = DOUBAO_APP_KEY;
  g_voice.config.model = DOUBAO_MODEL;
  g_voice.config.speaker = DOUBAO_SPEAKER;
  g_voice.config.capture_device = CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_CAPTURE_DEV;
  g_voice.config.playback_device = CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV;
  g_voice.config.tts_enabled = 1;
  g_voice.snapshot.state = DOUBAO_VOICE_CONNECTING;
#else
  g_voice.snapshot.state = DOUBAO_VOICE_UNCONFIGURED;
  return 0;
#endif

  if (!doubao_voice_is_configured())
    {
      g_voice.snapshot.state = DOUBAO_VOICE_UNCONFIGURED;
      return 0;
    }

  load_dialog_id();

  {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    /* 栈 8MB:session_loop 内 received[64KB] + send_json frame[66KB] + nxaudio/
     * curl+TLS 深调用叠加,断网重连路径峰值很高。Hexfight 实测需 8MB 防爆栈。 */
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    g_voice.worker_running = true;
    ret = pthread_create(&g_voice.worker, &attr, voice_worker, NULL);
    pthread_attr_destroy(&attr);
  }
  if (ret != 0)
    {
      g_voice.worker_running = false;
      set_state(DOUBAO_VOICE_ERROR, "worker 启动失败");
      return -ret;
    }
  return 0;
}

void doubao_voice_deinit(void)
{
  bool join = false;

  if (!g_voice.initialized)
    {
      return;
    }

  pthread_mutex_lock(&g_voice.mutex);
  g_voice.shutdown = true;
  g_voice.talking = false;
  join = g_voice.worker_running;
  pthread_mutex_unlock(&g_voice.mutex);

  if (join)
    {
      (void)pthread_join(g_voice.worker, NULL);
    }
  pthread_mutex_destroy(&g_voice.mutex);
  memset(&g_voice, 0, sizeof(g_voice));
}

int doubao_voice_start(void)
{
  if (!g_voice.initialized || !doubao_voice_is_configured())
    {
      return -ENOKEY;
    }
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.talking = true;
  pthread_mutex_unlock(&g_voice.mutex);
  return 0;
}

int doubao_voice_stop(void)
{
  if (!g_voice.initialized)
    {
      return -EINVAL;
    }
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.talking = false;
  g_voice.stop_playback = true;
  pthread_mutex_unlock(&g_voice.mutex);
  return 0;
}

int doubao_voice_abort_playback(void)
{
  if (!g_voice.initialized)
    {
      return -EINVAL;
    }
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.abort_playback = true;
  g_voice.query_pending = false;
  pthread_mutex_unlock(&g_voice.mutex);
  return 0;
}

int doubao_voice_ask_text(const char *text)
{
  if (!g_voice.initialized || !doubao_voice_is_configured())
    {
      return -ENOKEY;
    }
  if (!text || !text[0])
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_voice.mutex);
  /* 全双工中或上一条回复未播完(state≠IDLE)→ 拒绝,保证一次一条不并发 */
  if (g_voice.talking || g_voice.snapshot.state != DOUBAO_VOICE_IDLE)
    {
      pthread_mutex_unlock(&g_voice.mutex);
      return -EBUSY;
    }
  copy_text(g_voice.pending_query, sizeof(g_voice.pending_query), text);
  g_voice.query_pending = true;
  pthread_mutex_unlock(&g_voice.mutex);
  return 0;
}

/* 唤醒链路当前不接 KWS,统一 stub。接口保留供后续接入。 */
int doubao_voice_start_wake_turn(void)
{
  return -ENOSYS;
}

int doubao_voice_greet_then_wake(const char *text)
{
  (void)text;
  return -ENOSYS;
}

int doubao_voice_greet_then_talk(const char *text)
{
  (void)text;
  return -ENOSYS;
}

int doubao_voice_reset(void)
{
  if (!g_voice.initialized)
    {
      return -EINVAL;
    }
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.snapshot.user_text[0] = '\0';
  g_voice.snapshot.assistant_text[0] = '\0';
  g_voice.snapshot.error_text[0] = '\0';
  pthread_mutex_unlock(&g_voice.mutex);
  return 0;
}

bool doubao_voice_is_configured(void)
{
#if DOUBAO_SECRETS_AVAILABLE
  return g_voice.config.app_id && g_voice.config.app_id[0] &&
         g_voice.config.access_token && g_voice.config.access_token[0] &&
         strcmp(g_voice.config.app_id, "replace-with-your-app-id") != 0;
#else
  return false;
#endif
}

bool doubao_voice_is_talking(void)
{
  return is_talking();
}

void doubao_voice_get_snapshot(doubao_voice_snapshot_t *snapshot)
{
  if (!snapshot)
    {
      return;
    }
  pthread_mutex_lock(&g_voice.mutex);
  *snapshot = g_voice.snapshot;
  pthread_mutex_unlock(&g_voice.mutex);
}

size_t doubao_voice_get_history(doubao_history_turn_t *turns, size_t capacity,
                                unsigned *revision)
{
  /* 当前不迁移服务端历史加载(CONVERSATION_RETRIEVE);返回空。接口保留供
   * UI 兼容与后续接入。 */
  (void)turns;
  (void)capacity;
  if (revision)
    {
      pthread_mutex_lock(&g_voice.mutex);
      *revision = g_voice.snapshot.history_revision;
      pthread_mutex_unlock(&g_voice.mutex);
    }
  return 0;
}
