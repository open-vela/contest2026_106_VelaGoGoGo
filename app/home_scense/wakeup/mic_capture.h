/* mic_capture: 内存流式麦克风采集 (参考 apps/system/nxrecorder 的 recordthread)。
 * 直接用 NuttX audio ioctl 把 PCM 读入环形 buffer, 不落盘。
 * 格式: 16kHz / mono / 16bit。
 */
#ifndef WAKE_MIC_CAPTURE_H
#define WAKE_MIC_CAPTURE_H

#include <stdint.h>
#include <pthread.h>
#include <mqueue.h>

struct mic_capture_s
{
  int               dev_fd;       /* 音频设备 fd */
  mqd_t             mq;           /* 消息队列 (驱动回 buffer) */
  char              mqname[32];
  pthread_t         thread;
  volatile int      running;

  /* int16 环形 buffer (按样本计) */
  int16_t          *ring;
  int               ring_size;    /* 容量(样本数) */
  int               wr;           /* 写位置 */
  int               rd;           /* 读位置 */
  int               count;        /* 可读样本数 */
  pthread_mutex_t   lock;
  pthread_cond_t    cond;
};

/* 打开设备并启动采集。device 如 "/dev/audio/pcm0c"。返回 0 成功。 */
int mic_capture_start(struct mic_capture_s *c, const char *device,
                      int samplerate, int channels, int bps);

/* 阻塞读取 nsamples 个 int16 样本到 out。返回 0 成功, <0 结束/错误。 */
int mic_capture_read(struct mic_capture_s *c, int16_t *out, int nsamples);

/* 停止采集并释放资源。 */
void mic_capture_stop(struct mic_capture_s *c);

#endif /* WAKE_MIC_CAPTURE_H */
