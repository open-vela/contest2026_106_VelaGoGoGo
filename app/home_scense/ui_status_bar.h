/****************************************************************************
 * app/home_scense/ui_status_bar.h
 * Shared fixed status bar for every normal application page.
 ****************************************************************************/

#ifndef HOME_SCENSE_UI_STATUS_BAR_H
#define HOME_SCENSE_UI_STATUS_BAR_H

#include <lvgl/lvgl.h>

#define STATUS_BAR_HEIGHT 24

typedef void (*status_bar_settings_cb_t)(void);

lv_obj_t *ui_status_bar_create(lv_obj_t *parent,
                               status_bar_settings_cb_t settings_cb);
void ui_status_bar_refresh(lv_timer_t *timer);

#endif /* HOME_SCENSE_UI_STATUS_BAR_H */
