/****************************************************************************
 * app/home_scense/doubao/voice_capture.c
 * Fixed-size PCM capture packets for the R528 audio device.
 ****************************************************************************/

#include "voice_capture.h"

#include <nuttx/audio/audio.h>

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <time.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define CAPTURE_BUFFER_COUNT 2
#define CAPTURE_BUFFER_BYTES  8192

struct voice_capture_s
{
  int fd;
  mqd_t mq;
  char mq_name[32];
  struct ap_buffer_s **buffers;
  int buffer_count;
  struct ap_buffer_s *current;
  uint32_t offset;
};

static void free_buffers(voice_capture_t *capture)
{
  struct audio_buf_desc_s descriptor;
  int i;

  if (!capture || !capture->buffers)
    {
      return;
    }

  memset(&descriptor, 0, sizeof(descriptor));
  for (i = 0; i < capture->buffer_count; i++)
    {
      if (capture->buffers[i])
        {
          descriptor.u.buffer = capture->buffers[i];
          (void)ioctl(capture->fd, AUDIOIOC_FREEBUFFER,
                      (unsigned long)&descriptor);
        }
    }
  free(capture->buffers);
  capture->buffers = NULL;
}

int voice_capture_open(voice_capture_t **out, const char *device,
                       uint32_t sample_rate, uint8_t channels,
                       uint8_t bits_per_sample)
{
  voice_capture_t *capture;
  struct audio_caps_desc_s caps;
  struct audio_buf_desc_s descriptor;
  struct mq_attr attributes;
  int i;

  if (!out || !device)
    {
      return -EINVAL;
    }

  capture = calloc(1, sizeof(*capture));
  if (!capture)
    {
      return -ENOMEM;
    }
  capture->fd = -1;
  capture->mq = (mqd_t)-1;
  capture->buffer_count = CAPTURE_BUFFER_COUNT;

  capture->fd = open(device, O_RDWR | O_CLOEXEC);
  if (capture->fd < 0)
    {
      voice_capture_close(capture);
      return -errno;
    }
  if (ioctl(capture->fd, AUDIOIOC_RESERVE, 0) < 0)
    {
      voice_capture_close(capture);
      return -errno;
    }

  memset(&caps, 0, sizeof(caps));
  caps.caps.ac_len = sizeof(caps.caps);
  caps.caps.ac_type = AUDIO_TYPE_INPUT;
  caps.caps.ac_channels = channels;
  caps.caps.ac_controls.hw[0] = sample_rate;
  caps.caps.ac_controls.b[3] = sample_rate >> 16;
  caps.caps.ac_controls.b[2] = bits_per_sample;
  caps.caps.ac_subtype = AUDIO_FMT_PCM;
  if (ioctl(capture->fd, AUDIOIOC_CONFIGURE, (unsigned long)&caps) < 0)
    {
      voice_capture_close(capture);
      return -errno;
    }

  {
    struct ap_buffer_info_s bufinfo;
    int alloc_bytes;

    memset(&bufinfo, 0, sizeof(bufinfo));
    if (ioctl(capture->fd, AUDIOIOC_GETBUFFERINFO, (unsigned long)&bufinfo) < 0)
      {
        voice_capture_close(capture);
        return -errno;
      }
    alloc_bytes = (int)bufinfo.buffer_size;
    capture->buffer_count = bufinfo.nbuffers < CAPTURE_BUFFER_COUNT ?
                            bufinfo.nbuffers : CAPTURE_BUFFER_COUNT;
    capture->buffers = calloc(capture->buffer_count, sizeof(*capture->buffers));
    if (!capture->buffers)
      {
        voice_capture_close(capture);
        return -ENOMEM;
      }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.numbytes = alloc_bytes;
    for (i = 0; i < capture->buffer_count; i++)
      {
        descriptor.u.pbuffer = &capture->buffers[i];
        if (ioctl(capture->fd, AUDIOIOC_ALLOCBUFFER,
                  (unsigned long)&descriptor) != sizeof(descriptor))
          {
            voice_capture_close(capture);
            return -ENOMEM;
          }
      }
  }

  snprintf(capture->mq_name, sizeof(capture->mq_name), "/doubao%lx",
           (unsigned long)(uintptr_t)capture);
  memset(&attributes, 0, sizeof(attributes));
  attributes.mq_maxmsg = capture->buffer_count + 4;
  attributes.mq_msgsize = sizeof(struct audio_msg_s);
  capture->mq = mq_open(capture->mq_name, O_RDWR | O_CREAT, 0600, &attributes);
  if (capture->mq == (mqd_t)-1)
    {
      voice_capture_close(capture);
      return -errno;
    }
  if (ioctl(capture->fd, AUDIOIOC_REGISTERMQ, (unsigned long)capture->mq) < 0)
    {
      voice_capture_close(capture);
      return -errno;
    }

  for (i = 0; i < capture->buffer_count; i++)
    {
      capture->buffers[i]->nbytes = capture->buffers[i]->nmaxbytes;
      descriptor.numbytes = capture->buffers[i]->nbytes;
      descriptor.u.buffer = capture->buffers[i];
      if (ioctl(capture->fd, AUDIOIOC_ENQUEUEBUFFER,
                (unsigned long)&descriptor) < 0)
        {
          voice_capture_close(capture);
          return -errno;
        }
    }

  if (ioctl(capture->fd, AUDIOIOC_START, 0) < 0)
    {
      voice_capture_close(capture);
      return -errno;
    }

  *out = capture;
  return 0;
}

