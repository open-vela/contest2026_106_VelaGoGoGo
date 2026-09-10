/****************************************************************************
 * app/home_scense/wakeup/wakeup.c
 * Hands-free wake-word trigger for the Doubao voice assistant.
 *
 * Ported from apps/wake_demo (self-contained pure-C KWS, no tflite-micro):
 * log-mel front end + CNN forward + consecutive-window decision — maths and
 * thresholds are byte-for-byte the same as the demo, validated by
 * wake_model/host_test.  Instead of a main() this is a background thread:
 * while the assistant is idle it owns the mic; on the wake word it stops
 * its capture, plays the “我在” ack (/data/wakeup_wozai.wav via wav_player,
 * nxplayer-style; packed into the usrdata partition alongside startup.wav),
 * and only then calls doubao_voice_start(), equivalent to tapping 点击说话.
 *
 * Mic arbitration (双向握手): doubao holds /dev/audio/pcm0c for the WHOLE
 * talking session (LISTENING..PLAYING, not just RECORDING), so this thread
 * yields the mic for that entire window (assistant_busy() = talking) and
 * reacquires it only after the user stops the session.  Conversely, doubao
 * waits for wakeup_is_recording() to clear before opening its capture; the
 * flag is claimed optimistically before open, so the two sides can never
 * open the device concurrently — each side's check is ordered after the
 * other's claim.
 ****************************************************************************/

#include "wakeup.h"
#include "mel_features.h"
#include "mic_capture.h"
#include "wake_model_weights.h"
#include "wav_player.h"
#include "../doubao/doubao_voice.h"

#include <nuttx/config.h>

#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#ifndef CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP_CAPTURE_DEV
#  define CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP_CAPTURE_DEV "/dev/audio/pcm0c"
#endif

/* 应答音播放设备: 与豆包 TTS 同一块声卡 (doubao_voice.c 同款宏) */
#ifndef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV
#  define CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV "/dev/audio/pcm0p"
#endif

/* “我在”应答音: 存 /data (usrdata 分区), 打包方式同 startup.wav ——
 * build.sh 打包时临时拷进 lichee/board/common/data/UDISK/, make_usrdata_image
 * 用该目录生成 usrdata.fex。文件不存在时跳过应答音, 不影响唤醒流程。 */
#define WAKE_ACK_WAV "/data/wakeup_wozai.wav"

/* ---- 共用参数 (与 sp_vela/apps/wake_demo 一致, 改动需重跑流式仿真) ---- */
/* 阈值经流式仿真调优 (seed42 模型, 16/32/64 通道; 误报流 val+test 负样本 0/+6/+12dB):
 * 0.88 + 连续4窗(400ms 持续): 困难负样本 neg_zh/neg_jul/neg_board + mixed 误报全 0,
 * val/test 段唤醒率 84.6% (0/-6/-12dB 一致); rec.pcm 两次真唤醒 0dB 与 -6dB 均 2/2 命中
 * (30.2s/33.8s)。阈值上限受 rec.pcm -6dB 第二次峰值(0.966)约束, >0.88 会漏。重训后需重调。 */
#define WAKE_THR         0.995f        /* 唤醒阈值 */
#define WAKE_CONSEC      6             /* 连续 N 窗超阈才判唤醒 (1帧=100ms) */
#define WAKE_HOP_SAMPLES (MEL_SR / 10) /* 100ms 步长 */
#define WAKE_MIN_DIST    12            /* 两次唤醒最小间隔(帧, 1帧=100ms) */

/* ---- 推理缓冲区 (尺寸由权重头的 OUT 宏决定, 适配任意层宽) ---- */
#define POOL1_H (MEL_NFRAMES / 2)   /* 49 -> 24 */
#define POOL1_W (MEL_NMELS / 2)     /* 40 -> 20 */
#define POOL2_H (POOL1_H / 2)       /* 24 -> 12 */
#define POOL2_W (POOL1_W / 2)       /* 20 -> 10 */
static float buf_conv1[MEL_NFRAMES * MEL_NMELS * CONV1_OUT];
static float buf_pool1[POOL1_H * POOL1_W * CONV1_OUT];
static float buf_conv2[POOL1_H * POOL1_W * CONV2_OUT];
static float buf_pool2[POOL2_H * POOL2_W * CONV2_OUT];
static float buf_conv3[POOL2_H * POOL2_W * CONV3_OUT];
static float buf_gap[CONV3_OUT];
static float buf_d1[DENSE1_OUT];

