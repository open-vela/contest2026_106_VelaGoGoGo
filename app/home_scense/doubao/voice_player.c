/****************************************************************************
 * app/home_scense/doubao/voice_player.c
 * Streaming PCM playback backed by NuttX nxaudio.
 ****************************************************************************/

#include "voice_player.h"

#include <nuttx/audio/audio.h>
#include <audioutils/nxaudio.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

struct voice_player_s
{
  struct nxaudio_s audio;
  int read_fd;
  int write_fd;
  pthread_t thread;
  volatile int running;
  int audio_initialized;
};

static void player_dequeue(unsigned long arg, struct ap_buffer_s *buffer)
{
  voice_player_t *player = (voice_player_t *)(uintptr_t)arg;
  ssize_t received;

  if (!player || !buffer)
    {
      return;
    }

  received = read(player->read_fd, buffer->samp, buffer->nmaxbytes);
  if (received <= 0)
    {
      buffer->nbytes = 0;
      return;
    }

  buffer->nbytes = received;
  buffer->curbyte = 0;
  buffer->flags = 0;
  (void)nxaudio_enqbuffer(&player->audio, buffer);
}

static void player_complete(unsigned long arg)
{
  voice_player_t *player = (voice_player_t *)(uintptr_t)arg;
  if (player)
    {
      player->running = 0;
    }
}

static void player_user(unsigned long arg, struct audio_msg_s *message,
                        bool *running)
{
  (void)arg;
  (void)message;
  (void)running;
}

static void *player_thread(void *arg)
{
  voice_player_t *player = arg;
  struct nxaudio_callbacks_s callbacks =
  {
    player_dequeue,
    player_complete,
    player_user,
  };
  int i;

  for (i = 0; i < player->audio.abufnum; i++)
    {
      player_dequeue((unsigned long)(uintptr_t)player, player->audio.abufs[i]);
    }

  (void)nxaudio_start(&player->audio);
  (void)nxaudio_msgloop(&player->audio, &callbacks,
                        (unsigned long)(uintptr_t)player);
  player->running = 0;
  return NULL;
}

int voice_player_open(voice_player_t **out, const char *device,
                      uint32_t sample_rate, uint8_t channels,
                      uint8_t bits_per_sample)
{
  voice_player_t *player;
  int fds[2];
  int ret;

  if (!out || !device)
    {
      return -EINVAL;
    }

  player = calloc(1, sizeof(*player));
  if (!player)
    {
      return -ENOMEM;
    }
  player->read_fd = -1;
  player->write_fd = -1;

  if (pipe(fds) < 0)
    {
      free(player);
      return -errno;
    }
  player->read_fd = fds[0];
  player->write_fd = fds[1];

  ret = init_nxaudio_devname(&player->audio, sample_rate, bits_per_sample,
                             channels, device, "/doubao_player");
  if (ret < 0)
    {
      voice_player_close(player);
      return ret;
    }
  player->audio_initialized = 1;

  player->running = 1;
  ret = pthread_create(&player->thread, NULL, player_thread, player);
  if (ret != 0)
    {
      voice_player_close(player);
      return -ret;
    }

  *out = player;
  return 0;
}

int voice_player_write(voice_player_t *player, const uint8_t *data,
                       size_t size)
{
  size_t written = 0;

  if (!player || !data)
    {
      return -EINVAL;
    }

  while (written < size)
    {
      ssize_t ret = write(player->write_fd, data + written, size - written);
      if (ret <= 0)
        {
          return -errno;
        }
      written += ret;
    }

  return 0;
}

void voice_player_close(voice_player_t *player)
{
  if (!player)
    {
      return;
    }

  if (player->write_fd >= 0)
    {
      close(player->write_fd);
      player->write_fd = -1;
    }
  if (player->running)
    {
      (void)nxaudio_stop(&player->audio);
    }
  if (player->thread)
    {
      (void)pthread_join(player->thread, NULL);
    }
  if (player->read_fd >= 0)
    {
      close(player->read_fd);
    }
  if (player->audio_initialized)
    {
      fin_nxaudio(&player->audio);
    }
  free(player);
}
