/****************************************************************************
 * app/home_scense/ui_settings.c
 * Home/settings navigation with a shared status bar.
 ****************************************************************************/

#include "ui_settings.h"
#include "ui_status_bar.h"
#include "main.h"

#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *g_home_page;
static lv_obj_t *g_settings_page;
static lv_obj_t *g_light_page;
static lv_obj_t *g_about_page;
static lv_obj_t *g_temp_value;
static lv_obj_t *g_prox_value;

static void show_page(lv_obj_t *page)
{
    lv_obj_add_flag(g_home_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_light_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_about_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
}

void ui_settings_show_home(void)
{
    show_page(g_home_page);
}

void ui_settings_show_settings(void)
{
    show_page(g_settings_page);
}

lv_obj_t *ui_settings_home_content(void)
{
    return g_home_page;
}

static void back_click_cb(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_user_data(event);
    home_record_activity();
    if (target) show_page(target);
}

static void status_settings_cb(void)
{
    home_record_activity();
    ui_settings_show_settings();
}

static void light_click_cb(lv_event_t *event)
{
    (void)event;
    show_page(g_light_page);
}

static void about_click_cb(lv_event_t *event)
{
    (void)event;
    show_page(g_about_page);
}

static void led_switch_cb(lv_event_t *event)
{
    if (lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED)) {
        led_adapter_on();
    } else {
        led_adapter_off();
    }
}

static void brightness_cb(lv_event_t *event)
{
    led_adapter_set_brightness(lv_slider_get_value(lv_event_get_target(event)));
}

static lv_obj_t *create_page(lv_obj_t *screen)
{
    lv_obj_t *page = lv_obj_create(screen);
    lv_obj_set_size(page, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x17202A), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    ui_status_bar_create(page, status_settings_cb);
    return page;
}

static lv_obj_t *create_title(lv_obj_t *page, const char *title,
                              bool with_back, lv_obj_t *back_target)
{
    lv_obj_t *heading = lv_label_create(page);
    lv_obj_set_style_text_font(heading, g_misans_normal_16, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(0xF7F9F9), 0);
    lv_label_set_text(heading, title);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 10);

    if (with_back) {
        lv_obj_t *back = lv_button_create(page);
        lv_obj_set_size(back, 72, 26);
        lv_obj_align(back, LV_ALIGN_TOP_LEFT, 8, STATUS_BAR_HEIGHT + 7);
        lv_obj_set_style_bg_color(back, lv_color_hex(0x273746), 0);
        lv_obj_set_style_border_width(back, 0, 0);
        lv_obj_t *label = lv_label_create(back);
        lv_label_set_text(label, "< 返回");
        lv_obj_set_style_text_font(label, g_misans_normal_11, 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(back, back_click_cb, LV_EVENT_CLICKED, back_target);
    }
    return heading;
}

static lv_obj_t *create_row(lv_obj_t *page, int y, const char *name,
                            const char *detail, bool clickable,
                            lv_event_cb_t callback)
{
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_set_size(row, SCREEN_WIDTH - 20, 34);
    lv_obj_set_pos(row, 10, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x273746), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_all(row, 5, 0);

    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_font(name_label, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xF7F9F9), 0);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t *detail_label = lv_label_create(row);
    lv_label_set_text(detail_label, detail);
    lv_obj_set_style_text_font(detail_label, g_misans_normal_11, 0);
    lv_obj_set_style_text_color(detail_label, lv_color_hex(0xAEB6BF), 0);
    lv_obj_align(detail_label, LV_ALIGN_RIGHT_MID, clickable ? -18 : -4, 0);

    if (clickable) {
        lv_obj_t *arrow = lv_label_create(row);
        lv_label_set_text(arrow, ">");
        lv_obj_set_style_text_font(arrow, g_misans_normal_12, 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, NULL);
    }
    return detail_label;
}

void ui_settings_init(lv_obj_t *screen)
{
    lv_obj_t *switch_obj;
    lv_obj_t *slider;
    lv_obj_t *content;

    g_home_page = create_page(screen);
    g_settings_page = create_page(screen);
    g_light_page = create_page(screen);
    g_about_page = create_page(screen);

    create_title(g_settings_page, "设置", true, g_home_page);
    g_temp_value = create_row(g_settings_page, 68, "T&H", "--.- C  --.-%", false, NULL);
    (void)create_row(g_settings_page, 108, "Light", "灯光控制", true, light_click_cb);
    g_prox_value = create_row(g_settings_page, 148, "PROX", "--.- cm", false, NULL);
    (void)create_row(g_settings_page, 188, "About", "设备信息", true, about_click_cb);

    create_title(g_light_page, "灯光控制", true, g_settings_page);
    switch_obj = lv_switch_create(g_light_page);
    lv_obj_align(switch_obj, LV_ALIGN_TOP_LEFT, 40, 78);
    if (g_led_is_on) lv_obj_add_state(switch_obj, LV_STATE_CHECKED);
    lv_obj_add_event_cb(switch_obj, led_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    content = lv_label_create(g_light_page);
    lv_label_set_text(content, "灯光开关");
    lv_obj_set_style_text_font(content, g_misans_normal_12, 0);
    lv_obj_align_to(content, switch_obj, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    slider = lv_slider_create(g_light_page);
    lv_obj_set_width(slider, SCREEN_WIDTH - 80);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 140);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, g_led_brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    content = lv_label_create(g_light_page);
    lv_label_set_text(content, "亮度");
    lv_obj_set_style_text_font(content, g_misans_normal_12, 0);
    lv_obj_align_to(content, slider, LV_ALIGN_OUT_TOP_LEFT, 0, -10);

    create_title(g_about_page, "关于设备", true, g_settings_page);
    content = lv_label_create(g_about_page);
    lv_label_set_text(content,
                      "Gemini-S1 开发板\n\n"
                      "R528 双核 ARM Cortex-A7\n"
                      "麦克风阵列与 PCM 音频\n"
                      "显示屏、Wi-Fi、蓝牙\n"
                      "GPIO / I2C / SPI / UART / ADC\n\n"
                      "openVela AI 硬件大赛作品");
    lv_obj_set_width(content, SCREEN_WIDTH - 38);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 20, 72);
    lv_obj_set_style_text_font(content, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(content, lv_color_hex(0xD5DBDB), 0);

    ui_settings_show_home();
}

void ui_settings_refresh(lv_timer_t *timer)
{
    char text[48];
    int temperature;
    int humidity;
    int proximity;

    (void)timer;
    if (!g_temp_value || !g_prox_value) return;
    temperature = lv_subject_get_int(&temperature_subject);
    humidity = lv_subject_get_int(&humidity_subject);
    proximity = lv_subject_get_int(&prox_subject);
    lv_snprintf(text, sizeof(text), "%d.%d C  %d.%d%%",
                temperature / 10, abs(temperature % 10),
                humidity / 10, abs(humidity % 10));
    lv_label_set_text(g_temp_value, text);
    lv_snprintf(text, sizeof(text), "%d.%d cm", proximity / 10,
                abs(proximity % 10));
    lv_label_set_text(g_prox_value, text);
}
