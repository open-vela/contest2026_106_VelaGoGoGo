/* mic_capture 实现: 移植自 nxrecorder 的 recordthread (audio ioctl 流式采集)。 */
#include <nuttx/config.h>
#include <nuttx/audio/audio.h>

#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <mqueue.h>

#include "mic_capture.h"

#define MIC_RING_SAMPLES 48000   /* 3s @16k, 足够缓冲 */

static void ring_push(struct mic_capture_s *c, const int16_t *data, int n)
{
  pthread_mutex_lock(&c->lock);
  for (int i = 0; i < n; ++i)
    {
      c->ring[c->wr] = data[i];
      c->wr = (c->wr + 1) % c->ring_size;
      if (c->count < c->ring_size)
        c->count++;
      else
        c->rd = (c->rd + 1) % c->ring_size;  /* 覆盖最旧 */
    }
  pthread_cond_signal(&c->cond);
  pthread_mutex_unlock(&c->lock);
}

static int mic_enqueue(struct mic_capture_s *c, FAR struct ap_buffer_s *apb)
{
  struct audio_buf_desc_s buf_desc;
#ifdef CONFIG_AUDIO_MULTI_SESSION
  buf_desc.session = NULL;
#endif
  apb->nbytes  = apb->nmaxbytes;
  apb->flags   = 0;
  apb->curbyte = 0;
  buf_desc.numbytes  = apb->nbytes;
  buf_desc.u.buffer  = apb;
  return ioctl(c->dev_fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&buf_desc);
}

static FAR void *mic_thread(pthread_addr_t pvarg)
{
  FAR struct mic_capture_s *c = (FAR struct mic_capture_s *)pvarg;
  struct audio_msg_s msg;
  unsigned int prio;

  while (c->running)
    {
      ssize_t size = mq_receive(c->mq, (FAR char *)&msg, sizeof(msg), &prio);
      if (size != sizeof(msg))
        continue;

      switch (msg.msg_id)
        {
          case AUDIO_MSG_DEQUEUE:
            {
              FAR struct ap_buffer_s *apb =
                (FAR struct ap_buffer_s *)msg.u.ptr;
              if (apb && apb->nbytes > 0)
                {
                  int nsamp = apb->nbytes / 2;
                  ring_push(c, (const int16_t *)apb->samp, nsamp);
                }
              mic_enqueue(c, apb);
            }
            break;

          case AUDIO_MSG_STOP:
          case AUDIO_MSG_COMPLETE:
            c->running = 0;
            pthread_cond_signal(&c->cond);
            goto done;

          default:
            break;
        }
    }
done:
  return NULL;
}

