# Contest 2026 team 106 home_scense app

if(CONFIG_LVX_USE_DEMO_CONTEST2026_106_HELLO_APP)
  set(home_scense_srcs
    main.c
    sensor.c
    ui_main.c
    ui_light.c
    ui_about.c
    led_control.c
    ui_emoji_idle.c
    ui_status.c
    ui_status_bar.c
    ui_settings.c
    wifi_status.c)

  if(CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE)
    list(APPEND home_scense_srcs
      ui_voice.c
      doubao/doubao_voice.c
      doubao/doubao_protocol.c
      doubao/voice_transport.c
      doubao/voice_capture.c
      doubao/voice_player.c)
  endif()

  nuttx_add_application(
    NAME      ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_PROGNAME}
    SRCS      ${home_scense_srcs}
    STACKSIZE ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_STACKSIZE}
    PRIORITY  ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_PRIORITY}
    MODULE    ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_HELLO_APP})
endif()
