/****************************************************************************
 * app/home_scense/ui_main.c
 * Main screen: background, time/date, and four app-launch tiles.
 *
 * Originally part of OpenVela luncher_mini.c.
 ****************************************************************************/

#include "main.h"
#include "ui_emoji.h"

#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

/*-----------------------------------------------------------------------
 * Observer callbacks (non-static — used by main.c)
 *---------------------------------------------------------------------*/
void time_observer_cb(lv_observer_t *obs, lv_subject_t *sub)
{
    (void)sub;
    lv_obj_t *label = lv_observer_get_target_obj(obs);
    char buf[16];
    lv_snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                lv_subject_get_int(&hour_subject),
                lv_subject_get_int(&minute_subject),
                lv_subject_get_int(&second_subject));
    lv_label_set_text(label, buf);
}

void date_observer_cb(lv_observer_t *obs, lv_subject_t *sub)
{
    (void)sub;
    lv_obj_t *label = lv_observer_get_target_obj(obs);
    char buf[32];
    lv_snprintf(buf, sizeof(buf), "%s, %d %s",
                (const char *)lv_subject_get_pointer(&week_day_name_subject),
                lv_subject_get_int(&month_day_subject),
                (const char *)lv_subject_get_pointer(&month_name_subject));
    lv_label_set_text(label, buf);
}

/* Sensor observer callbacks (display labels live in ui_main's screen, but
 * subjects are updated by sensor.c) */
void temperature_observer_cb(lv_observer_t *obs, lv_subject_t *sub)
{
    (void)sub;
    lv_obj_t *label = lv_observer_get_target_obj(obs);
    int v = lv_subject_get_int(&temperature_subject);
    char buf[32];
    lv_snprintf(buf, sizeof(buf), "T:%d.%d C", v / 10, abs(v % 10));
    lv_label_set_text(label, buf);
}

void humidity_observer_cb(lv_observer_t *obs, lv_subject_t *sub)
{
    (void)sub;
    lv_obj_t *label = lv_observer_get_target_obj(obs);
    int v = lv_subject_get_int(&humidity_subject);
    char buf[32];
    lv_snprintf(buf, sizeof(buf), "P:%d.%d%%", v / 10, abs(v % 10));
    lv_label_set_text(label, buf);
}

void prox_observer_cb(lv_observer_t *obs, lv_subject_t *sub)
{
    (void)sub;
    lv_obj_t *label = lv_observer_get_target_obj(obs);
    int v = lv_subject_get_int(&prox_subject);
    char buf[32];
    if (v < 0) {
        lv_snprintf(buf, sizeof(buf), "--.- cm");
    } else {
        lv_snprintf(buf, sizeof(buf), "%.1f cm", v / 10.0f);
    }
    lv_label_set_text(label, buf);
}

/*-----------------------------------------------------------------------
 * Background image
 *---------------------------------------------------------------------*/
static lv_obj_t *load_background(lv_obj_t *parent)
{
    lv_obj_t *img = lv_image_create(parent);
    const char *path = "/resource/imgs/luncher_mini_bg_new.png";
    if (access(path, F_OK) == 0) {
        lv_image_set_src(img, path);
    } else {
        lv_obj_set_style_bg_color(img, lv_color_hex(0x1C2833), 0);
        lv_obj_set_style_bg_opa(img, LV_OPA_COVER, 0);
    }
    lv_obj_set_size(img, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(img, 0, 0);
    return img;
}

/*-----------------------------------------------------------------------
 * Click feedback
 *---------------------------------------------------------------------*/
static void restore_color_cb(lv_timer_t *timer)
{
    lv_obj_t *win = (lv_obj_t *)lv_timer_get_user_data(timer);
    lv_obj_set_style_bg_color(win, lv_color_hex(0x2C3E50), 0);
    lv_obj_set_style_bg_opa(win, LV_OPA_70, 0);
    lv_timer_del(timer);
}

static void window_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *obj = lv_event_get_target(e);

    for (int i = 0; i < 4; i++) {
        if (obj != window[i]) continue;

        lv_color_t orig = lv_obj_get_style_bg_color(obj, 0);
        lv_color_t bright = lv_color_make(
            LV_MIN(orig.red   + 30, 255),
            LV_MIN(orig.green + 30, 255),
            LV_MIN(orig.blue  + 30, 255));
        lv_obj_set_style_bg_color(obj, bright, 0);
        lv_timer_t *t = lv_timer_create(restore_color_cb, 300, NULL);
        lv_timer_set_user_data(t, obj);
        lv_timer_set_repeat_count(t, 1);

        if (i == 1) {          /* Light */
            if (gamble_window) { lv_obj_del(gamble_window); gamble_window = NULL; }
            create_light_control_window();
        } else if (i == 3) {   /* About */
            if (light_window) {
                bool was_on = light_switch && lv_obj_has_state(light_switch, LV_STATE_CHECKED);
                lv_obj_del(light_window); light_window = NULL;
                light_switch = brightness_slider = brightness_label = NULL;
                if (was_on) led_blink_resume();
            }
            create_gamble_info_window();
        }
        break;
    }
}

