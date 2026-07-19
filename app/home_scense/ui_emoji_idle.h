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
 * Create a fullscreen (320×240) animated emoji overlay on `parent`.
 *
 * Each call plays the next emoji in sequence (0→1→...→12→0).
 * Timer advances keyframes at ~150ms intervals.
 *
 * @param parent   Parent object, typically lv_scr_act().
 * @param on_exit  Called when user taps to dismiss. May be NULL.
 * @return         The overlay object, or NULL on failure.
 */
lv_obj_t *emoji_idle_create(lv_obj_t            *parent,
                            emoji_idle_exit_cb_t on_exit);

#endif
