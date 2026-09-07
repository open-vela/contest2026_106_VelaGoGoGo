/* log-mel 前端 (纯 C), 参数与训练端 features.py 一致:
 *   SR=16000, N_FFT=512, FRAME_LEN=400(25ms), FRAME_STEP=320(20ms),
 *   N_MELS=40, FMIN=0, FMAX=8000, 窗=hamming, log=log10(power+1e-10)
 * 输出 (N_FRAMES=49, N_MELS=40) float, row-major: [frame][mel]。
 */
#ifndef WAKE_MEL_FEATURES_H
#define WAKE_MEL_FEATURES_H

#define MEL_SR        16000
#define MEL_NFFT      512
#define MEL_FRAME_LEN 400
#define MEL_FRAME_STEP 320
#define MEL_NMELS     40
#define MEL_NFRAMES   49
#define MEL_EPS       1e-10f

/* 初始化窗函数与 mel 滤波器组 (首次调用自动初始化) */
void mel_init(void);

/* x: 长度 MEL_SR(16000) 的 float 样本 [-1,1]; out: [MEL_NFRAMES*MEL_NMELS] */
void mel_compute(const float *x, float *out);

#endif /* WAKE_MEL_FEATURES_H */
