############################################################################
# app/home_scense/Makefile
############################################################################
include $(APPDIR)/Make.defs

MAINSRC = main.c
CSRCS   = sensor.c ui_main.c ui_light.c ui_about.c led_control.c ui_emoji.c ui_emoji_idle.c
ASRCS   = emoji_blob.S

PROGNAME  = $(CONFIG_LVX_USE_DEMO_CONTEST2026_106_PROGNAME)
PRIORITY  = $(CONFIG_LVX_USE_DEMO_CONTEST2026_106_PRIORITY)
STACKSIZE = $(CONFIG_LVX_USE_DEMO_CONTEST2026_106_STACKSIZE)

# Allwinner HAL includes (required by led_control.c -> sunxi_hal_ledc.h)
include $(APPDIR)/vendor/allwinnertech/Common.mk
$(eval $(AW_ADD_HAL_INCLUDES))

# LED configuration
CFLAGS += -DCONFIG_LED_RGB_WS2812

include $(APPDIR)/Application.mk
