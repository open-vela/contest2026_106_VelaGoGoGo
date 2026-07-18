/****************************************************************************
 * app/home_scense/ui_emoji.c
 * Animated emoji face drawn with LVGL primitives — no font dependency.
 *
 * Cycles through 5 expressions every 3 seconds:
 *   😊 Smile  😄 Laugh  😉 Wink  🙄 Roll  😎 Cool
 ****************************************************************************/

#include "ui_emoji.h"

/*-----------------------------------------------------------------------
 * Expression enum
 *---------------------------------------------------------------------*/
typedef enum {
    EXPR_SMILE,
    EXPR_LAUGH,
    EXPR_WINK,
    EXPR_ROLL,
    EXPR_COOL,
    EXPR_COUNT
} expr_t;

/*-----------------------------------------------------------------------
 * Emoji state (one instance)
 *---------------------------------------------------------------------*/
typedef struct {
    lv_obj_t *face;
    lv_obj_t *eye_l;
    lv_obj_t *eye_r;
    lv_obj_t *mouth;
    expr_t    current;
} emoji_t;

static emoji_t g_emoji;

/*-----------------------------------------------------------------------
 * Expression → LED color mapping (24-bit RGB)
 *   SMILE=RED  LAUGH=YELLOW  WINK=GREEN  ROLL=RED  COOL=YELLOW
 *---------------------------------------------------------------------*/
static const uint32_t s_expr_color[EXPR_COUNT] = {
    0xFF0000,   /* EXPR_SMILE — Red   */
    0xFFFF00,   /* EXPR_LAUGH — Yellow */
    0x00FF00,   /* EXPR_WINK  — Green  */
    0xFF0000,   /* EXPR_ROLL  — Red    */
    0xFFFF00,   /* EXPR_COOL  — Yellow */
};

static emoji_expr_changed_cb_t s_on_expr_changed = NULL;

/*-----------------------------------------------------------------------
 * Apply one expression: reposition eyes + reshape mouth
 *---------------------------------------------------------------------*/
