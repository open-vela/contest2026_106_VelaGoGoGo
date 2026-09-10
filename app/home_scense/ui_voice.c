/****************************************************************************
 * app/home_scense/ui_voice.c
 * AI conversation home page below the shared status bar.
 *
 * 多轮对话历史:每轮追加"你/豆包"两条气泡,历史保留、自动滚到底。
 * 按钮为点击切换(点一下开始录音 / 再点一下结束并等待回复),配合常驻会话:
 * 应用启动即连接豆包,每次录音只是同一连接上的新一轮,无需每轮重连。
 ****************************************************************************/

#include "ui_voice.h"
#include "main.h"
#include "ui_emoji_idle.h"
#include "wifi_status.h"
#include "doubao/doubao_voice.h"

#include <string.h>

static lv_obj_t *g_messages;
static lv_obj_t *g_action;
static lv_obj_t *g_action_label;
static lv_obj_t *g_state_label;

/* 当前轮的气泡(实时更新);turn_seq 变化即沉淀为历史、清引用 */
static lv_obj_t *g_cur_user_bubble;
static lv_obj_t *g_cur_ai_bubble;
static unsigned  g_cur_turn;
static char g_last_user[DOUBAO_TEXT_MAX];
static char g_last_reply[DOUBAO_REPLY_MAX];

typedef enum {
    VOICE_ANIMATION_NONE,
    VOICE_ANIMATION_LISTEN,
    VOICE_ANIMATION_SPEAK,
} voice_animation_t;

static voice_animation_t g_voice_animation;

static void update_voice_animation(doubao_voice_state_t state)
{
    voice_animation_t next = VOICE_ANIMATION_NONE;
    const char *name = NULL;

    if (state == DOUBAO_VOICE_LISTENING ||
        state == DOUBAO_VOICE_RECORDING) {
        next = VOICE_ANIMATION_LISTEN;
        name = "listen";
    } else if (state == DOUBAO_VOICE_PLAYING) {
        next = VOICE_ANIMATION_SPEAK;
        name = "speak";
    }

    if (next == g_voice_animation) return;

    /* Route through the priority arbiter as the VOICE source: listen/speak
     * outrank the Claude MQTT status, so an active conversation is never
     * masked by the agent状态动画;NULL 时撤回本源请求,交回给 Claude/待机。 */
    emoji_idle_request(EMOJI_SRC_VOICE, name);
    g_voice_animation = next;
}

/* 新增/更新气泡:传入已存在的 bubble 则更新其文字,否则新建并返回。 */
static lv_obj_t *set_bubble(lv_obj_t *existing, const char *prefix,
                            const char *text, bool user)
{
    lv_obj_t *bubble;
    lv_obj_t *label;

    if (!text || !text[0]) return existing;

    if (existing) {
        label = lv_obj_get_child(existing, 0);
        if (label) lv_label_set_text_fmt(label, "%s%s", prefix, text);
        lv_obj_scroll_to_view(existing, LV_ANIM_ON);
        return existing;
    }

    bubble = lv_obj_create(g_messages);
    lv_obj_set_width(bubble, 220);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
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
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    label = lv_label_create(bubble);
    lv_obj_set_width(label, 205);
    lv_label_set_text_fmt(label, "%s%s", prefix, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, g_misans_normal_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF7F9F9), 0);
    lv_obj_scroll_to_view(bubble, LV_ANIM_ON);
    return bubble;
}

static const char *status_text(doubao_voice_state_t state)
{
    switch (state) {
    case DOUBAO_VOICE_UNCONFIGURED: return "请先配置豆包凭证";
    case DOUBAO_VOICE_IDLE: return "点击说话";
    case DOUBAO_VOICE_CONNECTING: return "正在连接豆包…";
    case DOUBAO_VOICE_LISTENING: return "聆听中…请说话";
    case DOUBAO_VOICE_RECORDING: return "正在识别…";
    case DOUBAO_VOICE_WAITING_RESPONSE: return "思考中…";
    case DOUBAO_VOICE_PLAYING: return "豆包正在回答…";
    case DOUBAO_VOICE_ERROR: return "语音服务发生错误";
    default: return "";
    }
}

