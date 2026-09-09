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

/** Create the next emoji in the configured resource order. */
lv_obj_t *emoji_idle_create(lv_obj_t            *parent,
                            emoji_idle_exit_cb_t on_exit);

#endif
