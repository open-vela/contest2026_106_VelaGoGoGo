#ifndef EMOJI_BLOB_H
#define EMOJI_BLOB_H

// Auto-generated — 6 emoji, 30 frames\n// RGB565 320x132  84480 bytes/frame
#define EMOJI_COUNT        6
#define EMOJI_MAX_KEYFRAMES 5
#define EMOJI_FRAME_W      320
#define EMOJI_FRAME_H      132
#define EMOJI_FRAME_BYTES  84480

extern const unsigned char emoji_blob_data[];
extern const unsigned char emoji_blob_end[];

static const char *const g_emoji_names[EMOJI_COUNT] = {
    "02", "03", "drink", "exercise", "listen", "speak"
};

/* Keyframes per emoji (resource order) */
static const int g_emoji_frames[EMOJI_COUNT] = {
    5, 5, 5, 5, 5, 5
};

#endif /* EMOJI_BLOB_H */
