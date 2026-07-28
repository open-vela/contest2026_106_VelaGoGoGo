/****************************************************************************
 * app/home_scense/doubao/doubao_config.h
 * Shared, non-sensitive configuration for the Doubao voice client.
 ****************************************************************************/

#ifndef HOME_SCENSE_DOUBAO_CONFIG_H
#define HOME_SCENSE_DOUBAO_CONFIG_H

#include <stddef.h>
#include <stdint.h>

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

#define DOUBAO_TEXT_MAX               512
#define DOUBAO_REPLY_MAX              2048
#define DOUBAO_ERROR_MAX              256
#define DOUBAO_WS_BUFFER_SIZE         8192
#define DOUBAO_CONNECT_TIMEOUT_MS     10000

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
