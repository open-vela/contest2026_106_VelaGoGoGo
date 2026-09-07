/****************************************************************************
 * app/home_scense/wakeup/wav_player.c
 * 阻塞式一次性 WAV 播放 (唤醒“我在”应答音 /data/wakeup_wozai.wav)。
 *
 * 音频本体存 /data (随 usrdata 分区打包, startup.wav 同款机制), 播放前
 * 先整个读进堆 (仅 ~14KB), 再走内存播放。
 *
 * 缓冲管线参考 nxplayer_playthread (apps/system/nxplayer/nxplayer.c):
 * RESERVE → CONFIGURE → REGISTERMQ → ALLOCBUFFER×N → 预填 ENQUEUE →
 * START → mq 循环 DEQUEUE 补数据, 数据耗尽后不再入队。
 *
 * 收尾按本板 sunxi 驱动 (sunxi_alsa.c) 的实际行为适配, 不能照搬 nxplayer:
 *  - play_workerthread 在 pendq 排空后只是 usleep 自旋, 自然播完不会发
 *    AUDIO_MSG_COMPLETE; COMPLETE 只在 STOP 令 worker 退出后才出现。
 *    nxplayer “数据发完干等 COMPLETE” 在本板会永久挂起。
 *  - AUDIOIOC_STOP 只丢 pendq 里尚未写入声卡的数据, 已进声卡环形缓冲
 *    的部分会先 snd_vela_pcm_drain 播完 (sunxi_audio_stop)。
 *  - STOP 一次即可完成 drain+join+close; 不能重复 STOP (第二次会在
 *    audio_thread_stop 里白等信号量最长 10s)。
 * 因此: 用 outstanding 计数等所有缓冲被驱动取走(数据已全部写入声卡),
 * 再睡 WAV_TAIL_MARGIN_MS 让环形缓冲残余播完, 然后 STOP 一次, 等驱动
 * 回 COMPLETE 后清理 —— 保证整段音频完整播放且设备干净归还。
 ****************************************************************************/

#include "wav_player.h"

#include <nuttx/config.h>
#include <nuttx/audio/audio.h>

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define WAV_MAX_BUFFERS      16
#define WAV_MAX_FILE_BYTES   (512 * 1024) /* 应答音仅 ~14KB, 上限防误读大文件 */
#define WAV_READ_CHUNK       4096
#define WAV_POLL_MS          200  /* mq_timedreceive 单次等待上限 */
#define WAV_TAIL_MARGIN_MS   150  /* 缓冲全被取走后声卡环形缓冲残余上限+余量 */
#define WAV_STOP_GRACE_MS    500  /* STOP 后等 COMPLETE 的宽限 */
#define WAV_EXTRA_TIMEOUT_MS 3000 /* 兜底超时(音频时长之外再加) */

/* ---- WAV (RIFF) 小端解析 ---- */

