/****************************************************************************
 * app/home_scense/doubao/voice_capture.h
 * Fixed-size PCM capture packets for a voice conversation session.
 ****************************************************************************/

#ifndef HOME_SCENSE_VOICE_CAPTURE_H
#define HOME_SCENSE_VOICE_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

typedef struct voice_capture_s voice_capture_t;

int voice_capture_open(voice_capture_t **capture, const char *device,
                       uint32_t sample_rate, uint8_t channels,
                       uint8_t bits_per_sample);
int voice_capture_read_packet(voice_capture_t *capture, uint8_t *packet,
                              size_t packet_size);
void voice_capture_close(voice_capture_t *capture);

#endif /* HOME_SCENSE_VOICE_CAPTURE_H */
