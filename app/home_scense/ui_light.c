/****************************************************************************
 * app/home_scense/ui_light.c
 * Light-control popup window (LED switch + brightness slider).
 ****************************************************************************/

#include "main.h"
#include "led_control.h"
#include "ui_status.h"

#include <stdio.h>
#include <unistd.h>

/* Internal forward refs */
static void close_light_window_cb(lv_event_t *e);
static void light_switch_event_cb(lv_event_t *e);
static void brightness_slider_event_cb(lv_event_t *e);

/* LED adapter wrappers */
void led_adapter_init(void)
{
    led_error_t err = led_controller_init();
    if (err != LED_SUCCESS) {
        LV_LOG_ERROR("LED init failed: %s", led_get_error_string(err));
        return;
    }
    g_led_is_on      = true;   /* default ON for claude_status */
    g_led_brightness = 50;
    led_set_color(LED_COLOR_WHITE);
    led_set_brightness(g_led_brightness);
}

void led_adapter_deinit(void)
{
    led_adapter_off();
    led_controller_deinit();
}

void led_adapter_on(void)
{
    g_led_is_on = true;
    claude_status_reapply();   /* show current claude status */
}

void led_adapter_off(void)
{
    g_led_is_on = false;
    int fd = open("/dev/leds0", O_RDWR);
    if (fd >= 0) {
        uint32_t off = 0;
        write(fd, &off, sizeof(off));
        close(fd);
    }
}

void led_adapter_set_brightness(int32_t brightness)
{
    if (brightness < 0 || brightness > 100) return;
    g_led_brightness = brightness;
    if (g_led_is_on) {
        claude_status_reapply();
    }
}

void led_adapter_diagnose(void)
{
    LV_LOG_INFO("=== LED Diagnosis ===");
    bool state = false;
    led_is_on(&state);
    LV_LOG_INFO("LED is on: %s  brightness: %" PRId32, state ? "YES" : "NO", g_led_brightness);
    led_status_t st;
    if (led_get_status(&st) == LED_SUCCESS) {
        LV_LOG_INFO("Color: 0x%06X (%s)  mode: %d",
                    st.color, led_get_color_name(st.color), (int)st.mode);
    }
}

/*-----------------------------------------------------------------------
 * create_light_control_window
 *---------------------------------------------------------------------*/
void create_light_control_window(void)
{
    if (light_window) return;

    lv_coord_t left = (SCREEN_WIDTH  - 240) / 2;
    lv_coord_t top  = (SCREEN_HEIGHT - 160) / 2;

    light_window = lv_obj_create(lv_scr_act());
    lv_obj_set_size(light_window, 240, 160);
    lv_obj_set_pos(light_window, left, top);
    lv_obj_set_style_bg_color(light_window, lv_color_hex(0x2C3E50), 0);
    lv_obj_set_style_bg_opa(light_window, LV_OPA_90, 0);
    lv_obj_set_style_radius(light_window, 15, 0);
    lv_obj_set_style_border_width(light_window, 3, 0);
    lv_obj_set_style_border_color(light_window, lv_color_hex(0x3498DB), 0);
    lv_obj_set_style_shadow_width(light_window, 30, 0);
    lv_obj_set_style_shadow_color(light_window, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(light_window, LV_OPA_70, 0);
    lv_obj_set_style_shadow_spread(light_window, 5, 0);

    /* Close button */
    lv_obj_t *btn = lv_btn_create(light_window);
    lv_obj_set_size(btn, 60, 30);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xE74C3C), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 5, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Close");
    lv_obj_set_style_text_font(lbl, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, close_light_window_cb, LV_EVENT_CLICKED, NULL);

    /* Title */
    lv_obj_t *title = lv_label_create(light_window);
    lv_label_set_text(title, "Light Control");
    lv_obj_set_style_text_font(title, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xECF0F1), 0);
    lv_obj_align(title, LV_ALIGN_TOP_RIGHT, 0, 15);

    /* Switch */
    light_switch = lv_switch_create(light_window);
    lv_obj_align(light_switch, LV_ALIGN_TOP_LEFT, 30, 50);
    if (g_led_is_on) lv_obj_add_state(light_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(light_switch, light_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *sw_label = lv_label_create(light_window);
    lv_label_set_text(sw_label, "Light Switch");
    lv_obj_set_style_text_font(sw_label, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(sw_label, lv_color_hex(0xBDC3C7), 0);
    lv_obj_align_to(sw_label, light_switch, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    /* Brightness slider */
    brightness_slider = lv_slider_create(light_window);
    lv_obj_set_width(brightness_slider, 180);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 110);
    lv_slider_set_range(brightness_slider, 0, 100);
    lv_slider_set_value(brightness_slider, g_led_brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    brightness_label = lv_label_create(light_window);
    char buf[32];
    lv_snprintf(buf, sizeof(buf), "Brightness: %" PRId32 "%%", g_led_brightness);
    lv_label_set_text(brightness_label, buf);
    lv_obj_set_style_text_font(brightness_label, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(brightness_label, lv_color_hex(0x95A5A6), 0);
    lv_obj_align_to(brightness_label, brightness_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
}

/*-----------------------------------------------------------------------
 * Callbacks
 *---------------------------------------------------------------------*/
static void close_light_window_cb(lv_event_t *e)
{
    (void)e;
    if (light_window) {
        lv_obj_del(light_window);
        light_window = brightness_slider = brightness_label = NULL;
        light_switch = NULL;
    }
    if (gamble_window) {
        lv_obj_del(gamble_window);
        gamble_window = NULL;
    }
}

static void light_switch_event_cb(lv_event_t *e)
{
    if (lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)) {
        led_adapter_on();
        claude_status_reapply();   /* let claude_status take over */
    } else {
        led_adapter_off();
    }
}

static void brightness_slider_event_cb(lv_event_t *e)
{
    int32_t val = lv_slider_get_value(lv_event_get_target(e));
    led_adapter_set_brightness(val);
    char buf[32];
    lv_snprintf(buf, sizeof(buf), "Brightness: %" PRId32 "%%", val);
    lv_label_set_text(brightness_label, buf);
}