static void set_expression(expr_t e)
{
    lv_coord_t el_x, el_y, er_x, er_y, ew, eh;
    lv_coord_t mw, mh, mx, my, mr;

    switch (e) {

    case EXPR_SMILE:
        ew = 7; eh = 7;  el_x = 5; el_y = 9;  er_x = 19; er_y = 9;
        lv_obj_set_style_radius(g_emoji.eye_l, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_radius(g_emoji.eye_r, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(g_emoji.eye_l, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(g_emoji.eye_r, LV_OPA_COVER, 0);
        mw = 14; mh = 5; mx = 8; my = 20; mr = 5;
        break;

    case EXPR_LAUGH:
        ew = 8; eh = 3;  el_x = 4; el_y = 10;  er_x = 18; er_y = 10;
        lv_obj_set_style_radius(g_emoji.eye_l, 2, 0);
        lv_obj_set_style_radius(g_emoji.eye_r, 2, 0);
        lv_obj_set_style_bg_opa(g_emoji.eye_l, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(g_emoji.eye_r, LV_OPA_COVER, 0);
        mw = 14; mh = 9; mx = 8; my = 18; mr = 6;
        break;

    case EXPR_WINK:
        ew = 7; eh = 7;  el_x = 5; el_y = 9;  er_x = 18; er_y = 10;
        lv_obj_set_style_radius(g_emoji.eye_l, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_radius(g_emoji.eye_r, 1, 0);
        lv_obj_set_style_bg_opa(g_emoji.eye_l, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(g_emoji.eye_r, LV_OPA_COVER, 0);
        lv_obj_set_size(g_emoji.eye_r, 8, 3);  /* wink = flat line */
        mw = 12; mh = 4; mx = 9; my = 20; mr = 3;
        break;

    case EXPR_ROLL:
        ew = 6; eh = 6;  el_x = 6; el_y = 6;  er_x = 19; er_y = 6;
        lv_obj_set_style_radius(g_emoji.eye_l, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_radius(g_emoji.eye_r, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(g_emoji.eye_l, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(g_emoji.eye_r, LV_OPA_COVER, 0);
        mw = 10; mh = 3; mx = 10; my = 21; mr = 1;
        break;

    case EXPR_COOL:
        /* sunglasses: one wide dark bar across both eyes */
        ew = 20; eh = 5;  el_x = 5; el_y = 8;  er_x = 0; er_y = 0;
        lv_obj_set_style_radius(g_emoji.eye_l, 2, 0);
        lv_obj_set_style_radius(g_emoji.eye_r, 0, 0);
        lv_obj_set_style_bg_opa(g_emoji.eye_l, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(g_emoji.eye_r, LV_OPA_TRANSP, 0);
        lv_obj_set_size(g_emoji.eye_l, 20, 5);  /* bar */
        lv_obj_set_size(g_emoji.eye_r, 0, 0);
        mw = 10; mh = 3; mx = 10; my = 20; mr = 2;
        break;

    default:
        return;
    }

    /* Reset sizes for non-wink, non-cool */
    if (e != EXPR_WINK) {
        lv_obj_set_size(g_emoji.eye_l, ew, eh);
        lv_obj_set_size(g_emoji.eye_r, ew, eh);
    }
    if (e != EXPR_COOL) {
        lv_obj_set_size(g_emoji.eye_l, ew, eh);
        lv_obj_set_size(g_emoji.eye_r, ew, eh);
    }

    lv_obj_set_pos(g_emoji.eye_l, el_x, el_y);
    lv_obj_set_pos(g_emoji.eye_r, er_x, er_y);

    lv_obj_set_size(g_emoji.mouth, mw, mh);
    lv_obj_set_pos(g_emoji.mouth, mx, my);
    lv_obj_set_style_radius(g_emoji.mouth, mr, 0);
    lv_obj_set_style_bg_color(g_emoji.mouth, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(g_emoji.mouth, LV_OPA_COVER, 0);

    if (e == EXPR_LAUGH) {
        /* open mouth: black background, white inside = hollow effect */
        lv_obj_set_style_bg_color(g_emoji.mouth, lv_color_hex(0x5A3E00), 0);
    }

    g_emoji.current = e;
}

/*-----------------------------------------------------------------------
 * Timer: cycle to next expression
 *---------------------------------------------------------------------*/
static void emoji_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    expr_t next = (expr_t)((g_emoji.current + 1) % EXPR_COUNT);
    set_expression(next);
    if (s_on_expr_changed) {
        s_on_expr_changed(s_expr_color[next]);
    }
}

/*-----------------------------------------------------------------------
 * Public: create the emoji and return its container
 *---------------------------------------------------------------------*/
lv_obj_t *emoji_create(lv_obj_t *parent)
{
    /* Face background — yellow circle */
    g_emoji.face = lv_obj_create(parent);
    lv_obj_set_size(g_emoji.face, 34, 34);
    lv_obj_set_style_bg_color(g_emoji.face, lv_color_hex(0xFFD93D), 0);
    lv_obj_set_style_bg_opa(g_emoji.face, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_emoji.face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(g_emoji.face, 1, 0);
    lv_obj_set_style_border_color(g_emoji.face, lv_color_hex(0xE6B800), 0);
    lv_obj_set_style_pad_all(g_emoji.face, 0, 0);

    /* Left eye */
    g_emoji.eye_l = lv_obj_create(g_emoji.face);
    lv_obj_set_style_bg_color(g_emoji.eye_l, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(g_emoji.eye_l, 0, 0);
    lv_obj_set_style_pad_all(g_emoji.eye_l, 0, 0);

    /* Right eye */
    g_emoji.eye_r = lv_obj_create(g_emoji.face);
    lv_obj_set_style_bg_color(g_emoji.eye_r, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(g_emoji.eye_r, 0, 0);
    lv_obj_set_style_pad_all(g_emoji.eye_r, 0, 0);

    /* Mouth */
    g_emoji.mouth = lv_obj_create(g_emoji.face);
    lv_obj_set_style_bg_color(g_emoji.mouth, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(g_emoji.mouth, 0, 0);
    lv_obj_set_style_pad_all(g_emoji.mouth, 0, 0);

    set_expression(EXPR_SMILE);

    /* Cycle every 3 seconds */
    lv_timer_t *t = lv_timer_create(emoji_timer_cb, 3000, NULL);
    lv_timer_set_repeat_count(t, -1);

    return g_emoji.face;
}

/*-----------------------------------------------------------------------
 * Public: register color-change callback
 *---------------------------------------------------------------------*/
void emoji_register_color_callback(emoji_expr_changed_cb_t cb)
{
    s_on_expr_changed = cb;
}

/*-----------------------------------------------------------------------
 * Public: return the LED color for the current expression
 *---------------------------------------------------------------------*/
uint32_t emoji_get_current_color(void)
{
    return s_expr_color[g_emoji.current];
}
