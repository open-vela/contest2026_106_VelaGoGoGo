/****************************************************************************
 * app/home_scense/led_control.c — WS2812 LED hardware driver
 *
 * Originally lv_demo_panel_rgb_control.c from OpenVela luncher_mini.
 ****************************************************************************/

#include "led_control.h"
#include "sunxi_hal_ledc.h"
#include <debug.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#ifndef CONFIG_WS2812_DEV_PATH
#ifdef CONFIG_LED_RGB_WS2812
#define CONFIG_WS2812_DEV_PATH "/dev/leds0"
#else
#define CONFIG_WS2812_DEV_PATH "/dev/leds0"
#endif
#endif

/* Internal state */
static led_status_t g_led_status = {
    .color      = LED_COLOR_WHITE,
    .mode       = LED_MODE_STATIC,
    .brightness = 100,
    .frequency  = 1,
    .is_running = false
};
static bool g_initialized = false;

/* Forward */
static led_error_t apply_led_settings(void);

/* Low-level WS2812 write */
static int ctrl_ws2812(unsigned int color)
{
    int fd = open(CONFIG_WS2812_DEV_PATH, O_RDWR);
    if (fd < 0) {
        return -1;
    }
    int ret = write(fd, &color, sizeof(int));
    close(fd);
    return (sizeof(int) == ret) ? 0 : -1;
}

/*===========================================================================
 * Init / Deinit
 *===========================================================================*/
led_error_t led_controller_init(void)
{
    if (g_initialized) return LED_SUCCESS;

#ifndef CONFIG_LED_RGB_WS2812
    if (hal_ledc_init() != 0) {
        _info("Failed to initialize LEDC controller\n");
        return LED_ERROR_INIT_FAILED;
    }
#endif
    g_initialized = true;
    g_led_status.is_running = false;
    _info("LED controller initialized\n");
    return LED_SUCCESS;
}

led_error_t led_controller_deinit(void)
{
    if (!g_initialized) return LED_SUCCESS;
    led_off();
#ifndef CONFIG_LED_RGB_WS2812
    hal_ledc_deinit();
#endif
    g_initialized = false;
    return LED_SUCCESS;
}

/*===========================================================================
 * On / Off / Toggle
 *===========================================================================*/
led_error_t led_on(void)
{
    if (!g_initialized) return LED_ERROR_NOT_RUNNING;
    g_led_status.is_running = true;
    return apply_led_settings();
}

led_error_t led_off(void)
{
    if (!g_initialized) return LED_ERROR_NOT_RUNNING;
    uint32_t saved_color = g_led_status.color;
    g_led_status.is_running = false;
#ifdef CONFIG_LED_RGB_WS2812
    ctrl_ws2812(LED_COLOR_OFF);
#else
    sunxi_set_led_brightness(0, LED_COLOR_OFF);
#endif
    g_led_status.color = saved_color;
    return LED_SUCCESS;
}

led_error_t led_toggle(void)
{
    return g_led_status.is_running ? led_off() : led_on();
}

led_error_t led_is_on(bool *state)
{
    if (!state) return LED_ERROR_INVALID_PARAM;
    *state = g_led_status.is_running;
    return LED_SUCCESS;
}

/*===========================================================================
 * Color / Brightness
 *===========================================================================*/
led_error_t led_set_color(uint32_t color)
{
    if (!g_initialized) return LED_ERROR_NOT_RUNNING;
    g_led_status.color = color;
    g_led_status.mode  = LED_MODE_STATIC;
    if (g_led_status.is_running) return apply_led_settings();
    return LED_SUCCESS;
}

led_error_t led_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    return led_set_color((red << 16) | (green << 8) | blue);
}

led_error_t led_set_brightness(int32_t brightness)
{
    if (brightness < 0 || brightness > 100) return LED_ERROR_INVALID_PARAM;
    g_led_status.brightness = brightness;
    if (g_led_status.is_running) return apply_led_settings();
    return LED_SUCCESS;
}

/*===========================================================================
 * Preset colors
 *===========================================================================*/
led_error_t led_red(void)     { return led_set_color(LED_COLOR_RED); }
led_error_t led_green(void)   { return led_set_color(LED_COLOR_GREEN); }
led_error_t led_blue(void)    { return led_set_color(LED_COLOR_BLUE); }
led_error_t led_yellow(void)  { return led_set_color(LED_COLOR_YELLOW); }
led_error_t led_cyan(void)    { return led_set_color(LED_COLOR_CYAN); }
led_error_t led_magenta(void) { return led_set_color(LED_COLOR_MAGENTA); }
led_error_t led_white(void)   { return led_set_color(LED_COLOR_WHITE); }