int voice_capture_read_packet(voice_capture_t *capture, uint8_t *packet,
                              size_t packet_size)
{
  struct audio_msg_s message;
  struct audio_buf_desc_s descriptor;
  unsigned int priority;
  size_t copied = 0;

  if (!capture || !packet || packet_size == 0)
    {
      return -EINVAL;
    }

  while (copied < packet_size)
    {
      if (!capture->current)
        {
          struct timespec timeout;

          if (clock_gettime(CLOCK_REALTIME, &timeout) < 0)
            {
              return -errno;
            }
          timeout.tv_nsec += 100000000;
          if (timeout.tv_nsec >= 1000000000)
            {
              timeout.tv_sec++;
              timeout.tv_nsec -= 1000000000;
            }
          if (mq_timedreceive(capture->mq, (char *)&message, sizeof(message),
                              &priority, &timeout) != sizeof(message))
            {
              return errno == ETIMEDOUT ? -EAGAIN :
                     (copied ? (int)copied : -errno);
            }
          if (message.msg_id != AUDIO_MSG_DEQUEUE)
            {
              continue;
            }
          capture->current = (struct ap_buffer_s *)message.u.ptr;
          capture->offset = 0;
        }

      {
        size_t available = capture->current->nbytes - capture->offset;
        size_t take = packet_size - copied < available ?
                      packet_size - copied : available;
        memcpy(packet + copied, capture->current->samp + capture->offset, take);
        copied += take;
        capture->offset += take;
      }

      if (capture->offset == capture->current->nbytes)
        {
          capture->current->nbytes = capture->current->nmaxbytes;
          descriptor.numbytes = capture->current->nbytes;
          descriptor.u.buffer = capture->current;
          (void)ioctl(capture->fd, AUDIOIOC_ENQUEUEBUFFER,
                      (unsigned long)&descriptor);
          capture->current = NULL;
          capture->offset = 0;
        }
    }

  return (int)copied;
}

void voice_capture_close(voice_capture_t *capture)
{
  if (!capture)
    {
      return;
    }

  if (capture->fd >= 0)
    {
      (void)ioctl(capture->fd, AUDIOIOC_STOP, 0);
      if (capture->mq != (mqd_t)-1)
        {
          (void)ioctl(capture->fd, AUDIOIOC_UNREGISTERMQ,
                      (unsigned long)capture->mq);
        }
      free_buffers(capture);
      (void)ioctl(capture->fd, AUDIOIOC_RELEASE, 0);
      close(capture->fd);
    }
  if (capture->mq != (mqd_t)-1)
    {
      mq_close(capture->mq);
      mq_unlink(capture->mq_name);
    }
  free(capture);
}
