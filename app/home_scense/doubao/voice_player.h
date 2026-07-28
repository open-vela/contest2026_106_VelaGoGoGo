/****************************************************************************
 * app/home_scense/doubao/voice_player.h
 * Streaming PCM playback interface for TTS data.
 ****************************************************************************/

#ifndef HOME_SCENSE_VOICE_PLAYER_H
#define HOME_SCENSE_VOICE_PLAYER_H

#include <stddef.h>
#include <stdint.h>

typedef struct voice_player_s voice_player_t;

int voice_player_open(voice_player_t **player, const char *device,
                      uint32_t sample_rate, uint8_t channels,
                      uint8_t bits_per_sample);
int voice_player_write(voice_player_t *player, const uint8_t *data,
                       size_t size);
void voice_player_close(voice_player_t *player);

#endif /* HOME_SCENSE_VOICE_PLAYER_H */
