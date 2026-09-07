/* log-mel 前端实现 (纯 C), 对齐 features.py。 */
#include <math.h>
#include <string.h>
#include "mel_features.h"

#define PI 3.14159265358979323846f

static float g_window[MEL_FRAME_LEN];
static float g_filterbank[MEL_NMELS][MEL_NFFT / 2 + 1];
static int g_inited = 0;

static float hz_to_mel(float f) { return 2595.0f * log10f(1.0f + f / 700.0f); }
static float mel_to_hz(float m) { return 700.0f * (powf(10.0f, m / 2595.0f) - 1.0f); }

void mel_init(void) {
  if (g_inited) return;
  /* hamming: 0.54 - 0.46*cos(2*pi*n/(N-1)) */
  for (int n = 0; n < MEL_FRAME_LEN; ++n)
    g_window[n] = 0.54f - 0.46f * cosf(2.0f * PI * n / (MEL_FRAME_LEN - 1));

  /* mel 三角滤波器组 (HTK) */
  const float mmin = hz_to_mel(0.0f);
  const float mmax = hz_to_mel(8000.0f);
  int bin[MEL_NMELS + 2];
  for (int i = 0; i < MEL_NMELS + 2; ++i) {
    float m = mmin + (mmax - mmin) * i / (MEL_NMELS + 1);
    float hz = mel_to_hz(m);
    int b = (int)floorf((MEL_NFFT + 1) * hz / MEL_SR);
    if (b < 0) b = 0;
    if (b > MEL_NFFT / 2) b = MEL_NFFT / 2;
    bin[i] = b;
  }
  for (int m = 0; m < MEL_NMELS; ++m) {
    int left = bin[m], center = bin[m + 1], right = bin[m + 2];
    for (int k = 0; k < MEL_NFFT / 2 + 1; ++k) g_filterbank[m][k] = 0.0f;
    if (center > left)
      for (int k = left; k < center; ++k)
        g_filterbank[m][k] = (float)(k - left) / (center - left);
    if (right > center)
      for (int k = center; k < right; ++k)
        g_filterbank[m][k] = (float)(right - k) / (right - center);
  }
  g_inited = 1;
}

/* 512 点 radix-2 迭代 FFT (in-place) */
static void fft512(float *re, float *im) {
  const int N = MEL_NFFT;
  int j = 0;
  for (int i = 1; i < N; ++i) {
    int bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float tr = re[i]; re[i] = re[j]; re[j] = tr;
      float ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }
  for (int len = 2; len <= N; len <<= 1) {
    float ang = -2.0f * PI / len;
    float wr = cosf(ang), wi = sinf(ang);
    for (int i = 0; i < N; i += len) {
      float cr = 1.0f, ci = 0.0f;
      for (int k = 0; k < len / 2; ++k) {
        float ur = re[i + k], ui = im[i + k];
        float vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
        float vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
        float ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = ncr;
      }
    }
  }
}

void mel_compute(const float *x, float *out) {
  mel_init();
  static float re[MEL_NFFT], im[MEL_NFFT], power[MEL_NFFT / 2 + 1];
  for (int f = 0; f < MEL_NFRAMES; ++f) {
    const float *src = x + f * MEL_FRAME_STEP;
    for (int n = 0; n < MEL_NFFT; ++n) {
      im[n] = 0.0f;
      re[n] = (n < MEL_FRAME_LEN) ? src[n] * g_window[n] : 0.0f;
    }
    fft512(re, im);
    for (int b = 0; b < MEL_NFFT / 2 + 1; ++b)
      power[b] = re[b] * re[b] + im[b] * im[b];
    for (int m = 0; m < MEL_NMELS; ++m) {
      const float *fb = g_filterbank[m];
      float e = 0.0f;
      for (int b = 0; b < MEL_NFFT / 2 + 1; ++b) e += fb[b] * power[b];
      out[f * MEL_NMELS + m] = log10f(e > MEL_EPS ? e : MEL_EPS);
    }
  }
}
