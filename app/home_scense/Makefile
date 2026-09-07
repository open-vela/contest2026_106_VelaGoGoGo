############################################################################
# app/home_scense/Makefile
############################################################################
include $(APPDIR)/Make.defs

MAINSRC = main.c
CSRCS   = sensor.c ui_main.c ui_light.c ui_about.c led_control.c ui_emoji_idle.c \
         claude_mqtt.c ui_claude_status.c ui_status_bar.c ui_settings.c wifi_status.c
ASRCS   = emoji_blob.S claude_idle.S claude_thinking.S claude_executing.S

ifeq ($(CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE),y)
CSRCS += ui_voice.c doubao/doubao_voice.c doubao/doubao_protocol.c \
         doubao/voice_transport.c doubao/voice_capture.c doubao/voice_player.c \
         doubao/wake_reply.c
ASRCS += doubao/wake_reply_assets.S
endif

ifeq ($(CONFIG_LVX_USE_DEMO_CONTEST2026_106_WAKEUP),y)
CSRCS += wakeup/wakeup.c wakeup/mel_features.c wakeup/mic_capture.c \
         wakeup/wav_player.c
endif

PROGNAME  = $(CONFIG_LVX_USE_DEMO_CONTEST2026_106_PROGNAME)
PRIORITY  = $(CONFIG_LVX_USE_DEMO_CONTEST2026_106_PRIORITY)
STACKSIZE = $(CONFIG_LVX_USE_DEMO_CONTEST2026_106_STACKSIZE)

# Allwinner HAL includes (required by led_control.c -> sunxi_hal_ledc.h)
include $(APPDIR)/vendor/allwinnertech/Common.mk
$(eval $(AW_ADD_HAL_INCLUDES))

# MQTT-C headers and sources (the Make build exposes MQTT-C as a library
# only to its optional examples, so link its two core objects here).
CFLAGS += -I$(APPDIR)/netutils/mqttc/MQTT-C/include
VPATH  += $(APPDIR)/netutils/mqttc/MQTT-C/src
CSRCS  += mqtt.c mqtt_pal.c

# LED configuration
CFLAGS += -DCONFIG_LED_RGB_WS2812

# ---------------------------------------------------------------------------
# Background frontal-face detection (BlazeFace / TFLite Micro), no LCD preview.
# Wiring mirrors apps/examples/face_detection/Makefile so the TFLite Micro
# framework compiles into this app. face_detect.cc drives the Doubao full-
# duplex conversation from face presence.
# ---------------------------------------------------------------------------
# Gated on existing (already-enabled) configs — introduces no new Kconfig
# symbol, so it does not regenerate .config or force a full rebuild.
ifeq ($(CONFIG_LVX_USE_DEMO_CONTEST2026_106_DOUBAO_VOICE)$(CONFIG_TFLITEMICRO)$(CONFIG_DRIVERS_VIDEO),yyy)
CXXEXT   := .cc
TFLM_DIR := $(APPDIR)/mlearning/tflite-micro/tflite-micro
UVC_DIR  := $(APPDIR)/examples/uvc_test
FACE_DIR := $(APPDIR)/examples/face_detection

CXXSRCS += face_detect.cc
CSRCS   += $(UVC_DIR)/tjpgd.c

# TFLite Micro framework sources
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/c/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/core/c/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/core/api/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/kernels/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/kernels/internal/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/kernels/internal/optimized/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/kernels/internal/reference/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/arena_allocator/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/kernels/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/memory_planner/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/tflite_bridge/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/schema/*.cc)
CXXSRCS := $(filter-out %test.cc, $(CXXSRCS))

# Include paths (framework + gemmlowp/ruy/kissfft + uvc tjpgd + model .inc)
FACE_FLAGS += ${INCDIR_PREFIX}$(TFLM_DIR)
FACE_FLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/gemmlowp/gemmlowp
FACE_FLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/kissfft/kissfft
FACE_FLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/ruy/ruy
FACE_FLAGS += ${INCDIR_PREFIX}$(UVC_DIR)
FACE_FLAGS += ${INCDIR_PREFIX}$(FACE_DIR)

# Compiler flags
FACE_FLAGS += -fno-lto
FACE_FLAGS += -fno-exceptions
FACE_FLAGS += -Wno-shadow
FACE_FLAGS += -Wno-sign-compare
FACE_FLAGS += -Wno-undef
FACE_FLAGS += -Wno-unused-variable
FACE_FLAGS += -DTFLITE_EMULATE_FLOAT
FACE_FLAGS += -DTF_LITE_DISABLE_X86_NEON

ifneq ($(CONFIG_TFLITEMICRO_DEBUG),)
FACE_FLAGS += -DTF_LITE_SHOW_MEMORY_USE
FACE_FLAGS += -DTF_LITE_USE_CTIME
else
FACE_FLAGS += -DTF_LITE_STRIP_ERROR_STRINGS
endif

# CMSIS-NN support (BEFORE flag assignment!)
ifneq ($(CONFIG_MLEARNING_CMSIS_NN),)
FACE_FLAGS += -DCMSIS_NN
FACE_FLAGS += ${INCDIR_PREFIX}$(APPDIR)/mlearning/cmsis-nn/cmsis-nn
FACE_FLAGS += ${INCDIR_PREFIX}$(APPDIR)/mlearning/cmsis-nn/cmsis-nn/Include
CMSIS_NN_SRCS := $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/kernels/cmsis_nn/*.cc)
UNNEEDED_SRCS := $(addprefix $(TFLM_DIR)/tensorflow/lite/micro/kernels/, $(notdir $(CMSIS_NN_SRCS)))
CXXSRCS := $(filter-out $(UNNEEDED_SRCS), $(CXXSRCS))
CXXSRCS += $(CMSIS_NN_SRCS)
endif

CFLAGS   += ${FACE_FLAGS}
CXXFLAGS += ${FACE_FLAGS}
endif

include $(APPDIR)/Application.mk
