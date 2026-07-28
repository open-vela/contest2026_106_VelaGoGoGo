/****************************************************************************
 * app/home_scense/doubao/voice_transport.h
 * TLS WebSocket transport abstraction used by the Doubao session layer.
 ****************************************************************************/

#ifndef HOME_SCENSE_VOICE_TRANSPORT_H
#define HOME_SCENSE_VOICE_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

typedef struct voice_transport_s voice_transport_t;

typedef struct voice_transport_config_s
{
  const char *url;
  const char *app_id;
  const char *access_token;
  const char *resource_id;
  const char *app_key;
  const char *connect_id;
} voice_transport_config_t;

typedef enum voice_ws_opcode_e
{
  VOICE_WS_TEXT = 1,
  VOICE_WS_BINARY = 2,
  VOICE_WS_CLOSE = 8,
  VOICE_WS_PING = 9,
  VOICE_WS_PONG = 10,
} voice_ws_opcode_t;

int voice_transport_connect(voice_transport_t **transport,
                            const voice_transport_config_t *config,
                            int timeout_ms);
int voice_transport_send(voice_transport_t *transport, voice_ws_opcode_t opcode,
                         const uint8_t *data, size_t size);
int voice_transport_receive(voice_transport_t *transport, voice_ws_opcode_t *opcode,
                            uint8_t *data, size_t capacity, int timeout_ms);
const char *voice_transport_logid(const voice_transport_t *transport);
void voice_transport_close(voice_transport_t *transport);

#endif /* HOME_SCENSE_VOICE_TRANSPORT_H */
