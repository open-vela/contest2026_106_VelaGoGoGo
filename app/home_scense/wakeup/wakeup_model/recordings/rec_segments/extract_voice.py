#!/usr/bin/env python3
"""从 rec.pcm (16kHz/16bit/mono) 中检测人声段并切分保存为 WAV。"""
import wave
from pathlib import Path

import numpy as np
import webrtcvad

SRC = Path('rec.pcm')
OUT_DIR = Path('voice_segments')
SR = 16000

FRAME_MS = 30                 # webrtcvad 帧长
FRAME = SR * FRAME_MS // 1000 # 480 samples
AGGRESSIVENESS = 2            # 0-3，越大越严格
MIN_SPEECH = 0.30             # 丢弃短于此的段（秒），多为咔哒声/噪声
BRIDGE_GAP = 0.30             # 短于此的静音间隙并入同一段
PAD = 0.25                    # 每段前后保留的余量（秒）


def main():
    pcm = SRC.read_bytes()
    assert len(pcm) % 2 == 0, 'not 16-bit aligned'
    x = np.frombuffer(pcm, dtype='<i2')
    total = len(x) / SR
    print(f'{SRC}: {total:.2f}s, {len(x)} samples')

    # --- VAD：逐帧判定是否人声 ---
    vad = webrtcvad.Vad(AGGRESSIVENESS)
    n_frames = len(x) // FRAME
    voiced = np.zeros(n_frames, dtype=bool)
    for i in range(n_frames):
        f = pcm[i * FRAME * 2:(i + 1) * FRAME * 2]
        voiced[i] = vad.is_speech(f, SR)

    ratio = voiced.mean()
    print(f'VAD 判定语音帧占比: {ratio:.1%}')

    # --- 由帧标签得到段，合并间隙、过滤短段 ---
    segs = []
    i = 0
    while i < n_frames:
        if voiced[i]:
            j = i
            last_v = i
            while j < n_frames:
                if voiced[j]:
                    last_v = j
                elif (j - last_v) * FRAME_MS / 1000 > BRIDGE_GAP:
                    break
                j += 1
            segs.append((i, last_v + 1))  # 帧区间 [i, last_v+1)
            i = j
        else:
            i += 1

    segs = [(a / n_frames * total, b / n_frames * total)
            for a, b in segs if (b - a) * FRAME_MS / 1000 >= MIN_SPEECH]
    if not segs:
        print('未检测到人声段')
        return

    # --- 前后加 PAD 后写出 WAV（直接拷贝原始 int16，无损） ---
    OUT_DIR.mkdir(exist_ok=True)
    for old in OUT_DIR.glob('seg_*.wav'):
        old.unlink()

    total_speech = 0.0
    lines = []
    for k, (s, e) in enumerate(segs, 1):
        s = max(0.0, s - PAD)
        e = min(total, e + PAD)
        chunk = x[int(s * SR):int(e * SR)]
        path = OUT_DIR / f'seg_{k:03d}.wav'
        with wave.open(str(path), 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(SR)
            w.writeframes(chunk.tobytes())
        total_speech += e - s
        lines.append(f'{k:3d}  {s:8.2f} - {e:8.2f}   {e - s:6.2f}s  {path}')

    print(f'\n共 {len(segs)} 段，语音总时长 {total_speech:.1f}s / {total:.1f}s\n')
    print(' idx   start      end     dur    file')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
