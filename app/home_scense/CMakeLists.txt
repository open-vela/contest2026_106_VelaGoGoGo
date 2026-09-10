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
    home_scense_emoji_ipc.c
    claude_mqtt.c
    ui_status_bar.c
    ui_settings.c
    wifi_status.c
    ac_ir_control.c)
  list(APPEND home_scense_srcs
    ${CMAKE_CURRENT_LIST_DIR}/../../../../apps/netutils/mqttc/MQTT-C/src/mqtt.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../../apps/netutils/mqttc/MQTT-C/src/mqtt_pal.c)
  list(APPEND home_scense_srcs
    emoji_blob.S)

  if(CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE)
    list(APPEND home_scense_srcs
      ui_voice.c
      doubao/doubao_voice.c
      doubao/doubao_protocol.c
      doubao/voice_transport.c
      doubao/voice_capture.c
      doubao/voice_player.c
      doubao/wake_reply.c
      doubao/wake_reply_assets.S)
  endif()

  # ---------------------------------------------------------------------------
  # Background frontal-face detection (BlazeFace / TFLite Micro), no preview.
  # Sources + include dirs + compile flags mirror apps/examples/face_detection.
  # Reached via the build-tree symlink apps/packages/demos/contest2026_106_hello_app
  # so the ../../../../apps prefix (same as the MQTT-C sources above) points at
  # the repo apps/ tree.
  # ---------------------------------------------------------------------------
  if(CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE
     AND CONFIG_TFLITEMICRO
     AND CONFIG_DRIVERS_VIDEO)
    set(APPS_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../../apps)
    set(TFLM_DIR ${APPS_DIR}/mlearning/tflite-micro/tflite-micro)

    set(FACE_COMMON_FLAGS
        -fno-lto
        -fno-exceptions
        -DTF_LITE_STATIC_MEMORY
        -DTF_LITE_DISABLE_X86_NEON
        -Wno-sign-compare
        -Wno-unused-variable
        -Wno-undef
        -Wno-shadow
        -DTFLITE_WITH_STABLE_ABI=0
        -DTFLITE_USE_OPAQUE_DELEGATE=0
        -DTFLITE_SINGLE_ROUNDING=0
        -O3)
    if(NOT CONFIG_TFLITEMICRO_DEBUG)
      list(APPEND FACE_COMMON_FLAGS -DTF_LITE_STRIP_ERROR_STRINGS)
    endif()

    file(
      GLOB
      TFLITE_MICRO_SRCS
      ${TFLM_DIR}/tensorflow/lite/micro/kernels/*.cc
      ${TFLM_DIR}/tensorflow/lite/c/*.cc
      ${TFLM_DIR}/tensorflow/lite/schema/*.cc
      ${TFLM_DIR}/tensorflow/lite/core/c/*.cc
      ${TFLM_DIR}/tensorflow/lite/kernels/*.cc
      ${TFLM_DIR}/tensorflow/lite/kernels/internal/optimized/*.cc
      ${TFLM_DIR}/tensorflow/lite/kernels/internal/reference/*.cc
      ${TFLM_DIR}/tensorflow/lite/kernels/internal/*.cc
      ${TFLM_DIR}/tensorflow/lite/core/api/*.cc
      ${TFLM_DIR}/tensorflow/lite/micro/arena_allocator/*.cc
      ${TFLM_DIR}/tensorflow/lite/micro/memory_planner/*.cc
      ${TFLM_DIR}/tensorflow/lite/micro/*.cc
      ${TFLM_DIR}/tensorflow/lite/micro/tflite_bridge/*.cc)
    list(FILTER TFLITE_MICRO_SRCS EXCLUDE REGEX ".*test.cc")
    list(FILTER TFLITE_MICRO_SRCS EXCLUDE REGEX ".*test_common.cc")

    set(FACE_INCDIR
        ${CMAKE_BINARY_DIR}/apps/math/gemmlowp/gemmlowp
        ${CMAKE_BINARY_DIR}/apps/math/ruy/ruy
        ${CMAKE_BINARY_DIR}/apps/math/kissfft/kissfft
        ${CMAKE_BINARY_DIR}/apps/math/tflite-micro/tflite-micro
        ${TFLM_DIR}
        ${APPS_DIR}/math/gemmlowp/gemmlowp
        ${APPS_DIR}/math/ruy/ruy
        ${APPS_DIR}/system/flatbuffers/flatbuffers/include
        ${APPS_DIR}/examples/uvc_test
        ${APPS_DIR}/examples/face_detection)

    list(APPEND home_scense_srcs
      face_detect.cc
      ${APPS_DIR}/examples/uvc_test/tjpgd.c
      ${TFLITE_MICRO_SRCS})
  endif()

  if(CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP)
    list(APPEND home_scense_srcs
      wakeup/wakeup.c
      wakeup/mel_features.c
      wakeup/mic_capture.c
      wakeup/wav_player.c)
  endif()

  nuttx_add_application(
    NAME      ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_PROGNAME}
    SRCS      ${home_scense_srcs}
    STACKSIZE ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_STACKSIZE}
    PRIORITY  ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_PRIORITY}
    MODULE    ${CONFIG_LVX_USE_DEMO_CONTEST2026_106_HELLO_APP}
    DEPENDS   mqttc)

  if(CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE
     AND CONFIG_TFLITEMICRO
     AND CONFIG_DRIVERS_VIDEO)
    set(FACE_TARGET apps_${CONFIG_LVX_USE_DEMO_CONTEST2026_106_PROGNAME})
    target_include_directories(${FACE_TARGET} PRIVATE ${FACE_INCDIR})
    target_compile_options(${FACE_TARGET} PRIVATE ${FACE_COMMON_FLAGS})
  endif()
endif()
