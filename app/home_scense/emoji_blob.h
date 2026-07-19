/****************************************************************************
 * emoji_blob.h — constants for the embedded emoji keyframe blob.
 *
 * Layout: 13 emoji × 10 keyframes, each 320×132 RGB565 (84,480 bytes).
 * Access:  emoji_blob_data[(emoji * 10 + keyframe) * 84480]
 ****************************************************************************/
#ifndef EMOJI_BLOB_H
#define EMOJI_BLOB_H

#define EMOJI_COUNT         13
#define EMOJI_MAX_KEYFRAMES  10
#define EMOJI_FRAME_W        320
#define EMOJI_FRAME_H        132
#define EMOJI_FRAME_BYTES    84480   /* 320 × 132 × 2 */

extern const unsigned char emoji_blob_data[];
extern const unsigned char emoji_blob_end[];

#endif
