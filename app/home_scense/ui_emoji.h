/****************************************************************************
 * app/home_scense/ui_emoji.h
 * Animated emoji face — cycles through smile/laugh/wink/roll/cool.
 ****************************************************************************/

#ifndef HOME_SCENSE_UI_EMOJI_H
#define HOME_SCENSE_UI_EMOJI_H

#include <lvgl/lvgl.h>
#include <stdint.h>

/* Callback type: invoked when emoji expression changes, passing the
 * corresponding LED color (24-bit RGB). */
typedef void (*emoji_expr_changed_cb_t)(uint32_t color);

lv_obj_t *emoji_create(lv_obj_t *parent);

/* Register a callback that fires every time the emoji loops to the
 * next expression. Pass NULL to unregister. */
void emoji_register_color_callback(emoji_expr_changed_cb_t cb);

/* Return the LED color for the currently-active expression. */
uint32_t emoji_get_current_color(void);

#endif
