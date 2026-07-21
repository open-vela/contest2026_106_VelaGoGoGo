/****************************************************************************
 * app/home_scense/ui_status.c
 * Claude Code status receiver — reads FIFO and controls LED state.
 *
 * LED mapping:
 *   processing → green blink (500ms toggle)
 *   waiting    → red static
 ****************************************************************************/

#include "ui_status.h"
#include "main.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <debug.h>

#define FIFO_PATH         "/tmp/claude_status.pipe"
#define BLINK_TOGGLE_MS   500
#define POLL_INTERVAL_MS  100

/* Hardware write — apply brightness */
static int led_write(uint32_t color)
{
    if (g_led_brightness < 100) {
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8)  & 0xFF;
        uint8_t b =  color        & 0xFF;
        r = (r * g_led_brightness) / 100;
        g = (g * g_led_brightness) / 100;
        b = (b * g_led_brightness) / 100;
        color = (r << 16) | (g << 8) | b;
    }
    int fd = open("/dev/leds0", O_RDWR);
    if (fd < 0) return -1;
    int ret = write(fd, &color, sizeof(color));
    close(fd);
    return (sizeof(color) == ret) ? 0 : -1;
}

/* Internal state */
static int              g_fifo_fd  = -1;
static claude_status_t  g_current  = CLAUDE_STATUS_NONE;
static int              g_tick     = 0;
static bool             g_on       = false;

/*-----------------------------------------------------------------------
 * State machine
 *---------------------------------------------------------------------*/
static claude_status_t parse_state(const char *s)
{
    if (strcmp(s, "processing") == 0) return CLAUDE_STATUS_PROCESSING;
    if (strcmp(s, "completed")  == 0) return CLAUDE_STATUS_COMPLETED;
    if (strcmp(s, "waiting")    == 0) return CLAUDE_STATUS_WAITING;
    if (strcmp(s, "started")    == 0) return CLAUDE_STATUS_STARTED;
    if (strcmp(s, "ended")      == 0) return CLAUDE_STATUS_ENDED;
    return CLAUDE_STATUS_NONE;
}

static const char *extract_state(const char *json, char *buf, size_t n)
{
    const char *key = "\"state\":\"";
    const char *s = strstr(json, key);
    if (!s) { key = "\"state\": \""; s = strstr(json, key); }
    if (!s) return NULL;
    s += strlen(key);
    const char *e = strchr(s, '"');
    if (!e) return NULL;
    size_t len = e - s;
    if (len >= n) return NULL;
    memcpy(buf, s, len); buf[len] = '\0';
    return buf;
}

static void apply_state(claude_status_t st)
{
    g_tick = 0;
    g_on   = false;

    if (st == CLAUDE_STATUS_STARTED || st == CLAUDE_STATUS_ENDED) {
        led_write(0x000000);
        return;
    }
    if (!g_led_is_on) return;

    switch (st) {
    case CLAUDE_STATUS_PROCESSING: led_write(0x00FF00); g_on = true; break;  /* green blink */
    case CLAUDE_STATUS_COMPLETED:  led_write(0x00FF00); break;               /* legacy */
    case CLAUDE_STATUS_WAITING:    led_write(0xFF0000); break;               /* red */
    default: break;
    }
}

/*-----------------------------------------------------------------------
 * Public
 *---------------------------------------------------------------------*/

void claude_status_init(void)
{
    struct stat st;
    if (stat(FIFO_PATH, &st) != 0) mkfifo(FIFO_PATH, 0666);

    g_fifo_fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (g_fifo_fd >= 0) {
        _info("ui_status: FIFO opened\n");
    }
    g_current = CLAUDE_STATUS_NONE;
}

void claude_status_deinit(void)
{
    if (g_fifo_fd >= 0) { close(g_fifo_fd); g_fifo_fd = -1; }
    led_write(0x000000);
    g_current = CLAUDE_STATUS_NONE;
}

void claude_status_poll(lv_timer_t *timer)
{
    (void)timer;

    /* 1. Read FIFO first — must process state changes before blinking */
    if (g_fifo_fd < 0) {
        g_fifo_fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    }

    if (g_fifo_fd >= 0) {
        char buf[256];
        ssize_t n = read(g_fifo_fd, buf, sizeof(buf) - 1);
        if (n == 0) { close(g_fifo_fd); g_fifo_fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK); }
        else if (n > 0) {
            buf[n] = '\0';
            char *line = buf;
            for (char *p = buf; *p; p++) {
                if (*p == '\n' || *p == '\r') {
                    *p = '\0';
                    char st_buf[32];
                    const char *st = extract_state(line, st_buf, sizeof(st_buf));
                    claude_status_t new_st = st ? parse_state(st) : CLAUDE_STATUS_NONE;
                    if (new_st != CLAUDE_STATUS_NONE && new_st != g_current) {
                        g_current = new_st;
                        apply_state(new_st);
                        _info("ui_status: -> %s\n", st);
                    }
                    line = p + 1;
                }
            }
        }
    }

    /* 2. Blink — only after state has been updated above */
    if (g_current == CLAUDE_STATUS_PROCESSING) {
        g_tick += POLL_INTERVAL_MS;
        if (g_tick >= BLINK_TOGGLE_MS) {
            g_tick -= BLINK_TOGGLE_MS;
            if (g_led_is_on) {
                g_on = !g_on;
                led_write(g_on ? 0x00FF00 : 0x000000);
            }
        }
    }
}

claude_status_t claude_status_get(void) { return g_current; }
bool claude_status_is_active(void)      { return true; }

void claude_status_reapply(void)
{
    if (!g_led_is_on || g_current == CLAUDE_STATUS_NONE) return;
    apply_state(g_current);
}
