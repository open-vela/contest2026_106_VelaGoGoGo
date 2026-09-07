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
#include "ui_emoji_idle.h"
#include "ui_voice.h"
#include "ui_settings.h"
#include "ui_status_bar.h"
#include "claude_mqtt.h"
#include "ui_claude_status.h"
#include "wifi_status.h"
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE
#include "doubao/doubao_voice.h"
#endif
#include "face_detect.h"   /* defines HOME_SCENSE_FACE_DETECT_ENABLED if active */
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP
#include "wakeup/wakeup.h"
#endif

/* Observer callbacks from ui_main.c */
extern void time_observer_cb(lv_observer_t *, lv_subject_t *);
extern void date_observer_cb(lv_observer_t *, lv_subject_t *);
extern void temperature_observer_cb(lv_observer_t *, lv_subject_t *);
extern void humidity_observer_cb(lv_observer_t *, lv_subject_t *);
extern void prox_observer_cb(lv_observer_t *, lv_subject_t *);

#include <unistd.h>
#include <sys/boardctl.h>
#include <stdio.h>
#include <syslog.h>
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
 * Idle detection — 10 s no input → emoji screen
 *---------------------------------------------------------------------*/
static uint32_t g_last_activity = 0;    /* tick of last user interaction   */
static bool     g_idle_active   = false; /* emoji idle screen is showing    */

void home_record_activity(void)
{
    g_last_activity = lv_tick_get();
}

/* Callback: when user taps to exit the idle emoji screen */
static void on_idle_exit(void)
{
    g_idle_active = false;
    home_record_activity();
}

/* 1-second timer: if idle > 60 s and overlay not already active, show it */
static void idle_check_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_idle_active) return;

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE
    /* snapshot 含 assistant_text[DOUBAO_REPLY_MAX=8192] 约 9KB;放栈上会压爆
     * 100KB 主线程栈(实测崩溃)。改 static:LVGL 单线程周期回调,无并发。 */
    static doubao_voice_snapshot_t voice;
    doubao_voice_get_snapshot(&voice);
    if (voice.state == DOUBAO_VOICE_CONNECTING ||
        voice.state == DOUBAO_VOICE_RECORDING ||
        voice.state == DOUBAO_VOICE_WAITING_RESPONSE ||
        voice.state == DOUBAO_VOICE_PLAYING) return;
#endif

    uint32_t elapsed = lv_tick_elaps(g_last_activity);
    if (elapsed > 60000) {
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
#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
#  ifndef CONFIG_LVX_USE_DEMO_CONTEST2026_106_TOUCH_DEVPATH
#    define CONFIG_LVX_USE_DEMO_CONTEST2026_106_TOUCH_DEVPATH "/dev/input0"
#  endif
    info.input_path = CONFIG_LVX_USE_DEMO_CONTEST2026_106_TOUCH_DEVPATH;
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
#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
    if (!result.indev) {
        LV_LOG_ERROR("Touchscreen init failed: %s", info.input_path);
        return 1;
    }
#endif

    init_fonts();
    create_main_screen();
    ui_claude_status_init(lv_scr_act());
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE
    doubao_voice_init();
#endif
#ifdef HOME_SCENSE_FACE_DETECT_ENABLED
    /* Background frontal-face detection: opens/closes the full-duplex
     * conversation as a face enters/leaves the camera. No LCD preview. */
    if (face_detect_start() != 0)
        LV_LOG_ERROR("face_detect_start failed");
#endif
    ui_settings_init(lv_scr_act());
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE
    ui_voice_create(ui_settings_home_content());
#endif

    /* Hands-free wake word — starts the mic-listening thread (after the
     * Doubao session and voice UI exist so a wake can trigger a session) */
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP
    wakeup_init();
#endif

    /* Init subjects */
    lv_subject_init_int(&hour_subject, 0);
    lv_subject_init_int(&minute_subject, 0);
    lv_subject_init_int(&second_subject, 0);
    lv_subject_init_pointer(&week_day_name_subject, (void *)"Sunday");
    lv_subject_init_int(&month_day_subject, 1);
    lv_subject_init_pointer(&month_name_subject, (void *)"January");

    update_time_cb(NULL);

    /* Sensors */
    lv_subject_init_int(&temperature_subject, 250);
    lv_subject_init_int(&humidity_subject, 600);
    lv_subject_init_int(&prox_subject, 50);

    if (init_sensors() != 0)
        LV_LOG_ERROR("Sensor init failed");

    /* LED — init hardware */
    led_adapter_init();
    led_adapter_diagnose();

    /* Claude Code status receiver — MQTT-C client */
    syslog(LOG_INFO, "[home_scense] before claude_mqtt_init\n");
    claude_mqtt_init();
    syslog(LOG_INFO, "[home_scense] after claude_mqtt_init\n");

    /* Idle detection — reset the clock and start the 1 s check timer */
    g_last_activity = lv_tick_get();
    lv_timer_t *idle_timer = lv_timer_create(idle_check_cb, 1000, NULL);
    lv_timer_set_repeat_count(idle_timer, -1);

    /* Claude status poll — consume MQTT state on the LVGL thread */
    lv_timer_t *status_timer = lv_timer_create(
        claude_mqtt_poll, 100, NULL);
    lv_timer_set_repeat_count(status_timer, -1);

    /* Voice and system UI polling stay on the LVGL thread. */
    wifi_status_refresh(NULL);
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE
    lv_timer_t *voice_timer = lv_timer_create(ui_voice_refresh, 150, NULL);
    lv_timer_set_repeat_count(voice_timer, -1);
#endif
    lv_timer_t *wifi_timer = lv_timer_create(wifi_status_refresh, 1500, NULL);
    lv_timer_set_repeat_count(wifi_timer, -1);
    lv_timer_t *settings_timer = lv_timer_create(ui_settings_refresh, 1000, NULL);
    lv_timer_set_repeat_count(settings_timer, -1);
    lv_timer_t *bar_timer = lv_timer_create(ui_status_bar_refresh, 500, NULL);
    lv_timer_set_repeat_count(bar_timer, -1);

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

#ifdef HOME_SCENSE_FACE_DETECT_ENABLED
    face_detect_stop();
#endif
    /* Wake thread first: it calls doubao APIs and must be joined before the
     * Doubao context (mutex included) is torn down. */
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP
    wakeup_deinit();
#endif
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE
    doubao_voice_deinit();
#endif
    claude_mqtt_deinit();
    led_adapter_deinit();
    return 0;
}

#endif /* CONFIG_LVX_USE_DEMO_CONTEST2026_106_HELLO_APP */
