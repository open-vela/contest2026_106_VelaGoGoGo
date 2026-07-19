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
#include "ui_emoji.h"
#include "ui_emoji_idle.h"

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

/* --- LED blink state --- */
static lv_timer_t *g_blink_timer = NULL;
bool        g_blink_enabled = false;  /* master: switch ON → true */
static bool        g_blink_paused  = false;  /* popup open → true */
static bool        g_blink_high = true;      /* true→brightness 100, false→10 */
static uint32_t    g_blink_color = 0xFF0000; /* current blink color (red) */

/*-----------------------------------------------------------------------
 * Idle detection — 10 s no input → emoji screen
 *---------------------------------------------------------------------*/
static uint32_t g_last_activity = 0;    /* tick of last user interaction   */
static bool     g_idle_active   = false; /* emoji idle screen is showing    */

/* Callback: when user taps to exit the idle emoji screen */
static void on_idle_exit(void)
{
    g_idle_active   = false;
    g_last_activity = lv_tick_get();
}

/* 1-second timer: if idle > 10 s and overlay not already active, show it */
static void idle_check_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_idle_active) return;

    uint32_t elapsed = lv_tick_elaps(g_last_activity);
    if (elapsed > 10000) {
        g_idle_active = true;

        lv_obj_t *overlay = emoji_idle_create(lv_scr_act(), on_idle_exit);
        if (!overlay) {
            g_idle_active = false;
        }
    }
}

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
 * LED blink — 30ms timer toggles brightness 10↔100 for flicker effect
 *---------------------------------------------------------------------*/
static void led_blink_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!g_blink_enabled || g_blink_paused) return;
    g_blink_high = !g_blink_high;
    led_set_brightness(g_blink_high ? 100 : 10);
}

/* Called by ui_emoji when expression changes — only takes effect if blink is active */
static void led_blink_on_emoji_change(uint32_t color)
{
    if (!g_blink_enabled || g_blink_paused) {
        g_blink_color = color;   /* remember for when blink resumes */
        return;
    }
    g_blink_color = color;
    g_blink_high  = true;
    led_set_color(color);
    led_set_brightness(100);
    if (!g_led_is_on) led_on();
}

/* Called when user toggles the Light switch ON */
void led_blink_start(void)
{
    g_blink_enabled = true;
    g_blink_high    = true;
    g_blink_color   = emoji_get_current_color();
    led_set_color(g_blink_color);
    led_set_brightness(100);
    led_on();
    g_led_is_on = true;
}

/* Called when user toggles the Light switch OFF */
void led_blink_stop(void)
{
    g_blink_enabled = false;
    led_off();
    g_led_is_on = false;
}

/* Popup opened — pause blinking so manual slider works */
void led_blink_pause(void)
{
    g_blink_paused = true;
    led_off();
    g_led_is_on = false;
}

/* Popup closed — resume only if switch is still ON */
void led_blink_resume(void)
{
    g_blink_paused = false;
    if (g_blink_enabled) {
        g_blink_high = true;
        led_set_color(g_blink_color);
        led_set_brightness(100);
        led_on();
        g_led_is_on = true;
    }
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

    /* LED — init hardware only; blink starts when user toggles switch ON */
    led_adapter_init();
    led_adapter_diagnose();

    /* Register: when emoji changes expression → LED changes color (if blink active) */
    emoji_register_color_callback(led_blink_on_emoji_change);

    /* 30ms blink timer — always running, gated by g_blink_enabled && !g_blink_paused */
    g_blink_timer = lv_timer_create(led_blink_cb, 30, NULL);
    lv_timer_set_repeat_count(g_blink_timer, -1);

    /* Idle detection — reset the clock and start the 1 s check timer */
    g_last_activity = lv_tick_get();
    lv_timer_t *idle_timer = lv_timer_create(idle_check_cb, 1000, NULL);
    lv_timer_set_repeat_count(idle_timer, -1);

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
