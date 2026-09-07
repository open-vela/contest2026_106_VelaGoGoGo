/****************************************************************************
 * wakeup/wav_player.h — 阻塞式一次性 WAV 播放。
 *
 * 供唤醒线程播“我在”应答音 (/data/wakeup_wozai.wav, 与 startup.wav
 * 一样打进 usrdata 分区): 传入 WAV 文件路径, 整段播完(或出错/超时)才
 * 返回。缓冲管线参考 nxplayer_playthread, 收尾按本板 sunxi 驱动的
 * 实际行为做了适配, 详见 wav_player.c 头部说明。
 ****************************************************************************/
#ifndef WAKEUP_WAV_PLAYER_H
#define WAKEUP_WAV_PLAYER_H

/* 同步播放一个 PCM WAV (RIFF) 文件。返回 0 = 整段播完; 负值 = 未播/出错。 */
int wav_player_play(const char *wav_path, const char *device);

#endif /* WAKEUP_WAV_PLAYER_H */
