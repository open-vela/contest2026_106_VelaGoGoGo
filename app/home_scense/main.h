/****************************************************************************
 * app/home_scense/main.h
 * Shared declarations for home_scense application.
 ****************************************************************************/

#ifndef HOME_SCENSE_MAIN_H
#define HOME_SCENSE_MAIN_H

#include <lvgl/lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
#include <uv.h>
#endif

/* Screen dimensions */
#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240

/* Fonts */
extern lv_font_t *g_misans_normal_11;
extern lv_font_t *g_misans_normal_12;
extern lv_font_t *g_misans_normal_16;
extern lv_font_t *g_misans_normal_32;

/* LVGL subjects */
extern lv_subject_t hour_subject;
extern lv_subject_t minute_subject;
extern lv_subject_t second_subject;
extern lv_subject_t week_day_name_subject;
extern lv_subject_t month_day_subject;
extern lv_subject_t month_name_subject;
extern lv_subject_t temperature_subject;
extern lv_subject_t humidity_subject;
extern lv_subject_t prox_subject;

/* Shared UI objects */
extern lv_obj_t *time_label;
extern lv_obj_t *date_label;
extern lv_obj_t *window[4];
extern lv_obj_t *bg_img;
extern lv_obj_t *temp_label;
extern lv_obj_t *humidity_label;
extern lv_obj_t *prox_label;

/* Window objects */
extern lv_obj_t *light_window;
extern lv_obj_t *light_switch;
extern lv_obj_t *brightness_slider;
extern lv_obj_t *brightness_label;
extern bool       light_initialized;
extern lv_obj_t *gamble_window;

/* LED state */
extern bool    g_led_is_on;
extern int32_t g_led_brightness;

/* Function declarations */
void init_fonts(void);
int  init_sensors(void);
void home_record_activity(void);

void create_main_screen(void);
void create_light_control_window(void);
void create_gamble_info_window(void);

void led_adapter_init(void);
void led_adapter_deinit(void);
void led_adapter_on(void);
void led_adapter_off(void);
void led_adapter_set_brightness(int32_t brightness);
void led_adapter_diagnose(void);

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
void lv_nuttx_uv_loop(uv_loop_t *loop, lv_nuttx_result_t *result);
#endif

#endif /* HOME_SCENSE_MAIN_H */
