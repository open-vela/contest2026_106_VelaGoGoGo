/****************************************************************************
 * app/home_scense/ui_about.c
 * About popup window showing Gemini-S1 board information.
 *
 * Originally part of OpenVela luncher_mini.c.
 ****************************************************************************/

#include "main.h"

static void close_gamble_window_cb(lv_event_t *e)
{
    (void)e;
    if (gamble_window) {
        lv_obj_del(gamble_window);
        gamble_window = NULL;
    }
    if (light_window) {
        lv_obj_del(light_window);
        light_window = NULL;
        light_switch = brightness_slider = brightness_label = NULL;
    }
}

void create_gamble_info_window(void)
{
    if (gamble_window) return;

    lv_coord_t left = (SCREEN_WIDTH  - 260) / 2;
    lv_coord_t top  = (SCREEN_HEIGHT - 200) / 2;

    gamble_window = lv_obj_create(lv_scr_act());
    lv_obj_set_size(gamble_window, 260, 200);
    lv_obj_set_pos(gamble_window, left, top);
    lv_obj_set_style_bg_color(gamble_window, lv_color_hex(0x2C3E50), 0);
    lv_obj_set_style_bg_opa(gamble_window, LV_OPA_90, 0);
    lv_obj_set_style_radius(gamble_window, 10, 0);
    lv_obj_set_style_border_width(gamble_window, 1, 0);
    lv_obj_set_style_border_color(gamble_window, lv_color_hex(0x3498DB), 0);
    lv_obj_set_style_pad_all(gamble_window, 0, 0);
    lv_obj_set_flex_flow(gamble_window, LV_FLEX_FLOW_COLUMN);

    /* Header */
    lv_obj_t *hdr = lv_obj_create(gamble_window);
    lv_obj_set_size(hdr, LV_PCT(100), 40);
    lv_obj_set_style_pad_all(hdr, 5, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);

    lv_obj_t *close_btn = lv_btn_create(hdr);
    lv_obj_set_size(close_btn, 30, 25);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xE74C3C), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(close_btn, 3, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_t *x = lv_label_create(close_btn);
    lv_label_set_text(x, "X");
    lv_obj_set_style_text_font(x, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(x, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(x);
    lv_obj_add_event_cb(close_btn, close_gamble_window_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t = lv_label_create(hdr);
    lv_label_set_text(t, "Gemini-s1 开发板介绍");
    lv_obj_set_style_text_font(t, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xECF0F1), 0);

    /* Scrollable content */
    lv_obj_t *scroll = lv_obj_create(gamble_window);
    lv_obj_set_size(scroll, LV_PCT(100), 160);
    lv_obj_set_style_pad_all(scroll, 15, 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);

    lv_obj_t *content = lv_label_create(scroll);
    lv_label_set_text(content, "\n"
                              "Gemini-s1 开发板介绍\n\n"
                              "高性能主控   R528 双核 ARM Cortex-A7\n\n"
                              "音频处理   麦克风阵列\n\n"
                              "可视化交互   配备显示屏\n\n"
                              "多模协同   WIFI, 蓝牙无线通信\n\n"
                              "丰富外设   GPIO、I2C、SPI、UART、ADC、PCM\n\n"
                              "环境传感器   温湿度，光照，人体感应、空气质量");
    lv_obj_set_style_text_font(content, g_misans_normal_11, 0);
    lv_obj_set_style_text_color(content, lv_color_hex(0xECF0F1), 0);
    lv_obj_set_style_text_align(content, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_scroll_to_y(scroll, 0, LV_ANIM_OFF);
}