/* 3x3 same padding, stride 1, +ReLU. w 布局 (kh,kw,in,out), b[out] */
static void conv3x3_same_relu(const float *in, int H, int W, int Cin,
                              const float *w, const float *b, int Cout,
                              float *out) {
  for (int o = 0; o < Cout; ++o) {
    for (int h = 0; h < H; ++h) {
      for (int p = 0; p < W; ++p) {
        float acc = b[o];
        for (int kh = 0; kh < 3; ++kh) {
          int ih = h + kh - 1;
          if (ih < 0 || ih >= H) continue;
          for (int kw = 0; kw < 3; ++kw) {
            int iw = p + kw - 1;
            if (iw < 0 || iw >= W) continue;
            const float *ip = in + ((ih * W) + iw) * Cin;
            for (int c = 0; c < Cin; ++c)
              acc += ip[c] * w[(((kh * 3) + kw) * Cin + c) * Cout + o];
          }
        }
        out[((h * W) + p) * Cout + o] = acc > 0 ? acc : 0;
      }
    }
  }
}

/* 2x2 max pool, stride 2, valid */
static void maxpool2(const float *in, int H, int W, int C, float *out,
                     int *oH, int *oW) {
  *oH = H / 2;
  *oW = W / 2;
  for (int h = 0; h < *oH; ++h)
    for (int w = 0; w < *oW; ++w)
      for (int c = 0; c < C; ++c) {
        float m = in[((2 * h) * W + (2 * w)) * C + c];
        float t;
        t = in[((2 * h) * W + (2 * w + 1)) * C + c];
        if (t > m) m = t;
        t = in[((2 * h + 1) * W + (2 * w)) * C + c];
        if (t > m) m = t;
        t = in[((2 * h + 1) * W + (2 * w + 1)) * C + c];
        if (t > m) m = t;
        out[((h * (*oW)) + w) * C + c] = m;
      }
}

/* 全局平均池化 */
static void gap(const float *in, int H, int W, int C, float *out) {
  for (int c = 0; c < C; ++c) {
    float s = 0;
    for (int i = 0; i < H * W; ++i) s += in[i * C + c];
    out[c] = s / (H * W);
  }
}

/* dense + ReLU. w 布局 (in,out) */
static void dense_relu(const float *in, int Din, const float *w,
                       const float *b, int Dout, float *out) {
  for (int o = 0; o < Dout; ++o) {
    float acc = b[o];
    for (int i = 0; i < Din; ++i) acc += in[i] * w[i * Dout + o];
    out[o] = acc > 0 ? acc : 0;
  }
}

/* dense + softmax */
static void dense_softmax(const float *in, int Din, const float *w,
                          const float *b, int Dout, float *out) {
  float mx = -1e30f;
  for (int o = 0; o < Dout; ++o) {
    float acc = b[o];
    for (int i = 0; i < Din; ++i) acc += in[i] * w[i * Dout + o];
    out[o] = acc;
    if (acc > mx) mx = acc;
  }
  float s = 0;
  for (int o = 0; o < Dout; ++o) {
    out[o] = expf(out[o] - mx);
    s += out[o];
  }
  for (int o = 0; o < Dout; ++o) out[o] /= s;
}

/* mel: [49*40] float -> prob: [3] */
static void predict(const float *mel, float *prob) {
  conv3x3_same_relu(mel, MEL_NFRAMES, MEL_NMELS, CONV1_IN, conv1_w, conv1_b,
                    CONV1_OUT, buf_conv1);
  int H, W;
  maxpool2(buf_conv1, MEL_NFRAMES, MEL_NMELS, CONV1_OUT, buf_pool1, &H, &W);
  conv3x3_same_relu(buf_pool1, H, W, CONV2_IN, conv2_w, conv2_b, CONV2_OUT,
                    buf_conv2);
  maxpool2(buf_conv2, H, W, CONV2_OUT, buf_pool2, &H, &W);
  conv3x3_same_relu(buf_pool2, H, W, CONV3_IN, conv3_w, conv3_b, CONV3_OUT,
                    buf_conv3);
  gap(buf_conv3, H, W, CONV3_OUT, buf_gap);
  dense_relu(buf_gap, CONV3_OUT, dense1_w, dense1_b, DENSE1_OUT, buf_d1);
  dense_softmax(buf_d1, DENSE1_OUT, dense2_w, dense2_b, DENSE2_OUT, prob);
}

/*-----------------------------------------------------------------------
 * Wake thread
 *---------------------------------------------------------------------*/

