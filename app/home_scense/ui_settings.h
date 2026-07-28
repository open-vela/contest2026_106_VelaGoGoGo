/****************************************************************************
 * app/home_scense/ui_settings.h
 * Navigation and settings page for the home scene application.
 ****************************************************************************/

#ifndef HOME_SCENSE_UI_SETTINGS_H
#define HOME_SCENSE_UI_SETTINGS_H

#include <lvgl/lvgl.h>

void ui_settings_init(lv_obj_t *screen);
void ui_settings_show_home(void);
void ui_settings_show_settings(void);
lv_obj_t *ui_settings_home_content(void);
void ui_settings_refresh(lv_timer_t *timer);

#endif /* HOME_SCENSE_UI_SETTINGS_H */