/* 全双工:点击切换会话。待命(未 talking)点击 → 开始连续对话;对话中(任一
 * 聆听/识别/思考/回答态)点击 → 停止会话。用 doubao_voice_is_talking() 判定,
 * 不再维护本地 toggle,避免与真实状态错位。 */
static void action_click_cb(lv_event_t *event)
{
    static uint32_t last_click;
    uint32_t now;

    (void)event;
    home_record_activity();

    /* 点击去抖:触摸屏偶尔把一次点击弹成两次 CLICKED。350ms 内重复忽略。 */
    now = lv_tick_get();
    if (now - last_click < 350) {
        return;
    }
    last_click = now;

    if (!wifi_status_is_connected()) {
        return;
    }
    if (doubao_voice_is_talking()) {
        (void)doubao_voice_stop();
    } else {
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
    /* snapshot 含 assistant_text[DOUBAO_REPLY_MAX=8192],放栈上约 9KB,叠加
     * LVGL 主线程调用链易压爆 100KB 栈。改 static:refresh 由 LVGL 单线程
     * 周期调用,无并发,与 g_last_user/g_last_reply 一致。 */
    static doubao_voice_snapshot_t snapshot;

    (void)timer;
    if (!g_action) return;
    doubao_voice_get_snapshot(&snapshot);
    update_voice_animation(snapshot.state);

    /* 新一轮开始:上一轮气泡已沉淀为历史,清空当前轮引用 */
    if (snapshot.turn_seq != g_cur_turn) {
        g_cur_turn = snapshot.turn_seq;
        g_cur_user_bubble = NULL;
        g_cur_ai_bubble = NULL;
        g_last_user[0] = '\0';
        g_last_reply[0] = '\0';
    }

    if (snapshot.user_text[0] && strcmp(snapshot.user_text, g_last_user) != 0) {
        strncpy(g_last_user, snapshot.user_text, sizeof(g_last_user) - 1);
        g_cur_user_bubble = set_bubble(g_cur_user_bubble, "你：",
                                       snapshot.user_text, true);
    }
    if (snapshot.assistant_text[0] &&
        strcmp(snapshot.assistant_text, g_last_reply) != 0) {
        strncpy(g_last_reply, snapshot.assistant_text, sizeof(g_last_reply) - 1);
        g_cur_ai_bubble = set_bubble(g_cur_ai_bubble, "豆包：",
                                     snapshot.assistant_text, false);
    }

    lv_label_set_text(g_state_label,
                      snapshot.state == DOUBAO_VOICE_ERROR &&
                      snapshot.error_text[0] ? snapshot.error_text :
                      status_text(snapshot.state));

    /* 全双工按钮:
     *  - 未配置 / Wi-Fi 未连 / 连接中 → 禁用,提示原因
     *  - 对话中(talking:聆听/识别/思考/回答任一态)→ "停止对话"(红),可点
     *  - 待命(IDLE)→ "开始对话"(蓝),可点
     *  - ERROR → "点击重试"(橙),可点(触发重新开始) */
    if (snapshot.state == DOUBAO_VOICE_UNCONFIGURED) {
        lv_label_set_text(g_action_label, "请先配置豆包凭证");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0x566573), 0);
        lv_obj_add_state(g_action, LV_STATE_DISABLED);
    } else if (!wifi_status_is_connected()) {
        lv_label_set_text(g_action_label, "Wi-Fi 未连接");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0x566573), 0);
        lv_obj_add_state(g_action, LV_STATE_DISABLED);
    } else if (snapshot.state == DOUBAO_VOICE_CONNECTING) {
        lv_label_set_text(g_action_label, "连接中…");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0x566573), 0);
        lv_obj_add_state(g_action, LV_STATE_DISABLED);
    } else if (doubao_voice_is_talking()) {
        lv_label_set_text(g_action_label, "停止对话");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0xC0392B), 0);
        lv_obj_clear_state(g_action, LV_STATE_DISABLED);
    } else if (snapshot.state == DOUBAO_VOICE_ERROR) {
        lv_label_set_text(g_action_label, "点击重试");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0xD68910), 0);
        lv_obj_clear_state(g_action, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(g_action_label, "开始对话");
        lv_obj_set_style_bg_color(g_action, lv_color_hex(0x1F618D), 0);
        lv_obj_clear_state(g_action, LV_STATE_DISABLED);
    }
}