static uint16_t rd_le16(const uint8_t *p)
{
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct wav_info_s
{
  const uint8_t *pcm;
  size_t         pcm_size;
  uint32_t       rate;
  uint32_t       channels;
  uint32_t       bits;
};

/* 只支持 PCM (audio_format==1), 头里带什么采样率/声道就按什么播 */
static int wav_parse(const uint8_t *blob, size_t size,
                     struct wav_info_s *out)
{
  size_t pos;
  bool fmt_ok = false;

  if (size < 12 || memcmp(blob, "RIFF", 4) != 0 ||
      memcmp(blob + 8, "WAVE", 4) != 0)
    {
      return -EINVAL;
    }

  pos = 12;
  while (pos + 8 <= size)
    {
      const uint8_t *body = blob + pos + 8;
      uint32_t len = rd_le32(blob + pos + 4);

      if ((size_t)(body - blob) + len > size)
        {
          break;    /* 长度越界的坏 chunk, 后面不再可信 */
        }

      if (memcmp(blob + pos, "fmt ", 4) == 0 && len >= 16)
        {
          out->channels = rd_le16(body + 2);
          out->rate     = rd_le32(body + 4);
          out->bits     = rd_le16(body + 14);
          fmt_ok        = (rd_le16(body) == 1);   /* PCM */
        }
      else if (memcmp(blob + pos, "data", 4) == 0)
        {
          out->pcm      = body;
          out->pcm_size = len;
        }

      pos += 8 + len + (len & 1);
    }

  if (!fmt_ok || out->pcm == NULL || out->pcm_size == 0 ||
      out->rate == 0 || out->channels == 0 ||
      out->bits == 0 || (out->bits % 8) != 0)
    {
      return -EINVAL;
    }
  return 0;
}

static uint64_t now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* 读整个文件进堆。分块倍增读取, 不依赖 fstat (yaffs 上行为最稳)。 */
static int wav_load(const char *path, uint8_t **out, size_t *out_size)
{
  size_t cap = 0;
  size_t len = 0;
  uint8_t *buf = NULL;
  int fd;

  fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    {
      return -errno;
    }

  for (;;)
    {
      ssize_t n;

      if (len == cap)
        {
          uint8_t *nb;

          if (cap >= WAV_MAX_FILE_BYTES)
            {
              break;    /* 超上限: 停止增长, 按 EFBIG 拒绝 */
            }
          cap = cap ? cap * 2 : WAV_READ_CHUNK;
          if (cap > WAV_MAX_FILE_BYTES)
            {
              cap = WAV_MAX_FILE_BYTES;
            }
          nb = realloc(buf, cap);
          if (nb == NULL)
            {
              free(buf);
              close(fd);
              return -ENOMEM;
            }
          buf = nb;
        }

      n = read(fd, buf + len, cap - len);
      if (n < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          free(buf);
          close(fd);
          return -errno;
        }
      if (n == 0)
        {
          break;        /* EOF */
        }
      len += n;
    }
  close(fd);

  if (len == 0 || len >= WAV_MAX_FILE_BYTES)
    {
      free(buf);
      return -EFBIG;
    }

  *out = buf;
  *out_size = len;
  return 0;
}

static int wav_play_mem(const void *wav_data, size_t wav_size,
                        const char *device);

int wav_player_play(const char *wav_path, const char *device)
{
  uint8_t *data = NULL;
  size_t size = 0;
  int ret;

  if (wav_path == NULL || device == NULL)
    {
      return -EINVAL;
    }

  /* 文件可能不存在 (如旧固件 /data 没打包应答音), 失败只记日志 */
  ret = wav_load(wav_path, &data, &size);
  if (ret != 0)
    {
      syslog(LOG_ERR, "[wav] 读 %s 失败: %d\n", wav_path, -ret);
      return ret;
    }

  ret = wav_play_mem(data, size, device);
  free(data);
  return ret;
}

/* 内存版核心: wav_data 指向完整 RIFF/WAVE blob */
static int wav_play_mem(const void *wav_data, size_t wav_size,
                        const char *device)
{
  struct wav_info_s info;
  struct ap_buffer_s *bufs[WAV_MAX_BUFFERS];
  struct audio_msg_s msg;
  struct audio_buf_desc_s buf_desc;
  struct audio_caps_desc_s cap_desc;
  struct ap_buffer_info_s buf_info;
  struct mq_attr attr;
  struct timespec ts;
  char mqname[24];
  mqd_t mq = (mqd_t)-1;
  int fd = -1;
  int allocated = 0;
  int nqueued = 0;
  int outstanding = 0;
  size_t pos = 0;
  size_t remaining;
  bool streaming;
  bool stopping = false;
  bool running = true;
  bool completed = false;   /* 收到驱动 COMPLETE (worker 已退出) */
  bool clean_stop = false;  /* 数据全部送入声卡后才 STOP, 未被截断 */
  uint64_t deadline_ms;
  int ret;
  int x;

  if (wav_data == NULL || device == NULL)
    {
      return -EINVAL;
    }

  memset(&info, 0, sizeof(info));
  if (wav_parse(wav_data, wav_size, &info) != 0)
    {
      syslog(LOG_ERR, "[wav] 非 PCM WAV, 拒绝播放\n");
      return -EINVAL;
    }
  remaining = info.pcm_size;

  fd = open(device, O_RDWR | O_CLOEXEC);
  if (fd < 0)
    {
      syslog(LOG_ERR, "[wav] 打开 %s 失败: %d\n", device, -errno);
      return -errno;
    }
  if (ioctl(fd, AUDIOIOC_RESERVE, 0) < 0)
    {
      syslog(LOG_ERR, "[wav] RESERVE 失败: %d\n", -errno);
      ret = -errno;
      goto out;
    }

  /* 采样参数: 同 nxaudio configure_audio 的 OUTPUT 配置 */
  memset(&cap_desc, 0, sizeof(cap_desc));
  cap_desc.caps.ac_len        = sizeof(struct audio_caps_s);
  cap_desc.caps.ac_type       = AUDIO_TYPE_OUTPUT;
  cap_desc.caps.ac_subtype    = AUDIO_FMT_PCM;
  cap_desc.caps.ac_channels   = info.channels;
  cap_desc.caps.ac_chmap      = 0;
  cap_desc.caps.ac_controls.hw[0] = info.rate;
  cap_desc.caps.ac_controls.b[2]  = info.bits;
  cap_desc.caps.ac_controls.b[3]  = 0;
  if (ioctl(fd, AUDIOIOC_CONFIGURE, (uintptr_t)&cap_desc) < 0)
    {
      syslog(LOG_ERR, "[wav] CONFIGURE %luHz/%luch/%lubit 失败: %d\n",
             (unsigned long)info.rate, (unsigned long)info.channels,
             (unsigned long)info.bits, -errno);
      ret = -errno;
      goto out;
    }

  if (ioctl(fd, AUDIOIOC_GETBUFFERINFO, (uintptr_t)&buf_info) < 0)
    {
      buf_info.buffer_size = CONFIG_AUDIO_BUFFER_NUMBYTES;
      buf_info.nbuffers    = CONFIG_AUDIO_NUM_BUFFERS;
    }

  /* mq 名必须唯一: 豆包播放器固定用 "/doubao_player", 撞名会把两个
   * 播放器的消息搅在一起。 */
  snprintf(mqname, sizeof(mqname), "/tmp/wk%lx",
           (unsigned long)(uintptr_t)&bufs);
  attr.mq_maxmsg  = buf_info.nbuffers + 8;
  attr.mq_msgsize = sizeof(msg);
  attr.mq_curmsgs = 0;
  attr.mq_flags   = 0;
  mq = mq_open(mqname, O_RDWR | O_CREAT, 0644, &attr);
  if (mq == (mqd_t)-1)
    {
      syslog(LOG_ERR, "[wav] mq_open 失败: %d\n", -errno);
      ret = -errno;
      goto out;
    }
  if (ioctl(fd, AUDIOIOC_REGISTERMQ, (uintptr_t)mq) < 0)
    {
      syslog(LOG_ERR, "[wav] REGISTERMQ 失败: %d\n", -errno);
      ret = -errno;
      goto out;
    }

  for (x = 0; x < buf_info.nbuffers && x < WAV_MAX_BUFFERS; ++x)
    {
      memset(&buf_desc, 0, sizeof(buf_desc));
      buf_desc.numbytes  = buf_info.buffer_size;
      buf_desc.u.pbuffer = &bufs[x];
      if (ioctl(fd, AUDIOIOC_ALLOCBUFFER, (uintptr_t)&buf_desc) !=
          (int)sizeof(buf_desc))
        {
          syslog(LOG_ERR, "[wav] ALLOCBUFFER %d 失败\n", x);
          ret = -EIO;
          goto out;
        }
      allocated++;
    }
  if (allocated == 0)
    {
      ret = -EIO;
      goto out;
    }

  /* 预填管线: 尽量把所有缓冲灌满再 START, 欠一块的部分块也照常入队 */
  for (x = 0; x < allocated && remaining > 0; ++x)
    {
      size_t n = bufs[x]->nmaxbytes < remaining ? bufs[x]->nmaxbytes
                                                : remaining;
      memcpy(bufs[x]->samp, info.pcm + pos, n);
      pos += n;
      remaining -= n;
      bufs[x]->nbytes  = n;
      bufs[x]->curbyte = 0;
      bufs[x]->flags   = 0;

      memset(&buf_desc, 0, sizeof(buf_desc));
      buf_desc.numbytes = n;
      buf_desc.u.buffer = bufs[x];
      if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER, (uintptr_t)&buf_desc) < 0)
        {
          syslog(LOG_ERR, "[wav] ENQUEUE 预填失败: %d\n", -errno);
          ret = -errno;
          goto out;
        }
      nqueued++;
      outstanding++;
    }
  if (nqueued == 0)
    {
      ret = 0;      /* 没有可播数据, 视为成功 */
      goto out;
    }
  streaming = remaining > 0;

  if (ioctl(fd, AUDIOIOC_START, 0) < 0)
    {
      syslog(LOG_ERR, "[wav] START 失败: %d\n", -errno);
      ret = -errno;
      goto out;
    }

  /* 兜底超时: 音频时长 + 余量; 正常路径远早于此结束 */
  deadline_ms = now_ms() +
      (uint64_t)info.pcm_size * 1000 /
          ((uint64_t)info.rate * info.channels * (info.bits / 8)) +
      WAV_EXTRA_TIMEOUT_MS;

  while (running)
    {
      unsigned prio;

      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_nsec += (long)WAV_POLL_MS * 1000000L;
      if (ts.tv_nsec >= 1000000000L)
        {
          ts.tv_sec++;
          ts.tv_nsec -= 1000000000L;
        }

      if (mq_timedreceive(mq, (char *)&msg, sizeof(msg), &prio, &ts) !=
          (ssize_t)sizeof(msg))
        {
          if (now_ms() < deadline_ms)
            {
              continue;
            }
          if (!stopping)
            {
              syslog(LOG_WARNING, "[wav] 播放超时, 强制 STOP\n");
              ioctl(fd, AUDIOIOC_STOP, 0);
              stopping = true;
              deadline_ms = now_ms() + WAV_STOP_GRACE_MS;
            }
          else
            {
              syslog(LOG_WARNING, "[wav] STOP 后仍未收到 COMPLETE, 放弃\n");
              break;
            }
          continue;
        }

      switch (msg.msg_id)
        {
          case AUDIO_MSG_DEQUEUE:
            {
              struct ap_buffer_s *apb = (struct ap_buffer_s *)msg.u.ptr;

              outstanding--;
              if (!stopping && streaming && apb != NULL)
                {
                  size_t n = apb->nmaxbytes < remaining ? apb->nmaxbytes
                                                        : remaining;
                  memcpy(apb->samp, info.pcm + pos, n);
                  pos += n;
                  remaining -= n;
                  apb->nbytes  = n;
                  apb->curbyte = 0;
                  apb->flags   = 0;

                  memset(&buf_desc, 0, sizeof(buf_desc));
                  buf_desc.numbytes = n;
                  buf_desc.u.buffer = apb;
                  if (ioctl(fd, AUDIOIOC_ENQUEUEBUFFER,
                            (uintptr_t)&buf_desc) == 0)
                    {
                      outstanding++;
                      if (remaining == 0)
                        {
                          streaming = false;
                        }
                    }
                  else
                    {
                      /* 入队失败: 剩余数据放弃, 等已入队部分播完 */
                      streaming = false;
                    }
                }
              else if (!stopping && !streaming && outstanding == 0)
                {
                  /* 所有数据已写入声卡: 睡掉环形缓冲残余(已进声卡的
                   * 部分 STOP 时也会 drain, 这里双保险), 然后收尾 */
                  usleep(WAV_TAIL_MARGIN_MS * 1000);
                  ioctl(fd, AUDIOIOC_STOP, 0);
                  stopping = true;
                  clean_stop = true;
                  /* COMPLETE 随后到达 (STOP 内含 drain+join+close) */
                }
            }
            break;

          case AUDIO_MSG_STOP:
            if (!stopping)
              {
                stopping = true;
                ioctl(fd, AUDIOIOC_STOP, 0);
              }
            break;

          case AUDIO_MSG_COMPLETE:
            /* 驱动 worker 已退出且 pendq 已清空, 可安全回收缓冲 */
            completed = true;
            running = false;
            break;

          default:
            break;
        }
    }

  /* 只有“数据全部送入声卡后 STOP 且驱动确认 COMPLETE”才算整段播完;
   * 超时/中途停会截断音频, 如实报错(调用方仅记日志, 不影响流程) */
  ret = (completed && clean_stop) ? 0 : -ETIMEDOUT;

out:
  /* 注意: 这里不再补发 STOP —— 正常路径上面已 STOP 过一次(内部完成
   * drain+join+close); 重复 STOP 会等一个已消费的信号量, 最长卡 10s。 */
  for (x = 0; x < allocated; ++x)
    {
      memset(&buf_desc, 0, sizeof(buf_desc));
      buf_desc.u.buffer = bufs[x];
      ioctl(fd, AUDIOIOC_FREEBUFFER, (uintptr_t)&buf_desc);
    }
  if (mq != (mqd_t)-1)
    {
      ioctl(fd, AUDIOIOC_UNREGISTERMQ, (uintptr_t)mq);
    }
  ioctl(fd, AUDIOIOC_RELEASE, 0);
  close(fd);
  if (mq != (mqd_t)-1)
    {
      mq_close(mq);
      mq_unlink(mqname);
    }
  return ret;
}
