/****************************************************************************
 * app/home_scense/led_control.h — WS2812 LED control API
 *
 * Originally lv_demo_panel_rgb_control.h from OpenVela luncher_mini.
 ****************************************************************************/

#ifndef HOME_SCENSE_LED_CONTROL_H
#define HOME_SCENSE_LED_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LED colors */
typedef enum {
    LED_COLOR_RED     = 0xFF0000,
    LED_COLOR_GREEN   = 0x00FF00,
    LED_COLOR_BLUE    = 0x0000FF,
    LED_COLOR_YELLOW  = 0xFFFF00,
    LED_COLOR_CYAN    = 0x00FFFF,
    LED_COLOR_MAGENTA = 0xFF00FF,
    LED_COLOR_WHITE   = 0xFFFFFF,
    LED_COLOR_OFF     = 0x000000
} led_color_t;

/* LED modes */
typedef enum {
    LED_MODE_STATIC,
    LED_MODE_BLINK,
    LED_MODE_BREATHE,
    LED_MODE_RAINBOW,
    LED_MODE_CUSTOM
} led_mode_t;

/* LED status */
typedef struct {
    uint32_t   color;
    led_mode_t mode;
    int        brightness;   /* 0-100 */
    int        frequency;    /* Hz */
    bool       is_running;
} led_status_t;

/* Error codes */
typedef enum {
    LED_SUCCESS = 0,
    LED_ERROR_INIT_FAILED = -1,
    LED_ERROR_INVALID_PARAM = -2,
    LED_ERROR_HARDWARE = -3,
    LED_ERROR_NOT_RUNNING = -4
} led_error_t;

/* Init / Deinit */
led_error_t led_controller_init(void);
led_error_t led_controller_deinit(void);

/* Basic on/off */
led_error_t led_on(void);
led_error_t led_off(void);
led_error_t led_toggle(void);
led_error_t led_is_on(bool *state);

/* Color / brightness */
led_error_t led_set_color(uint32_t color);
led_error_t led_set_rgb(uint8_t red, uint8_t green, uint8_t blue);
led_error_t led_set_brightness(int32_t brightness);

/* Preset colors */
led_error_t led_red(void);
led_error_t led_green(void);
led_error_t led_blue(void);
led_error_t led_yellow(void);
led_error_t led_cyan(void);
led_error_t led_magenta(void);
led_error_t led_white(void);

/* Modes */
led_error_t led_set_mode_static(uint32_t color);
led_error_t led_set_mode_blink(uint32_t color, int frequency);
led_error_t led_set_mode_breathe(uint32_t color, int frequency);
led_error_t led_set_mode_rainbow(int speed);
led_error_t led_set_mode_custom_sequence(const uint32_t *colors, int count, int interval_ms);

/* Status */
led_error_t  led_get_status(led_status_t *status);
const char  *led_get_error_string(led_error_t error);

/* Utilities */
uint32_t    led_rgb_to_hex(uint8_t r, uint8_t g, uint8_t b);
void        led_hex_to_rgb(uint32_t hex, uint8_t *r, uint8_t *g, uint8_t *b);
const char *led_get_color_name(uint32_t color);

/* Demo */
void demo_advanced_control(void);

#ifdef __cplusplus
}
#endif

#endif /* HOME_SCENSE_LED_CONTROL_H */