/*-----------------------------------------------------------------------
 * create_main_screen
 *---------------------------------------------------------------------*/
void create_main_screen(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* Background */
    bg_img = load_background(scr);

    /* Transparent container */
    lv_obj_t *area = lv_obj_create(scr);
    lv_obj_set_size(area, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(area, 0, 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_0, 0);
    lv_obj_set_style_border_width(area, 0, 0);
    lv_obj_set_style_radius(area, 0, 0);

    /* Time & date area */
    lv_obj_t *dt = lv_obj_create(area);
    lv_obj_set_size(dt, SCREEN_WIDTH, 100);
    lv_obj_align(dt, LV_ALIGN_TOP_LEFT, 5, 15);
    lv_obj_set_style_bg_opa(dt, LV_OPA_0, 0);
    lv_obj_set_style_border_width(dt, 0, 0);

    time_label = lv_label_create(dt);
    lv_obj_set_style_text_font(time_label, g_misans_normal_32, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, -8, 10);

    date_label = lv_label_create(dt);
    lv_obj_set_style_text_font(date_label, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(date_label, time_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    /* Animated emoji — placed on scr at a fixed position to avoid any clipping */
    lv_obj_t *emoji = emoji_create(scr);
    lv_obj_set_pos(emoji, 110, 18);  /* right of the time */
    lv_obj_move_foreground(emoji);

    /* Four launch tiles */
    const char *titles[4] = { "T&H", "Light", "Prox", "About" };
    int ww = 60, wh = 50, sx = 5, sy = 140;

    for (int i = 0; i < 4; i++) {
        window[i] = lv_obj_create(area);
        lv_obj_set_size(window[i], ww, wh);
        lv_obj_set_pos(window[i], sx + i * (ww + 10), sy);
        lv_obj_set_style_bg_color(window[i], lv_color_hex(0x2C3E50), 0);
        lv_obj_set_style_bg_opa(window[i], LV_OPA_70, 0);
        lv_obj_set_style_radius(window[i], 8, 0);
        lv_obj_set_style_border_width(window[i], 0, 0);

        if (i == 0) {
            temp_label = lv_label_create(window[i]);
            lv_label_set_text(temp_label, "T:25.0 C");
            lv_obj_set_style_text_font(temp_label, g_misans_normal_11, 0);
            lv_obj_set_style_text_color(temp_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_align(temp_label, LV_ALIGN_TOP_MID, 0, 10);

            humidity_label = lv_label_create(window[i]);
            lv_label_set_text(humidity_label, "P:60.0%");
            lv_obj_set_style_text_font(humidity_label, g_misans_normal_11, 0);
            lv_obj_set_style_text_color(humidity_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_align(humidity_label, LV_ALIGN_BOTTOM_MID, 0, -10);
        } else if (i == 2) {
            lv_obj_t *t = lv_label_create(window[i]);
            lv_label_set_text(t, "PROX");
            lv_obj_set_style_text_font(t, g_misans_normal_11, 0);
            lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
            lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);

            prox_label = lv_label_create(window[i]);
            lv_label_set_text(prox_label, "5.0 cm");
            lv_obj_set_style_text_font(prox_label, g_misans_normal_11, 0);
            lv_obj_set_style_text_color(prox_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_align(prox_label, LV_ALIGN_BOTTOM_MID, 0, -10);
        } else {
            lv_obj_t *t = lv_label_create(window[i]);
            lv_label_set_text(t, titles[i]);
            lv_obj_set_style_text_font(t, g_misans_normal_11, 0);
            lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
            lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
        }
        lv_obj_add_event_cb(window[i], window_click_cb, LV_EVENT_CLICKED, NULL);
    }
}
