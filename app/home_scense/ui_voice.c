/****************************************************************************
 * app/home_scense/ui_voice.c
 * AI conversation home page below the shared status bar.
 ****************************************************************************/

#include "ui_voice.h"
#include "main.h"
#include "wifi_status.h"
#include "doubao/doubao_voice.h"

#include <string.h>

static lv_obj_t *g_messages;
static lv_obj_t *g_action;
static lv_obj_t *g_action_label;
static lv_obj_t *g_state_label;
static char g_last_user[DOUBAO_TEXT_MAX];
static char g_last_reply[DOUBAO_REPLY_MAX];

static void append_bubble(const char *prefix, const char *text, bool user)
{
    lv_obj_t *bubble;
    lv_obj_t *label;

    if (!text || !text[0]) return;
    bubble = lv_obj_create(g_messages);
    lv_obj_set_width(bubble, 220);
    lv_obj_set_style_bg_color(bubble,
                              user ? lv_color_hex(0x1F618D) :
                                     lv_color_hex(0x273746), 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_radius(bubble, 8, 0);
    lv_obj_set_style_pad_all(bubble, 7, 0);
    lv_obj_set_style_margin_top(bubble, 4, 0);
    lv_obj_set_style_margin_bottom(bubble, 4, 0);
    lv_obj_set_style_margin_left(bubble, user ? 78 : 4, 0);
    lv_obj_set_style_margin_right(bubble, user ? 4 : 78, 0);

    label = lv_label_create(bubble);
    lv_obj_set_width(label, 205);
    lv_label_set_text_fmt(label, "%s%s", prefix, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF7F9F9), 0);
    lv_obj_scroll_to_view(bubble, LV_ANIM_ON);
}

static const char *status_text(doubao_voice_state_t state)
{
    switch (state) {
    case DOUBAO_VOICE_UNCONFIGURED: return "请先配置豆包凭证";
    case DOUBAO_VOICE_IDLE: return "点击开始说话";
    case DOUBAO_VOICE_CONNECTING: return "正在连接豆包…";
    case DOUBAO_VOICE_RECORDING: return "正在聆听…";
    case DOUBAO_VOICE_WAITING_RESPONSE: return "识别与思考中…";
    case DOUBAO_VOICE_PLAYING: return "豆包正在回答…";
    case DOUBAO_VOICE_ERROR: return "语音服务发生错误";
    default: return "";
    }
}

static void action_click_cb(lv_event_t *event)
{
    doubao_voice_snapshot_t snapshot;
    (void)event;
    home_record_activity();
    doubao_voice_get_snapshot(&snapshot);
    if (snapshot.state == DOUBAO_VOICE_RECORDING) {
        (void)doubao_voice_stop();
    } else if (snapshot.state == DOUBAO_VOICE_ERROR) {
        (void)doubao_voice_reset();
    } else if (snapshot.state == DOUBAO_VOICE_IDLE &&
               wifi_status_is_connected()) {
        (void)doubao_voice_start();
    }
}

void ui_voice_create(lv_obj_t *parent)
{
    g_messages = lv_obj_create(parent);
    lv_obj_set_size(g_messages, SCREEN_WIDTH - 16, 150);
    lv_obj_align(g_messages, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_opa(g_messages, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_messages, 0, 0);
    lv_obj_set_style_pad_all(g_messages, 2, 0);
    lv_obj_set_flex_flow(g_messages, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_messages, LV_DIR_VER);

    g_state_label = lv_label_create(parent);
    lv_obj_set_width(g_state_label, SCREEN_WIDTH - 24);
    lv_obj_align(g_state_label, LV_ALIGN_BOTTOM_MID, 0, -48);
    lv_obj_set_style_text_font(g_state_label, g_misans_normal_11, 0);
    lv_obj_set_style_text_align(g_state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_state_label, lv_color_hex(0xAED6F1), 0);

    g_action = lv_button_create(parent);
    lv_obj_set_size(g_action, SCREEN_WIDTH - 16, 36);
    lv_obj_align(g_action, LV_ALIGN_BOTTOM_MID, 0, -7);
    lv_obj_set_style_bg_color(g_action, lv_color_hex(0x1F618D), 0);
    lv_obj_set_style_border_width(g_action, 0, 0);
    lv_obj_set_style_radius(g_action, 8, 0);
    lv_obj_add_event_cb(g_action, action_click_cb, LV_EVENT_CLICKED, NULL);
    g_action_label = lv_label_create(g_action);
    lv_obj_set_style_text_font(g_action_label, g_misans_normal_16, 0);
    lv_obj_set_style_text_color(g_action_label, lv_color_hex(0xF7F9F9), 0);
    lv_obj_center(g_action_label);
}

void ui_voice_refresh(lv_timer_t *timer)
{
    doubao_voice_snapshot_t snapshot;
    bool enabled;

    (void)timer;
    if (!g_action) return;
    doubao_voice_get_snapshot(&snapshot);

    if (strcmp(snapshot.user_text, g_last_user) != 0 ||
        strcmp(snapshot.assistant_text, g_last_reply) != 0) {
        lv_obj_clean(g_messages);
        if (snapshot.user_text[0]) append_bubble("你：", snapshot.user_text, true);
        if (snapshot.assistant_text[0]) append_bubble("豆包：", snapshot.assistant_text, false);
        strcpy(g_last_user, snapshot.user_text);
        strcpy(g_last_reply, snapshot.assistant_text);
    }

    lv_label_set_text(g_state_label,
                      snapshot.state == DOUBAO_VOICE_ERROR &&
                      snapshot.error_text[0] ? snapshot.error_text :
                      status_text(snapshot.state));

    enabled = snapshot.state == DOUBAO_VOICE_IDLE && wifi_status_is_connected();
    if (snapshot.state == DOUBAO_VOICE_UNCONFIGURED) {
        lv_label_set_text(g_action_label, "请先配置豆包凭证");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0x566573), 0);
        lv_obj_add_state(g_action, LV_STATE_DISABLED);
    } else if (!wifi_status_is_connected() && snapshot.state == DOUBAO_VOICE_IDLE) {
        lv_label_set_text(g_action_label, "Wi-Fi 未连接");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0x566573), 0);
        lv_obj_add_state(g_action, LV_STATE_DISABLED);
    } else if (snapshot.state == DOUBAO_VOICE_RECORDING) {
        lv_label_set_text(g_action_label, "结束录音");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0xC0392B), 0);
        lv_obj_clear_state(g_action, LV_STATE_DISABLED);
    } else if (snapshot.state == DOUBAO_VOICE_ERROR) {
        lv_label_set_text(g_action_label, "重试");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0xD68910), 0);
        lv_obj_clear_state(g_action, LV_STATE_DISABLED);
    } else if (enabled) {
        lv_label_set_text(g_action_label, "AI 说话");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0x1F618D), 0);
        lv_obj_clear_state(g_action, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(g_action_label, "豆包处理中…");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0x566573), 0);
        lv_obj_add_state(g_action, LV_STATE_DISABLED);
    }
}