static struct
{
  pthread_t     thread;
  volatile bool running;
  volatile bool recording;   /* 持麦标志: 置位含"开麦意向", 豆包开麦前等它清零 */
  bool          initialized;
} g_wakeup;

/* 豆包全双工期间(talking=true)整个会话都持有 /dev/audio/pcm0c —— 不只是
 * RECORDING 状态, LISTENING 等待用户开口时麦克风也开着。因此这里以 talking
 * 为让出依据: doubao_voice_start() 一置 talking 本线程就让麦, 直到用户点击
 * 停止(talking=false)后才重新开麦。 */
static bool assistant_busy(void) {
  return doubao_voice_is_talking();
}

/* 应答音只在豆包空闲时播: 录音中播会被它的 ASR 收进去; 等回复/播放
 * TTS 时它占用或即将占用扬声器, 抢开 pcm0p 重复 CONFIGURE 会互相打断。
 * 这几种状态直接跳过应答音, 只触发下一轮。 */
static bool ack_allowed(void) {
  doubao_voice_snapshot_t snapshot;
  doubao_voice_get_snapshot(&snapshot);
  return snapshot.state != DOUBAO_VOICE_RECORDING &&
         snapshot.state != DOUBAO_VOICE_WAITING_RESPONSE &&
         snapshot.state != DOUBAO_VOICE_PLAYING;
}

static void *wakeup_thread(void *arg) {
  static float window[MEL_SR];                    /* 1s 滑动窗 */
  static int16_t hopbuf[WAKE_HOP_SAMPLES];        /* 每 hop 读入 */
  static float mel[MEL_NFRAMES * MEL_NMELS];
  struct mic_capture_s cap;
  bool capturing = false;
  bool mic_ready_logged = false;   /* 首次拿到麦克风时打一次"开始监听"日志 */
  float p3[3];
  int f = 0;                       /* 帧计数(100ms/帧) */
  int last_wake = -WAKE_MIN_DIST;
  int consec = 0;                  /* 当前连续超阈窗数 */
  int wakes = 0;

  (void)arg;
  mel_init();
  memset(window, 0, sizeof(window));

  /* 服务启动日志: 监听线程已真正跑起来。用 LOG_WARNING 保证不被 INFO 级过滤。 */
  syslog(LOG_WARNING,
         "[wakeup] ========== 唤醒监听线程已运行, 等待唤醒词『你好,openvela』 ==========\n");

  while (g_wakeup.running) {
    /* 仲裁: 豆包会话期间(talking)整段让出麦克风, 用户点击停止后才拿回 */
    if (assistant_busy()) {
      if (capturing) {
        mic_capture_stop(&cap);
        capturing = false;
        g_wakeup.recording = false;
        syslog(LOG_WARNING, "[wakeup] 停止录音 (豆包开始会话, 让出麦克风)\n");
      }
      usleep(200 * 1000);
      syslog(LOG_WARNING, "[wakeup] 豆包录音中......\n");
      continue;
    }
    if (!capturing) {
      /* 先占坑再开麦: recording 置位后豆包开麦前会等它清零 —— 即使与
       * 豆包 doubao_voice_start() 竞态, 占坑后的复核也能保证只有一方
       * 真正打开设备 (对方要么看到 talking 而退让, 要么等本标志清零)。 */
      g_wakeup.recording = true;
      if (assistant_busy()) {       /* 占坑期间豆包抢先? 退让 */
        g_wakeup.recording = false;
        usleep(200 * 1000);
        continue;
      }
      if (mic_capture_start(&cap,
                            CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP_CAPTURE_DEV,
                            MEL_SR, 1, 16) != 0) {
        g_wakeup.recording = false;
        usleep(200 * 1000);        /* 设备未就绪/被占, 稍后重试 */
        continue;
      }
      capturing = true;
      syslog(LOG_WARNING, "[wakeup] 开始录音 (%s)\n",
             CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP_CAPTURE_DEV);
      memset(window, 0, sizeof(window));   /* 旧窗口作废, 重新积累 1s */
      consec = 0;
      /* 服务启动日志: 首次拿到麦克风即宣告开始监听(拿设备可能重试多次)。 */
      if (!mic_ready_logged) {
        mic_ready_logged = true;
        syslog(LOG_WARNING,
               "[wakeup] 麦克风就绪, 开始实时监听唤醒词\n");
      }
    }
    if (mic_capture_read(&cap, hopbuf, WAKE_HOP_SAMPLES) != 0) {
      mic_capture_stop(&cap);      /* 读异常: 释放重开 */
      capturing = false;
      g_wakeup.recording = false;
      syslog(LOG_WARNING, "[wakeup] 停止录音 (读取异常, 重新打开麦克风)\n");
      continue;
    }

    /* 滑动窗口左移 hop, 末尾追加新数据 */
    memmove(window, window + WAKE_HOP_SAMPLES,
            (MEL_SR - WAKE_HOP_SAMPLES) * sizeof(float));
    for (int i = 0; i < WAKE_HOP_SAMPLES; ++i)
      window[MEL_SR - WAKE_HOP_SAMPLES + i] = hopbuf[i] / 32768.0f;

    mel_compute(window, mel);
    predict(mel, p3);
    float prob = p3[0];
    ++f;

    //syslog(LOG_INFO,"[wakeup]:current prob=%.3f\n",(double)prob);

    /* 连续 WAKE_CONSEC 窗超阈才判唤醒: 误报多为孤立单窗尖峰,
     * 真唤醒词会连续 10+ 窗超阈。 */
    if (prob > WAKE_THR) ++consec; else consec = 0;
    if (consec >= WAKE_CONSEC && (f - last_wake) >= WAKE_MIN_DIST) {
      last_wake = f;
      consec = 0;
      ++wakes;
      syslog(LOG_INFO,
             "[wakeup] *** 唤醒 *** 你好,openvela 第%d次 t=%.1fs prob=%.3f\n",
             wakes, (double)f * 0.1, (double)prob);
      /* 唤醒确认三步: 先停本线程录音, 再播“我在”应答音(阻塞至整段
       * 播完, wav_player), 最后才通知豆包开始录音 —— 顺序保证应答音
       * 不会被豆包 ASR 收进去, 豆包开麦时设备也早已空闲。 */
      mic_capture_stop(&cap);
      capturing = false;
      g_wakeup.recording = false;
      syslog(LOG_WARNING, "[wakeup] 停止录音 (检测到唤醒词)\n");
      if (ack_allowed()) {
        if (wav_player_play(WAKE_ACK_WAV,
                            CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_PLAYBACK_DEV) == 0)
          syslog(LOG_WARNING, "[wakeup] 应答音播放完成\n");
        else
          syslog(LOG_WARNING, "[wakeup] 应答音播放失败: %s\n", WAKE_ACK_WAV);
      } else {
        syslog(LOG_WARNING, "[wakeup] 跳过应答音 (豆包占用扬声器/录音中)\n");
      }
      doubao_voice_start();
    }
  }

  if (capturing) {
    mic_capture_stop(&cap);
    g_wakeup.recording = false;
    syslog(LOG_WARNING, "[wakeup] 停止录音 (监听线程退出)\n");
  }
  syslog(LOG_WARNING, "[wakeup] 监听线程退出, 累计唤醒 %d 次\n", wakes);
  return NULL;
}

