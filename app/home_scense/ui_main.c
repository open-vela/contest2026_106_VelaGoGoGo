/****************************************************************************
 * app/home_scense/ui_main.c
 * Home background container.  Interactive content is owned by ui_voice.c.
 ****************************************************************************/

#include "main.h"

void create_main_screen(void)
{
    lv_obj_t *screen = lv_scr_act();

    bg_img = NULL;
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x17202A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}
