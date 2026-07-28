/****************************************************************************
 * app/home_scense/ui_status_bar.c
 * Shared time, Wi-Fi, and settings controls.
 ****************************************************************************/

#include "ui_status_bar.h"
#include "main.h"
#include "wifi_status.h"

#define STATUS_BAR_MAX_INSTANCES 8
#define WIFI_BAR_COUNT 4

typedef struct status_bar_instance_s {
    lv_obj_t *time;
    lv_obj_t *wifi_bars[WIFI_BAR_COUNT];
    lv_obj_t *wifi_mark;
    status_bar_settings_cb_t settings_cb;
} status_bar_instance_t;

static status_bar_instance_t g_bars[STATUS_BAR_MAX_INSTANCES];
static unsigned int g_bar_count;

static void settings_click_cb(lv_event_t *event)
{
    status_bar_instance_t *bar = lv_event_get_user_data(event);
    if (bar && bar->settings_cb) bar->settings_cb();
}

static void set_wifi_bar_style(lv_obj_t *bar, lv_color_t color, bool active)
{
    lv_obj_set_style_bg_color(bar, color, 0);
    lv_obj_set_style_bg_opa(bar, active ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void update_wifi_icon(status_bar_instance_t *instance,
                             const wifi_status_t *status)
{
    const lv_color_t active = lv_color_hex(0x58D68D);
    const lv_color_t inactive = lv_color_hex(0x46545B);
    const lv_color_t offline = lv_color_hex(0xE74C3C);
    unsigned int i;

    if (!status->associated || !status->has_ip) {
        for (i = 0; i < WIFI_BAR_COUNT; i++) {
            set_wifi_bar_style(instance->wifi_bars[i], offline, true);
        }
        lv_label_set_text(instance->wifi_mark, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(instance->wifi_mark, offline, 0);
        return;
    }

    for (i = 0; i < WIFI_BAR_COUNT; i++) {
        set_wifi_bar_style(instance->wifi_bars[i],
                           status->has_rssi ? active : inactive,
                           status->has_rssi && i < status->signal_level);
    }
    lv_label_set_text(instance->wifi_mark, status->has_rssi ? "" : "?");
    lv_obj_set_style_text_color(instance->wifi_mark, inactive, 0);
}

lv_obj_t *ui_status_bar_create(lv_obj_t *parent,
                               status_bar_settings_cb_t settings_cb)
{
    status_bar_instance_t *instance;
    lv_obj_t *bar;
    lv_obj_t *settings;
    lv_obj_t *label;
    lv_obj_t *wifi;
    int heights[WIFI_BAR_COUNT] = {4, 7, 11, 15};
    unsigned int i;

    if (g_bar_count >= STATUS_BAR_MAX_INSTANCES) return NULL;
    instance = &g_bars[g_bar_count++];
    instance->settings_cb = settings_cb;

    bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCREEN_WIDTH, STATUS_BAR_HEIGHT);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0E1621), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    instance->time = lv_label_create(bar);
    lv_obj_align(instance->time, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_text_font(instance->time, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(instance->time, lv_color_hex(0xF7F9F9), 0);
    lv_label_set_text(instance->time, "--:--");

    settings = lv_button_create(bar);
    lv_obj_set_size(settings, 48, STATUS_BAR_HEIGHT);
    lv_obj_align(settings, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(settings, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings, 0, 0);
    lv_obj_add_event_cb(settings, settings_click_cb, LV_EVENT_CLICKED, instance);
    label = lv_label_create(settings);
    lv_label_set_text(label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF7F9F9), 0);
    lv_obj_center(label);

    wifi = lv_obj_create(bar);
    lv_obj_set_size(wifi, 30, 18);
    lv_obj_align_to(wifi, settings, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    lv_obj_set_style_bg_opa(wifi, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi, 0, 0);
    lv_obj_set_style_pad_all(wifi, 0, 0);
    lv_obj_clear_flag(wifi, LV_OBJ_FLAG_SCROLLABLE);

    for (i = 0; i < WIFI_BAR_COUNT; i++) {
        instance->wifi_bars[i] = lv_obj_create(wifi);
        lv_obj_set_size(instance->wifi_bars[i], 5, heights[i]);
        lv_obj_align(instance->wifi_bars[i], LV_ALIGN_BOTTOM_LEFT,
                     (int)i * 7, 0);
        lv_obj_set_style_radius(instance->wifi_bars[i], 1, 0);
        lv_obj_set_style_border_width(instance->wifi_bars[i], 0, 0);
        set_wifi_bar_style(instance->wifi_bars[i], lv_color_hex(0x46545B), false);
    }

    instance->wifi_mark = lv_label_create(wifi);
    lv_obj_align(instance->wifi_mark, LV_ALIGN_TOP_RIGHT, 3, -3);
    lv_obj_set_style_text_font(instance->wifi_mark, &lv_font_montserrat_12, 0);
    lv_label_set_text(instance->wifi_mark, LV_SYMBOL_CLOSE);
    update_wifi_icon(instance, &(wifi_status_t){0});
    return bar;
}

void ui_status_bar_refresh(lv_timer_t *timer)
{
    char time_text[8];
    wifi_status_t wifi;
    unsigned int i;

    (void)timer;
    wifi_status_get(&wifi);
    lv_snprintf(time_text, sizeof(time_text), "%02d:%02d",
                lv_subject_get_int(&hour_subject),
                lv_subject_get_int(&minute_subject));
    for (i = 0; i < g_bar_count; i++) {
        lv_label_set_text(g_bars[i].time, time_text);
        update_wifi_icon(&g_bars[i], &wifi);
    }
}