int mic_capture_start(struct mic_capture_s *c, const char *device,
                      int samplerate, int channels, int bps)
{
  struct audio_caps_desc_s cap_desc;
  struct ap_buffer_info_s  buf_info;
  struct mq_attr           attr;
  struct sched_param       sparam;
  pthread_attr_t           tattr;
  FAR struct ap_buffer_s  *pbuffers[16];
  int                      nbuffers;
  int                      ret;
  int                      x;

  memset(c, 0, sizeof(*c));
  c->dev_fd = -1;
  c->mq = (mqd_t)-1;
  c->ring_size = MIC_RING_SAMPLES;
  c->ring = malloc(c->ring_size * sizeof(int16_t));
  if (c->ring == NULL)
    return -ENOMEM;
  pthread_mutex_init(&c->lock, NULL);
  pthread_cond_init(&c->cond, NULL);

  c->dev_fd = open(device, O_RDWR | O_CLOEXEC);
  if (c->dev_fd < 0)
    {
      syslog(LOG_INFO, "打开 %s 失败: %d\n", device, -errno);
      return -errno;
    }

#ifdef CONFIG_AUDIO_MULTI_SESSION
  cap_desc.session = NULL;
#endif
  memset(&cap_desc, 0, sizeof(cap_desc));
  cap_desc.caps.ac_type    = AUDIO_TYPE_INPUT;
  cap_desc.caps.ac_channels = channels;
  cap_desc.caps.ac_chmap   = 0;
  cap_desc.caps.ac_controls.hw[0] = samplerate;
  cap_desc.caps.ac_controls.b[3]  = samplerate >> 16;
  cap_desc.caps.ac_controls.b[2]  = bps;
  cap_desc.caps.ac_subtype = AUDIO_FMT_PCM;
  ret = ioctl(c->dev_fd, AUDIOIOC_CONFIGURE, (uintptr_t)&cap_desc);
  if (ret < 0)
    {
      syslog(LOG_INFO, "AUDIOIOC_CONFIGURE 失败: %d\n", -errno);
      return -errno;
    }

  if (ioctl(c->dev_fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&buf_info) != OK)
    {
      buf_info.buffer_size = CONFIG_AUDIO_BUFFER_NUMBYTES;
      buf_info.nbuffers    = CONFIG_AUDIO_NUM_BUFFERS;
    }
  nbuffers = buf_info.nbuffers;
  if (nbuffers > 16) nbuffers = 16;

  attr.mq_maxmsg  = nbuffers + 8;
  attr.mq_msgsize = sizeof(struct audio_msg_s);
  attr.mq_curmsgs = 0;
  attr.mq_flags   = 0;
  snprintf(c->mqname, sizeof(c->mqname), "/tmp/%0lx",
           (unsigned long)(uintptr_t)c);
  c->mq = mq_open(c->mqname, O_RDWR | O_CREAT, 0644, &attr);
  if (c->mq == (mqd_t)-1)
    {
      syslog(LOG_INFO, "mq_open 失败: %d\n", -errno);
      return -errno;
    }
  ioctl(c->dev_fd, AUDIOIOC_REGISTERMQ, (uintptr_t)c->mq);

  for (x = 0; x < nbuffers; ++x)
    {
      struct audio_buf_desc_s buf_desc;
#ifdef CONFIG_AUDIO_MULTI_SESSION
      buf_desc.session = NULL;
#endif
      buf_desc.numbytes = buf_info.buffer_size;
      buf_desc.u.pbuffer = &pbuffers[x];
      ret = ioctl(c->dev_fd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&buf_desc);
      if (ret != sizeof(buf_desc))
        {
          syslog(LOG_INFO, "ALLOCBUFFER %d 失败\n", x);
          return -EIO;
        }
    }
  for (x = 0; x < nbuffers; ++x)
    mic_enqueue(c, pbuffers[x]);

  ret = ioctl(c->dev_fd, AUDIOIOC_START, 0);
  if (ret < 0)
    {
      syslog(LOG_INFO, "AUDIOIOC_START 失败: %d\n", -errno);
      return -errno;
    }

  c->running = 1;
  pthread_attr_init(&tattr);
  sparam.sched_priority = sched_get_priority_max(SCHED_FIFO) - 9;
  pthread_attr_setschedparam(&tattr, &sparam);
  pthread_attr_setstacksize(&tattr, 8192);
  ret = pthread_create(&c->thread, &tattr, mic_thread, c);
  if (ret != 0)
    {
      syslog(LOG_INFO, "pthread_create 失败: %d\n", ret);
      c->running = 0;
      ioctl(c->dev_fd, AUDIOIOC_STOP, 0);
      return -ret;
    }

  syslog(LOG_INFO, "采集已启动: %s %dHz %dch %dbit\n",
         device, samplerate, channels, bps);
  return 0;
}

int mic_capture_read(struct mic_capture_s *c, int16_t *out, int nsamples)
{
  pthread_mutex_lock(&c->lock);
  while (c->running && c->count < nsamples)
    pthread_cond_wait(&c->cond, &c->lock);
  if (c->count < nsamples)
    {
      pthread_mutex_unlock(&c->lock);
      return -1;
    }
  for (int i = 0; i < nsamples; ++i)
    {
      out[i] = c->ring[c->rd];
      c->rd = (c->rd + 1) % c->ring_size;
    }
  c->count -= nsamples;
  pthread_mutex_unlock(&c->lock);
  return 0;
}

void mic_capture_stop(struct mic_capture_s *c)
{
  if (c->running)
    {
      c->running = 0;
      ioctl(c->dev_fd, AUDIOIOC_STOP, 0);
      struct audio_msg_s msg;
      msg.msg_id = AUDIO_MSG_STOP;
      msg.u.ptr = NULL;
      mq_send(c->mq, (const char *)&msg, sizeof(msg), 0);
      pthread_cond_signal(&c->cond);
      pthread_join(c->thread, NULL);
    }
  if (c->dev_fd >= 0)
    {
      ioctl(c->dev_fd, AUDIOIOC_RELEASE, 0);
      close(c->dev_fd);
      c->dev_fd = -1;
    }
  if (c->mq != (mqd_t)-1)
    {
      mq_close(c->mq);
      mq_unlink(c->mqname);
      c->mq = (mqd_t)-1;
    }
  if (c->ring)
    {
      free(c->ring);
      c->ring = NULL;
    }
}
