/****************************************************************************
 * app/home_scense/ui_voice.h
 * LVGL presentation for the Doubao push-to-talk conversation.
 ****************************************************************************/

#ifndef HOME_SCENSE_UI_VOICE_H
#define HOME_SCENSE_UI_VOICE_H

#include <lvgl/lvgl.h>

void ui_voice_create(lv_obj_t *parent);
void ui_voice_refresh(lv_timer_t *timer);

#endif /* HOME_SCENSE_UI_VOICE_H */
