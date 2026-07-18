/****************************************************************************
 * app/home_scense/main.c
 * Entry point for home_scense—the team 106 LVGL application.
 *
 * Based on OpenVela luncher_mini, refactored into modular sources.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_HELLO_APP

#include "main.h"
#include "sensor.h"
#include "led_control.h"

/* Observer callbacks from ui_main.c */
extern void time_observer_cb(lv_observer_t *, lv_subject_t *);
extern void date_observer_cb(lv_observer_t *, lv_subject_t *);
extern void temperature_observer_cb(lv_observer_t *, lv_subject_t *);
extern void humidity_observer_cb(lv_observer_t *, lv_subject_t *);
extern void prox_observer_cb(lv_observer_t *, lv_subject_t *);

#include <unistd.h>
#include <sys/boardctl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*-----------------------------------------------------------------------
 * Board init
 *---------------------------------------------------------------------*/
#undef NEED_BOARDINIT
#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

/*-----------------------------------------------------------------------
 * libuv event loop (when enabled)
 *---------------------------------------------------------------------*/
#ifdef CONFIG_LV_USE_NUTTX_LIBUV
void lv_nuttx_uv_loop(uv_loop_t *loop, lv_nuttx_result_t *result)
{
    lv_nuttx_uv_t uv_info;
    void         *data;

    uv_loop_init(loop);

    lv_memset(&uv_info, 0, sizeof(uv_info));
    uv_info.loop  = loop;
    uv_info.disp  = result->disp;
    uv_info.indev = result->indev;
#ifdef CONFIG_UINPUT_TOUCH
    uv_info.uindev = result->utouch_indev;
#endif

    data = lv_nuttx_uv_init(&uv_info);
    uv_run(loop, UV_RUN_DEFAULT);
    lv_nuttx_uv_deinit(&data);
}
#endif

/*-----------------------------------------------------------------------
 * Global variables (defined here, declared extern in main.h)
 *---------------------------------------------------------------------*/

/* --- Fonts --- */
lv_font_t *g_misans_normal_11;
lv_font_t *g_misans_normal_12;
lv_font_t *g_misans_normal_16;
lv_font_t *g_misans_normal_32;

/* --- LVGL subjects --- */
lv_subject_t hour_subject;
lv_subject_t minute_subject;
lv_subject_t second_subject;
lv_subject_t week_day_name_subject;
lv_subject_t month_day_subject;
lv_subject_t month_name_subject;
lv_subject_t temperature_subject;
lv_subject_t humidity_subject;
lv_subject_t prox_subject;

/* --- UI objects --- */
lv_obj_t *time_label;
lv_obj_t *date_label;
lv_obj_t *window[4];
lv_obj_t *bg_img;
lv_obj_t *temp_label;
lv_obj_t *humidity_label;
lv_obj_t *prox_label;

/* --- Window pointers --- */
lv_obj_t *light_window;
lv_obj_t *light_switch;
lv_obj_t *brightness_slider;
lv_obj_t *brightness_label;
bool      light_initialized;
lv_obj_t *gamble_window;

/* --- LED state --- */
bool    g_led_is_on;
int32_t g_led_brightness;

/*-----------------------------------------------------------------------
 * Font loading
 *---------------------------------------------------------------------*/
void init_fonts(void)
{
#ifdef LV_USE_FREETYPE
    g_misans_normal_11 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 11, LV_FREETYPE_FONT_STYLE_NORMAL);
    g_misans_normal_12 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 12, LV_FREETYPE_FONT_STYLE_NORMAL);
    g_misans_normal_16 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 16, LV_FREETYPE_FONT_STYLE_NORMAL);
    g_misans_normal_32 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 32, LV_FREETYPE_FONT_STYLE_NORMAL);

    if (!g_misans_normal_11) g_misans_normal_11 = &lv_font_montserrat_12;
    if (!g_misans_normal_12) g_misans_normal_12 = &lv_font_montserrat_12;
    if (!g_misans_normal_16) g_misans_normal_16 = &lv_font_montserrat_16;
    if (!g_misans_normal_32) g_misans_normal_32 = &lv_font_montserrat_30;
#else
    g_misans_normal_11 = &lv_font_simsun_16_cjk;
    g_misans_normal_12 = &lv_font_simsun_16_cjk;
    g_misans_normal_16 = &lv_font_simsun_16_cjk;
    g_misans_normal_32 = &lv_font_montserrat_30;
#endif
}

/*-----------------------------------------------------------------------
 * Time update
 *---------------------------------------------------------------------*/
