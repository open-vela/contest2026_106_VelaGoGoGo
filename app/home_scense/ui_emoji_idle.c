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
static int            g_next_emoji;  /* next automatic emoji index */
static bool           g_named_play;  /* keep a named emoji selected */

/*-----------------------------------------------------------------------
 * Status-source arbitration.
 * Each status source parks the emoji name it wants (or "" for none);
 * the highest-priority non-empty request owns the overlay. g_status_shown
 * remembers what is currently displayed on behalf of a source so repeated
 * requests (polled every 100-150ms) don't restart the animation.
 *---------------------------------------------------------------------*/
#define EMOJI_NAME_MAX 24
static char g_src_name[EMOJI_SRC_COUNT][EMOJI_NAME_MAX];
static char g_status_shown[EMOJI_NAME_MAX];

/* Only these resources participate in automatic startup/idle rotation. */
static const int g_auto_emoji_indices[] = { 0, 1 }; /* 02, 03 */
static size_t g_auto_emoji_pos;

/*-----------------------------------------------------------------------
 * Timer: advance keyframe; wrap to next emoji at end of current one.
 *---------------------------------------------------------------------*/
static void anim_cb(lv_timer_t *t)
{
    (void)t;

    g_idle.keyframe++;
    if (g_idle.keyframe >= EMOJI_MAX_KEYFRAMES) {
        g_idle.keyframe = 0;
        if (!g_named_play) {
            g_idle.emoji_idx = g_auto_emoji_indices[g_auto_emoji_pos];
            g_auto_emoji_pos = (g_auto_emoji_pos + 1) %
                (sizeof(g_auto_emoji_indices) / sizeof(g_auto_emoji_indices[0]));
            g_next_emoji = g_auto_emoji_indices[g_auto_emoji_pos];
        }
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
    g_named_play = false;
}

/*-----------------------------------------------------------------------
 * Public
 *---------------------------------------------------------------------*/
static int emoji_index_from_name(const char *name)
{
    if (!name) return -1;

    for (int i = 0; i < EMOJI_COUNT; i++) {
        if (strcmp(name, g_emoji_names[i]) == 0) return i;
    }

    return -1;
}

void emoji_idle_stop(void)
{
    if (g_idle.timer)   lv_timer_del(g_idle.timer);
    if (g_idle.overlay) lv_obj_del(g_idle.overlay);
    memset(&g_idle, 0, sizeof(g_idle));
    g_named_play = false;
}

bool emoji_idle_is_active(void)
{
    return g_idle.overlay != NULL;
}

lv_obj_t *emoji_idle_play(lv_obj_t *parent, const char *name,
                          emoji_idle_exit_cb_t on_exit)
{
    int idx = emoji_index_from_name(name);
    if (idx < 0) return NULL;

    emoji_idle_stop();
    g_next_emoji = idx;
    g_named_play = true;

    return emoji_idle_create(parent, on_exit);
}

lv_obj_t *emoji_idle_create(lv_obj_t *parent, emoji_idle_exit_cb_t on_exit)
{
    memset(&g_idle, 0, sizeof(g_idle));

    int idx;
    if (g_named_play) {
        idx = g_next_emoji;
    } else {
        idx = g_auto_emoji_indices[g_auto_emoji_pos];
        g_auto_emoji_pos = (g_auto_emoji_pos + 1) %
            (sizeof(g_auto_emoji_indices) / sizeof(g_auto_emoji_indices[0]));
    }

    g_idle.emoji_idx = idx;
    g_idle.keyframe  = 0;
    g_idle.on_exit   = on_exit;

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

    /* 300ms = ~3.3fps, slower animation playback */
    g_idle.timer = lv_timer_create(anim_cb, 300, NULL);
    lv_timer_set_repeat_count(g_idle.timer, -1);
    lv_obj_add_event_cb(g_idle.overlay, click_cb, LV_EVENT_CLICKED, NULL);

    return g_idle.overlay;
}

/*-----------------------------------------------------------------------
 * Status-source arbitration (priority: VOICE > CLAUDE)
 *---------------------------------------------------------------------*/
static void status_apply(void)
{
    const char *winner = NULL;

    /* Highest enum value wins; first non-empty request from the top. */
    for (int s = EMOJI_SRC_COUNT - 1; s >= 0; s--) {
        if (g_src_name[s][0]) { winner = g_src_name[s]; break; }
    }

    if (winner) {
        /* Re-assert when the winner changed, or when the overlay was dismissed
         * (tap) / replaced out from under us: a status feed is live, so it keeps
         * reflecting state rather than staying gone. */
        if (strcmp(winner, g_status_shown) != 0 || !emoji_idle_is_active()) {
            if (emoji_idle_play(lv_scr_act(), winner, NULL)) {
                strncpy(g_status_shown, winner, EMOJI_NAME_MAX - 1);
                g_status_shown[EMOJI_NAME_MAX - 1] = '\0';
            }
        }
    } else if (g_status_shown[0]) {
        /* No source wants the overlay: release it so the idle rotation
         * (main.c) can take over again. */
        g_status_shown[0] = '\0';
        emoji_idle_stop();
    }
}

void emoji_idle_request(emoji_source_t src, const char *name)
{
    if ((unsigned)src >= EMOJI_SRC_COUNT) return;

    if (name && name[0]) {
        strncpy(g_src_name[src], name, EMOJI_NAME_MAX - 1);
        g_src_name[src][EMOJI_NAME_MAX - 1] = '\0';
    } else {
        g_src_name[src][0] = '\0';
    }
    status_apply();
}

bool emoji_idle_status_active(void)
{
    return g_status_shown[0] != '\0';
}