/*-----------------------------------------------------------------------
 * Public API
 *---------------------------------------------------------------------*/

int wakeup_init(void) {
  pthread_attr_t attr;
  int ret;

  if (g_wakeup.initialized)
    return 0;

  /* 服务启动日志: 进入初始化即打印醒目横幅, 用 LOG_WARNING 确保串口可见。 */
  syslog(LOG_WARNING,
         "[wakeup] ========== 唤醒服务启动中 (wake-word service starting) ==========\n");

  g_wakeup.running = true;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 32768);
  ret = pthread_create(&g_wakeup.thread, &attr, wakeup_thread, NULL);
  pthread_attr_destroy(&attr);
  if (ret != 0) {
    g_wakeup.running = false;
    syslog(LOG_ERR, "[wakeup] !!! 启动失败: 线程创建失败 ret=%d !!!\n", ret);
    return -ret;
  }
  g_wakeup.initialized = true;
  syslog(LOG_WARNING,
         "[wakeup] ========== 唤醒服务已启动 (dev=%s, 连续%d窗 prob>%.2f, 唤醒词: 你好,openvela) ==========\n",
         CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP_CAPTURE_DEV,
         WAKE_CONSEC, (double)WAKE_THR);
  return 0;
}

void wakeup_deinit(void) {
  if (!g_wakeup.initialized)
    return;

  g_wakeup.running = false;
  pthread_join(g_wakeup.thread, NULL);
  g_wakeup.initialized = false;
  syslog(LOG_WARNING, "[wakeup] 唤醒服务已停止\n");
}

bool wakeup_is_recording(void) {
  return g_wakeup.recording;
}
