/****************************************************************************
 * app/home_scense/doubao/doubao_voice.h
 * Public session API for the Doubao push-to-talk feature.
 ****************************************************************************/

#ifndef HOME_SCENSE_DOUBAO_VOICE_H
#define HOME_SCENSE_DOUBAO_VOICE_H

#include "doubao_config.h"

#include <stdbool.h>

typedef enum doubao_voice_state_e
{
  DOUBAO_VOICE_UNCONFIGURED,
  DOUBAO_VOICE_IDLE,
  DOUBAO_VOICE_CONNECTING,
  DOUBAO_VOICE_RECORDING,
  DOUBAO_VOICE_WAITING_RESPONSE,
  DOUBAO_VOICE_PLAYING,
  DOUBAO_VOICE_ERROR,
} doubao_voice_state_t;

typedef struct doubao_voice_snapshot_s
{
  doubao_voice_state_t state;
  char user_text[DOUBAO_TEXT_MAX];
  char assistant_text[DOUBAO_REPLY_MAX];
  char error_text[DOUBAO_ERROR_MAX];
} doubao_voice_snapshot_t;

int doubao_voice_init(void);
void doubao_voice_deinit(void);
int doubao_voice_start(void);
int doubao_voice_stop(void);
int doubao_voice_reset(void);
bool doubao_voice_is_configured(void);
void doubao_voice_get_snapshot(doubao_voice_snapshot_t *snapshot);

#endif /* HOME_SCENSE_DOUBAO_VOICE_H */
