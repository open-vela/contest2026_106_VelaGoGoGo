# Contest 2026 team 106 home_scense app

if(CONFIG_LVX_USE_DEMO_CONTEST2026_106_HELLO_APP)
  nuttx_add_application(
    NAME      ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_PROGNAME}
    SRCS      main.c
              sensor.c
              ui_main.c
              ui_light.c
              ui_about.c
              led_control.c
              ui_emoji.c
    STACKSIZE ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_STACKSIZE}
    PRIORITY  ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_PRIORITY}
    MODULE    ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_HELLO_APP})
endif()
