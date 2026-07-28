/****************************************************************************
 * app/home_scense/doubao/doubao_voice.c
 * Single-turn push-to-talk session coordinator for Doubao RealtimeAPI.
 ****************************************************************************/

#include "doubao_voice.h"
#include "doubao_protocol.h"
#include "voice_capture.h"
#include "voice_player.h"
#include "voice_transport.h"

#include <nuttx/config.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

typedef struct doubao_voice_context_s
{
  pthread_mutex_t mutex;
  pthread_t worker;
  bool initialized;
  bool worker_active;
  bool worker_joinable;
  bool stop_requested;
  doubao_voice_snapshot_t snapshot;
  doubao_voice_config_t config;
} doubao_voice_context_t;

static doubao_voice_context_t g_voice;

static void copy_text(char *destination, size_t size, const char *source)
{
  if (!destination || size == 0)
    {
      return;
    }
  if (!source)
    {
      destination[0] = '\0';
      return;
    }
  strncpy(destination, source, size - 1);
  destination[size - 1] = '\0';
}

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

static bool stop_requested(void)
{
  bool requested;
  pthread_mutex_lock(&g_voice.mutex);
  requested = g_voice.stop_requested;
  pthread_mutex_unlock(&g_voice.mutex);
  return requested;
}

static void reap_completed_worker(void)
{
  pthread_t worker;
  bool join = false;

  pthread_mutex_lock(&g_voice.mutex);
  if (g_voice.worker_joinable && !g_voice.worker_active)
    {
      worker = g_voice.worker;
      g_voice.worker_joinable = false;
      join = true;
    }
  pthread_mutex_unlock(&g_voice.mutex);

  if (join)
    {
      (void)pthread_join(worker, NULL);
    }
}

static void append_reply(const char *text)
{
  size_t used;
  size_t available;

  if (!text)
    {
      return;
    }
  pthread_mutex_lock(&g_voice.mutex);
  used = strlen(g_voice.snapshot.assistant_text);
  available = sizeof(g_voice.snapshot.assistant_text) - used - 1;
  strncat(g_voice.snapshot.assistant_text, text, available);
  pthread_mutex_unlock(&g_voice.mutex);
}

static void make_identifier(char *output, size_t size, const char *prefix)
{
  static unsigned int next_id;
  snprintf(output, size, "%s-%08x", prefix, ++next_id);
}

static int send_json(voice_transport_t *transport, int event,
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
  return voice_transport_send(transport, VOICE_WS_BINARY, frame, frame_size);
}

static int send_audio(voice_transport_t *transport, const char *session_id,
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
  return voice_transport_send(transport, VOICE_WS_BINARY, frame, frame_size);
}

static void build_start_session_json(char *output, size_t output_size)
{
  snprintf(output, output_size,
           "{\"asr\":{\"extra\":{\"enable_asr_twopass\":true}},"
           "\"tts\":{\"speaker\":\"%s\",\"audio_config\":{"
           "\"channel\":1,\"format\":\"pcm_s16le\",\"sample_rate\":24000}},"
           "\"dialog\":{\"bot_name\":\"Vela\","
           "\"system_role\":\"你是一个简洁友好的桌面语音助手。\","
           "\"speaking_style\":\"简洁自然\",\"extra\":{"
           "\"input_mod\":\"push_to_talk\",\"model\":\"%s\"}}}",
           g_voice.config.speaker, g_voice.config.model);
}

static void log_service_error(const doubao_packet_t *packet,
                              const voice_transport_t *transport)
{
  char payload[257];
  size_t length;
  size_t i;

  length = packet->payload_size < sizeof(payload) - 1 ?
           packet->payload_size : sizeof(payload) - 1;
  for (i = 0; i < length; i++)
    {
      uint8_t value = packet->payload[i];
      payload[i] = value >= 0x20 && value <= 0x7e ? (char)value : '.';
    }
  payload[length] = '\0';
  fprintf(stderr,
          "doubao: service error event=%d code=%d logid=%s payload=%s\n",
          packet->event, packet->error_code, voice_transport_logid(transport),
          payload);
}

