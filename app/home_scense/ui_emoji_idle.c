/****************************************************************************
 * ui_emoji_idle.c — sequential multi-emoji keyframe animation.
 * Blob embedded via .S, zero malloc, zero file I/O.
 * Plays emoji 0→1→...→12 in order, loops back to 0.
 ****************************************************************************/
#include "ui_emoji_idle.h"
#include "main.h"
#include "emoji_blob.h"
#include <string.h>

typedef struct {
    lv_obj_t   *overlay;
    lv_obj_t   *img;
    lv_timer_t *timer;
    int         emoji_idx;     /* 0..EMOJI_COUNT-1 */
    int         keyframe;      /* 0..EMOJI_MAX_KEYFRAMES-1 */
    emoji_idle_exit_cb_t on_exit;
} emoji_idle_t;

static emoji_idle_t   g_idle;
static lv_image_dsc_t g_dsc;
static int            g_next_emoji;  /* round-robin cursor */

/*-----------------------------------------------------------------------
 * Timer: advance keyframe; wrap to next emoji at end of current one.
 *---------------------------------------------------------------------*/
static void anim_cb(lv_timer_t *t)
{
    (void)t;

    g_idle.keyframe++;
    if (g_idle.keyframe >= EMOJI_MAX_KEYFRAMES) {
        g_idle.emoji_idx = g_next_emoji;
        g_idle.keyframe  = 0;
        g_next_emoji     = (g_idle.emoji_idx + 1) % EMOJI_COUNT;
    }

    const unsigned char *frame = emoji_blob_data +
        ((unsigned)g_idle.emoji_idx * EMOJI_MAX_KEYFRAMES +
         (unsigned)g_idle.keyframe) * EMOJI_FRAME_BYTES;
    g_dsc.data = (uint8_t *)frame;
    lv_image_set_src(g_idle.img, &g_dsc);
}

/*-----------------------------------------------------------------------
 * Tap dismiss
 *---------------------------------------------------------------------*/
static void click_cb(lv_event_t *e)
{
    (void)e;
    if (g_idle.timer)   { lv_timer_del(g_idle.timer); }
    if (g_idle.overlay) { lv_obj_del(g_idle.overlay); }
    if (g_idle.on_exit) g_idle.on_exit();
    memset(&g_idle, 0, sizeof(g_idle));
}

/*-----------------------------------------------------------------------
 * Public
 *---------------------------------------------------------------------*/
lv_obj_t *emoji_idle_create(lv_obj_t *parent, emoji_idle_exit_cb_t on_exit)
{
    memset(&g_idle, 0, sizeof(g_idle));

    int idx = g_next_emoji;
    g_idle.emoji_idx = idx;
    g_idle.keyframe  = 0;
    g_idle.on_exit   = on_exit;
    g_next_emoji     = (idx + 1) % EMOJI_COUNT;

    /* First frame of the chosen emoji */
    const unsigned char *first = emoji_blob_data +
        ((unsigned)idx * EMOJI_MAX_KEYFRAMES) * EMOJI_FRAME_BYTES;

    memset(&g_dsc, 0, sizeof(g_dsc));
    g_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    g_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    g_dsc.header.w      = EMOJI_FRAME_W;
    g_dsc.header.h      = EMOJI_FRAME_H;
    g_dsc.header.stride = EMOJI_FRAME_W * 2;
    g_dsc.data          = (uint8_t *)first;
    g_dsc.data_size     = EMOJI_FRAME_BYTES;

    /* Overlay */
    g_idle.overlay = lv_obj_create(parent);
    lv_obj_set_size(g_idle.overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(g_idle.overlay, 0, 0);
    lv_obj_set_style_bg_color(g_idle.overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_idle.overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_idle.overlay, 0, 0);
    lv_obj_set_style_radius(g_idle.overlay, 0, 0);
    lv_obj_set_style_pad_all(g_idle.overlay, 0, 0);
    lv_obj_set_scrollbar_mode(g_idle.overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(g_idle.overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_idle.overlay, LV_OBJ_FLAG_CLICKABLE);

    /* Image — centred */
    g_idle.img = lv_image_create(g_idle.overlay);
    lv_obj_set_size(g_idle.img, EMOJI_FRAME_W, EMOJI_FRAME_H);
    lv_obj_set_style_pad_all(g_idle.img, 0, 0);
    lv_obj_set_style_border_width(g_idle.img, 0, 0);
    lv_obj_align(g_idle.img, LV_ALIGN_CENTER, 0, 0);
    lv_image_set_src(g_idle.img, &g_dsc);

    /* 150ms = ~6.7fps, smooth for 10-frame animation */
    g_idle.timer = lv_timer_create(anim_cb, 150, NULL);
    lv_timer_set_repeat_count(g_idle.timer, -1);
    lv_obj_add_event_cb(g_idle.overlay, click_cb, LV_EVENT_CLICKED, NULL);

    return g_idle.overlay;
}