/*===========================================================================
 * Modes
 *===========================================================================*/
led_error_t led_set_mode_static(uint32_t color)
{
    g_led_status.color     = color;
    g_led_status.mode      = LED_MODE_STATIC;
    g_led_status.frequency = 1;
    if (g_led_status.is_running) return apply_led_settings();
    return LED_SUCCESS;
}

led_error_t led_set_mode_blink(uint32_t color, int frequency)
{
    if (frequency <= 0 || frequency > 10) return LED_ERROR_INVALID_PARAM;
    g_led_status.color     = color;
    g_led_status.mode      = LED_MODE_BLINK;
    g_led_status.frequency = frequency;
    if (g_led_status.is_running) return apply_led_settings();
    return LED_SUCCESS;
}

led_error_t led_set_mode_rainbow(int speed)
{
    if (speed <= 0 || speed > 10) return LED_ERROR_INVALID_PARAM;
    g_led_status.mode      = LED_MODE_RAINBOW;
    g_led_status.frequency = speed;
    if (!g_led_status.is_running) led_on();
    return LED_SUCCESS;
}

/*===========================================================================
 * Status / Utilities
 *===========================================================================*/
led_error_t led_get_status(led_status_t *status)
{
    if (!status) return LED_ERROR_INVALID_PARAM;
    memcpy(status, &g_led_status, sizeof(led_status_t));
    return LED_SUCCESS;
}

uint32_t led_rgb_to_hex(uint8_t r, uint8_t g, uint8_t b)
{
    return (r << 16) | (g << 8) | b;
}

void led_hex_to_rgb(uint32_t hex, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (r) *r = (hex >> 16) & 0xFF;
    if (g) *g = (hex >> 8)  & 0xFF;
    if (b) *b =  hex        & 0xFF;
}

const char *led_get_color_name(uint32_t color)
{
    switch (color) {
    case LED_COLOR_RED:     return "Red";
    case LED_COLOR_GREEN:   return "Green";
    case LED_COLOR_BLUE:    return "Blue";
    case LED_COLOR_YELLOW:  return "Yellow";
    case LED_COLOR_CYAN:    return "Cyan";
    case LED_COLOR_MAGENTA: return "Magenta";
    case LED_COLOR_WHITE:   return "White";
    case LED_COLOR_OFF:     return "Off";
    default:                return "Custom";
    }
}

const char *led_get_error_string(led_error_t error)
{
    switch (error) {
    case LED_SUCCESS:             return "Success";
    case LED_ERROR_INIT_FAILED:   return "Initialization failed";
    case LED_ERROR_INVALID_PARAM: return "Invalid parameter";
    case LED_ERROR_HARDWARE:      return "Hardware error";
    case LED_ERROR_NOT_RUNNING:   return "LED controller not running";
    default:                      return "Unknown error";
    }
}

void demo_advanced_control(void)
{
    printf("\n=== LED Advanced Control Demo ===\n");
    led_controller_init();
    printf("Setting custom color and turning ON...\n");
    led_set_rgb(255, 165, 0);
    led_on();
    sleep(2);
    for (int i = 100; i >= 0; i -= 20) {
        led_set_brightness(i);
        printf("Brightness: %d%%\n", i);
        sleep(1);
    }
    for (int i = 0; i <= 100; i += 20) {
        led_set_brightness(i);
        printf("Brightness: %d%%\n", i);
        sleep(1);
    }
    led_off();
    led_controller_deinit();
}

/*===========================================================================
 * Internal: apply brightness-adjusted color to hardware
 *===========================================================================*/
static led_error_t apply_led_settings(void)
{
    if (!g_initialized)    return LED_ERROR_NOT_RUNNING;
    if (!g_led_status.is_running) return LED_SUCCESS;

    uint32_t actual_color = g_led_status.color;
    if (g_led_status.brightness < 100) {
        uint8_t r, g, b;
        led_hex_to_rgb(actual_color, &r, &g, &b);
        r = (r * g_led_status.brightness) / 100;
        g = (g * g_led_status.brightness) / 100;
        b = (b * g_led_status.brightness) / 100;
        actual_color = led_rgb_to_hex(r, g, b);
    }

#ifdef CONFIG_LED_RGB_WS2812
    if (ctrl_ws2812(actual_color) != 0) {
        _info("Error setting LED color\n");
        return LED_ERROR_HARDWARE;
    }
#else
    if (sunxi_set_led_brightness(0, actual_color) != 0) {
        _info("Error setting LED color\n");
        return LED_ERROR_HARDWARE;
    }
#endif
    return LED_SUCCESS;
}