static void handle_packet(const doubao_packet_t *packet,
                          const voice_transport_t *transport,
                          voice_player_t **player)
{
  char text[DOUBAO_REPLY_MAX];

  if (packet->kind == DOUBAO_PACKET_ERROR)
    {
      log_service_error(packet, transport);
      set_state(DOUBAO_VOICE_ERROR, "豆包服务返回错误，请查看日志");
      return;
    }

  if (packet->kind == DOUBAO_PACKET_AUDIO)
    {
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
      return;
    }

  if (packet->kind != DOUBAO_PACKET_JSON)
    {
      return;
    }

  if (packet->event == DOUBAO_EVENT_ASR_RESPONSE)
    {
      if (doubao_protocol_get_text(packet->payload, packet->payload_size,
                                   "text", text, sizeof(text)))
        {
          pthread_mutex_lock(&g_voice.mutex);
          copy_text(g_voice.snapshot.user_text, sizeof(g_voice.snapshot.user_text),
                    text);
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
}

static void *voice_worker(void *arg)
{
  voice_transport_t *transport = NULL;
  voice_capture_t *capture = NULL;
  voice_player_t *player = NULL;
  voice_transport_config_t transport_config;
  uint8_t audio[DOUBAO_CAPTURE_PACKET_BYTES];
  uint8_t received[DOUBAO_WS_BUFFER_SIZE];
  voice_ws_opcode_t opcode;
  doubao_packet_t packet;
  char session_id[48];
  char connect_id[48];
  char start_session_json[768];
  bool tts_ended = false;
  bool chat_ended = false;
  int ret;

  (void)arg;
  make_identifier(session_id, sizeof(session_id), "vela-session");
  make_identifier(connect_id, sizeof(connect_id), "vela-connect");
  memset(&transport_config, 0, sizeof(transport_config));
  transport_config.url = DOUBAO_WS_URL;
  transport_config.app_id = g_voice.config.app_id;
  transport_config.access_token = g_voice.config.access_token;
  transport_config.resource_id = g_voice.config.resource_id;
  transport_config.app_key = g_voice.config.app_key;
  transport_config.connect_id = connect_id;

  set_state(DOUBAO_VOICE_CONNECTING, NULL);
  ret = voice_transport_connect(&transport, &transport_config,
                                DOUBAO_CONNECT_TIMEOUT_MS);
  if (ret < 0)
    {
      set_state(DOUBAO_VOICE_ERROR, "无法连接豆包服务");
      goto cleanup;
    }
  printf("doubao: connected, logid=%s\n", voice_transport_logid(transport));

  build_start_session_json(start_session_json, sizeof(start_session_json));

    if (send_json(transport, DOUBAO_EVENT_START_CONNECTION, session_id, "{}") < 0)
    { set_state(DOUBAO_VOICE_ERROR,"send fail"); goto cleanup; }
  
    if (send_json(transport, DOUBAO_EVENT_START_SESSION, session_id,
                start_session_json) < 0)
    { set_state(DOUBAO_VOICE_ERROR,"send fail"); goto cleanup; }
  
  /* Read back CONNECTION_STARTED + SESSION_STARTED events.  If we skip this,
   * the server's replies pile up in the TLS buffer and subsequent
   * mbedtls_ssl_write calls return WANT_READ, which the NuttX curl port
   * maps to CURLE_SEND_ERROR(55). */
  {
    uint8_t init_data[DOUBAO_WS_BUFFER_SIZE];
    voice_ws_opcode_t init_op;
    doubao_packet_t init_pkt;
    bool conn_ok = false, sess_ok = false;
    int init_tries = 0;

    while ((!conn_ok || !sess_ok) && init_tries < 50)
      {
        ret = voice_transport_receive(transport, &init_op, init_data,
                                      sizeof(init_data), 5000);
        if (ret <= 0 || init_op != VOICE_WS_BINARY)
          {
            if (ret > 0 && init_op != VOICE_WS_BINARY)
              {
                /* Log raw bytes of unexpected frame type */
                char hexbuf[256]; int hi;
                size_t n = ret < 30 ? (size_t)ret : 30;
                for (hi=0; hi<(int)n; hi++)
                  snprintf(hexbuf+hi*2, sizeof(hexbuf)-hi*2, "%02x", init_data[hi]);
                hexbuf[n*2] = '\0';
                              }
            usleep(50000); init_tries++; continue;
          }
        if (doubao_protocol_decode(init_data, (size_t)ret, &init_pkt) < 0)
          {
            /* Log raw bytes for decode failure */
            char hb[256]; int hi;
            size_t n = ret < 30 ? (size_t)ret : 30;
            for (hi=0; hi<(int)n; hi++)
              snprintf(hb+hi*2, sizeof(hb)-hi*2, "%02x", init_data[hi]);
            hb[n*2] = '\0';
                        usleep(50000); init_tries++; continue;
          }
        if (init_pkt.event == DOUBAO_EVENT_CONNECTION_STARTED) conn_ok = true;
        if (init_pkt.event == DOUBAO_EVENT_SESSION_STARTED ||
            init_pkt.event == 150) sess_ok = true;
                init_tries++;
      }
    if (!sess_ok)
      {
        set_state(DOUBAO_VOICE_ERROR, "豆包会话未就绪");
        goto cleanup;
      }
  }

  /* The service confirms connection/session asynchronously.  Capture opens
   * only after setup frames are sent, so UI never claims recording before the
   * device is owned by this session. */
  ret = voice_capture_open(&capture, g_voice.config.capture_device,
                           DOUBAO_CAPTURE_RATE, DOUBAO_CAPTURE_CHANNELS,
                           DOUBAO_CAPTURE_BITS);
  if (ret < 0)
    {
      set_state(DOUBAO_VOICE_ERROR, "麦克风不可用");
      goto cleanup;
    }
  set_state(DOUBAO_VOICE_RECORDING, NULL);

  {
    int pkt_count = 0;
    while (!stop_requested())
    {
      ret = voice_capture_read_packet(capture, audio, sizeof(audio));
      if (ret == -EAGAIN)
        { continue; }
      if (ret != sizeof(audio))
        { set_state(DOUBAO_VOICE_ERROR,"语音采集异常"); goto cleanup; }
      if (send_audio(transport, session_id, audio, sizeof(audio)) < 0)
        {           set_state(DOUBAO_VOICE_ERROR,"语音上传失败"); goto cleanup; }
      pkt_count++;
    }
      }

  voice_capture_close(capture); capture = NULL;
  set_state(DOUBAO_VOICE_WAITING_RESPONSE, NULL);

    if (send_json(transport, DOUBAO_EVENT_END_ASR, session_id, "{}") < 0)
    {       set_state(DOUBAO_VOICE_ERROR,"结束录音失败"); goto cleanup; }
  
  {
    int resp_count = 0;
    while (!(tts_ended && chat_ended))
    {
      ret = voice_transport_receive(transport, &opcode, received,
                                   sizeof(received), 60000);
      if (ret <= 0)
        {           set_state(DOUBAO_VOICE_ERROR,"等待豆包回复超时");
          goto cleanup;
        }
      if (opcode != VOICE_WS_BINARY ||
          doubao_protocol_decode(received, ret, &packet) < 0)
        {
          continue;
        }
      handle_packet(&packet, transport, &player);
        resp_count++;
      if (packet.kind == DOUBAO_PACKET_ERROR)
        { goto cleanup; }
      tts_ended |= packet.event == DOUBAO_EVENT_TTS_ENDED;
      chat_ended |= packet.event == DOUBAO_EVENT_CHAT_ENDED;
    }
      }

  if (player)
    {
      voice_player_close(player);
      player = NULL;
    }
  (void)send_json(transport, DOUBAO_EVENT_FINISH_SESSION, session_id, "{}");
  (void)send_json(transport, DOUBAO_EVENT_FINISH_CONNECTION, session_id, "{}");
  set_state(DOUBAO_VOICE_IDLE, NULL);

cleanup:
  if (capture)
    {
      voice_capture_close(capture);
    }
  if (player)
    {
      voice_player_close(player);
    }
  voice_transport_close(transport);
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.worker_active = false;
  pthread_mutex_unlock(&g_voice.mutex);
  return NULL;
}

int doubao_voice_init(void)
{
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
  g_voice.snapshot.state = DOUBAO_VOICE_IDLE;
#else
  g_voice.snapshot.state = DOUBAO_VOICE_UNCONFIGURED;
#endif
  return 0;
}

void doubao_voice_deinit(void)
{
  pthread_t worker;
  bool join = false;

  if (!g_voice.initialized)
    {
      return;
    }

  (void)doubao_voice_stop();
  pthread_mutex_lock(&g_voice.mutex);
  if (g_voice.worker_joinable)
    {
      worker = g_voice.worker;
      g_voice.worker_joinable = false;
      join = true;
    }
  pthread_mutex_unlock(&g_voice.mutex);

  if (join)
    {
      (void)pthread_join(worker, NULL);
    }
  pthread_mutex_destroy(&g_voice.mutex);
  memset(&g_voice, 0, sizeof(g_voice));
}

int doubao_voice_start(void)
{
  int ret;

  if (!g_voice.initialized || !doubao_voice_is_configured())
    {
      return -ENOKEY;
    }

  reap_completed_worker();
  pthread_mutex_lock(&g_voice.mutex);
  if (g_voice.worker_active)
    {
      pthread_mutex_unlock(&g_voice.mutex);
      return -EBUSY;
    }
  g_voice.stop_requested = false;
  g_voice.worker_active = true;
  g_voice.worker_joinable = true;
  g_voice.snapshot.user_text[0] = '\0';
  g_voice.snapshot.assistant_text[0] = '\0';
  g_voice.snapshot.error_text[0] = '\0';
  {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 65536);
    ret = pthread_create(&g_voice.worker, &attr, voice_worker, NULL);
    pthread_attr_destroy(&attr);
  }
  if (ret != 0)
    {
      g_voice.worker_active = false;
      g_voice.worker_joinable = false;
    }
  pthread_mutex_unlock(&g_voice.mutex);
  return ret ? -ret : 0;
}

int doubao_voice_stop(void)
{
  if (!g_voice.initialized)
    {
      return -EINVAL;
    }
  pthread_mutex_lock(&g_voice.mutex);
  g_voice.stop_requested = true;
  pthread_mutex_unlock(&g_voice.mutex);
  return 0;
}

int doubao_voice_reset(void)
{
  if (!g_voice.initialized)
    {
      return -EINVAL;
    }

  reap_completed_worker();
  pthread_mutex_lock(&g_voice.mutex);
  if (g_voice.worker_active)
    {
      pthread_mutex_unlock(&g_voice.mutex);
      return -EBUSY;
    }
  g_voice.stop_requested = false;
  g_voice.snapshot.user_text[0] = '\0';
  g_voice.snapshot.assistant_text[0] = '\0';
  g_voice.snapshot.error_text[0] = '\0';
  g_voice.snapshot.state = doubao_voice_is_configured() ?
                           DOUBAO_VOICE_IDLE : DOUBAO_VOICE_UNCONFIGURED;
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
