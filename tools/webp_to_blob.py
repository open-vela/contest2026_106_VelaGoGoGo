#!/usr/bin/env python3
"""
Convert all WebP emoji assets into a single RGB565 binary blob + .S linker file.

Usage:
    python3 tools/webp_to_blob.py ~/download/素材07-灵动表情 app/home_scense/

Output:
    app/home_scense/emoji_blob.bin  — concatenated RGB565 keyframes
    app/home_scense/emoji_blob.S    — assembler incbin wrapper
    app/home_scense/emoji_blob.h    — C constants

Layout:
    13 emojis (00.webp..13.webp, skipping 10), 5 keyframes each (static = 1).
    Each frame: 320×132 RGB565 (84,480 bytes).
    Access: emoji_blob_data + (emoji_idx * 5 + keyframe) * 84480
"""

import argparse, os, sys
from PIL import Image

FRAME_W   = 320
FRAME_H   = 132
KEYFRAMES = 5


def pick_keyframes(n_total, n_want=KEYFRAMES):
    if n_total <= n_want:
        return list(range(n_total))
    step = n_total / n_want
    return [int(i * step) for i in range(n_want)]


def convert_frame(frame):
    """RGBA → RGB565 bytes"""
    frame = frame.resize((FRAME_W, FRAME_H), Image.LANCZOS).convert("RGBA")
    data = bytearray()
    for y in range(FRAME_H):
        for x in range(FRAME_W):
            r, g, b, a = frame.getpixel((x, y))
            if a < 128:
                r = g = b = 0
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F
            rgb565 = (r5 << 11) | (g6 << 5) | b5
            data.append(rgb565 & 0xFF)
            data.append((rgb565 >> 8) & 0xFF)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description="WebP → RGB565 blob + .S")
    parser.add_argument("src_dir", help="Directory containing 00.webp..13.webp")
    parser.add_argument("dst_dir", help="Output directory (e.g. app/home_scense/)")
    args = parser.parse_args()

    names = ["00", "01", "02", "03", "04", "05", "06", "07",
             "08", "09", "11", "12", "13"]
    frame_counts = []
    all_data = bytearray()

    for name in names:
        path = os.path.join(args.src_dir, f"{name}.webp")
        if not os.path.exists(path):
            print(f"WARNING: {path} not found — skipping")
            continue
        img = Image.open(path)
        n = getattr(img, "n_frames", 1)
        kf = pick_keyframes(n)
        frame_counts.append(len(kf))
        for fi in kf:
            img.seek(fi)
            all_data += convert_frame(img.copy())

    # Write binary blob
    blob_path = os.path.join(args.dst_dir, "emoji_blob.bin")
    with open(blob_path, "wb") as f:
        f.write(all_data)

    frame_bytes = FRAME_W * FRAME_H * 2
    total_frames = len(all_data) // frame_bytes

    # Write assembler stub (absolute path required by incbin)
    src_abs = os.path.abspath(blob_path)
    s_path = os.path.join(args.dst_dir, "emoji_blob.S")
    with open(s_path, "w") as f:
        f.write('    .section .rodata.emoji,"a",%progbits\n')
        f.write('    .global emoji_blob_data\n')
        f.write('    .global emoji_blob_end\n')
        f.write('    .balign 4\n')
        f.write('emoji_blob_data:\n')
        f.write(f'    .incbin "{src_abs}"\n')
        f.write('emoji_blob_end:\n')

    # Write C header
    h_path = os.path.join(args.dst_dir, "emoji_blob.h")
    with open(h_path, "w") as f:
        f.write(f"// Auto-generated — {len(names)} emoji, {total_frames} frames\n")
        f.write(f"// RGB565 {FRAME_W}x{FRAME_H}  {frame_bytes} bytes/frame\n")
        f.write(f"#define EMOJI_COUNT        {len(names)}\n")
        f.write(f"#define EMOJI_MAX_KEYFRAMES {KEYFRAMES}\n")
        f.write(f"#define EMOJI_FRAME_W      {FRAME_W}\n")
        f.write(f"#define EMOJI_FRAME_H      {FRAME_H}\n")
        f.write(f"#define EMOJI_FRAME_BYTES  {frame_bytes}\n")
        f.write("\n")
        f.write("extern const unsigned char emoji_blob_data[];\n")
        f.write("extern const unsigned char emoji_blob_end[];\n")
        f.write("\n")
        f.write("/* Keyframes per emoji (order: 00.webp..13.webp) */\n")
        f.write("static const int g_emoji_frames[EMOJI_COUNT] = {\n")
        f.write("    " + ", ".join(str(c) for c in frame_counts) + "\n};\n")

    blob_mb = len(all_data) / (1024 * 1024)
    print(f"Generated: {blob_path} ({blob_mb:.1f} MB, {total_frames} frames)")
    print(f"           {s_path}")
    print(f"           {h_path}")


if __name__ == "__main__":
    main()
