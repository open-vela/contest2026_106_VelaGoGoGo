/****************************************************************************
 * app/home_scense/ui_emoji_idle.h
 * Idle emoji screen — fullscreen sequential keyframe animation overlay.
 ****************************************************************************/

#ifndef HOME_SCENSE_UI_EMOJI_IDLE_H
#define HOME_SCENSE_UI_EMOJI_IDLE_H

#include <lvgl/lvgl.h>

/** Called when the idle emoji screen is dismissed by user tap */
typedef void (*emoji_idle_exit_cb_t)(void);

/**
 * Status sources that may claim the fullscreen emoji overlay.
 * Higher enum value = higher priority: a live VOICE request preempts a CLAUDE
 * one, so the two status feeds never fight over the same overlay.
 * The auto 02/03 idle rotation is not a source here; it resumes on its own
 * (via main.c) whenever no source holds the overlay.
 */
typedef enum {
    EMOJI_SRC_CLAUDE = 0,   /* Claude MQTT agent status (executing/idle/...) */
    EMOJI_SRC_VOICE  = 1,   /* Doubao listen/speak (highest priority)        */
    EMOJI_SRC_COUNT,
} emoji_source_t;

/**
 * Priority-arbitrated request from one status source.
 *
 * @param src   Which status feed is requesting.
 * @param name  Resource name to show (e.g. "claude-idle", "listen"), or NULL/""
 *              to withdraw this source's request. The highest-priority source
 *              with an active request owns the overlay; when none does, the
 *              overlay is released so the idle rotation can resume.
 */
void emoji_idle_request(emoji_source_t src, const char *name);

/** True while any status source (Claude or voice) currently owns the overlay. */
bool emoji_idle_status_active(void);

/**
 * Play the named emoji as a fullscreen animated overlay.
 *
 * @param parent   Parent object, typically lv_scr_act().
 * @param name     Resource name without the .webp suffix, e.g. "02".
 * @param on_exit  Called when user taps to dismiss. May be NULL.
 * @return         The overlay object, or NULL when name is unknown or creation fails.
 */
lv_obj_t *emoji_idle_play(lv_obj_t            *parent,
                           const char          *name,
                           emoji_idle_exit_cb_t on_exit);

/** Stop and remove the currently playing emoji overlay, if any. */
void emoji_idle_stop(void);

/** Return true while an emoji overlay exists. */
bool emoji_idle_is_active(void);

/** Create the next emoji in the configured resource order. */
lv_obj_t *emoji_idle_create(lv_obj_t            *parent,
                            emoji_idle_exit_cb_t on_exit);

#endif