static void update_time_cb(lv_timer_t *timer)
{
    (void)timer;
    time_t    now;
    struct tm local;
    time(&now);
    struct tm *utc = gmtime(&now);
    if (!utc) return;
    local = *utc;
    local.tm_hour += 8;
    mktime(&local);

    lv_subject_set_int(&hour_subject, local.tm_hour);
    lv_subject_set_int(&minute_subject, local.tm_min);
    lv_subject_set_int(&second_subject, local.tm_sec);

    static const char *wd[7] = { "Sunday","Monday","Tuesday","Wednesday",
                                  "Thursday","Friday","Saturday" };
    static const char *mo[12]= { "January","February","March","April","May","June",
                                  "July","August","September","October","November","December" };
    if (local.tm_wday < 7 && local.tm_mon < 12) {
        lv_subject_set_pointer(&week_day_name_subject, (void *)wd[local.tm_wday]);
        lv_subject_set_int(&month_day_subject, local.tm_mday);
        lv_subject_set_pointer(&month_name_subject, (void *)mo[local.tm_mon]);
    }
}

/*-----------------------------------------------------------------------
 * sensor_init() adapter—calls sensor.c then creates timer
 *---------------------------------------------------------------------*/
int init_sensors(void)
{
    if (sensor_init() != 0) return -1;
    lv_timer_t *t = sensor_timer_create();
    if (!t) return -1;
    return 0;
}

/*-----------------------------------------------------------------------
 * main
 *---------------------------------------------------------------------*/
int main(int argc, FAR char *argv[])
{
    (void)argc; (void)argv;

    if (lv_is_initialized()) {
        LV_LOG_ERROR("LVGL already initialized");
        return -1;
    }

#ifdef NEED_BOARDINIT
    boardctl(BOARDIOC_INIT, 0);
#endif

    /* LVGL + NuttX backend */
    lv_init();
    lv_nuttx_dsc_t  info;
    lv_nuttx_result_t result;
    lv_nuttx_dsc_init(&info);
#ifdef CONFIG_LV_USE_NUTTX_LCD
    info.fb_path = "/dev/lcd0";
#endif
#ifdef CONFIG_INPUT_TOUCHSCREEN
    info.input_path = CONFIG_EXAMPLES_LVGLDEMO_INPUT_DEVPATH;
#endif
#ifdef CONFIG_LV_USE_NUTTX_LIBUV
    uv_loop_t ui_loop;
    lv_memzero(&ui_loop, sizeof(ui_loop));
#endif
    lv_nuttx_init(&info, &result);
    usleep(100000);
    if (!result.disp) {
        LV_LOG_ERROR("Display init failed");
        return 1;
    }

    init_fonts();
    create_main_screen();

    /* Init subjects */
    lv_subject_init_int(&hour_subject, 0);
    lv_subject_init_int(&minute_subject, 0);
    lv_subject_init_int(&second_subject, 0);
    lv_subject_init_pointer(&week_day_name_subject, (void *)"Sunday");
    lv_subject_init_int(&month_day_subject, 1);
    lv_subject_init_pointer(&month_name_subject, (void *)"January");

    lv_subject_add_observer_obj(&hour_subject, time_observer_cb, time_label, NULL);
    lv_subject_add_observer_obj(&minute_subject, time_observer_cb, time_label, NULL);
    lv_subject_add_observer_obj(&second_subject, time_observer_cb, time_label, NULL);
    lv_subject_add_observer_obj(&week_day_name_subject, date_observer_cb, date_label, NULL);
    lv_subject_add_observer_obj(&month_day_subject, date_observer_cb, date_label, NULL);
    lv_subject_add_observer_obj(&month_name_subject, date_observer_cb, date_label, NULL);

    update_time_cb(NULL);

    /* Sensors */
    lv_subject_init_int(&temperature_subject, 250);
    lv_subject_init_int(&humidity_subject, 600);
    lv_subject_init_int(&prox_subject, 50);

    lv_subject_add_observer_obj(&temperature_subject, temperature_observer_cb, temp_label, NULL);
    lv_subject_add_observer_obj(&humidity_subject, humidity_observer_cb, humidity_label, NULL);
    lv_subject_add_observer_obj(&prox_subject, prox_observer_cb, prox_label, NULL);

    if (init_sensors() != 0)
        LV_LOG_ERROR("Sensor init failed");

    /* LED */
    led_adapter_init();
    led_adapter_diagnose();
    /* Quick LED blink test */
    led_adapter_on();
    usleep(200000);
    led_adapter_off();

    /* Time timer */
    lv_timer_t *tt = lv_timer_create(update_time_cb, 1000, NULL);
    if (tt) lv_timer_set_repeat_count(tt, -1);

    /* Main loop */
#ifdef CONFIG_LV_USE_NUTTX_LIBUV
    lv_nuttx_uv_loop(&ui_loop, &result);
#else
    while (1) {
        uint32_t idle = lv_timer_handler();
        usleep(idle ? idle * 1000 : 5000);
    }
#endif

    led_adapter_deinit();
    return 0;
}

#endif /* CONFIG_LVX_USE_DEMO_CONTEST2026_106_HELLO_APP */
