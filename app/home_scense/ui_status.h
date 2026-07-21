/****************************************************************************
 * app/home_scense/ui_status.h
 * Claude Code status receiver — reads FIFO and controls LED state.
 ****************************************************************************/

#ifndef HOME_SCENSE_UI_STATUS_H
#define HOME_SCENSE_UI_STATUS_H

#include <stdbool.h>
#include <stdint.h>
#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status states received from Claude Code adapter */
typedef enum {
    CLAUDE_STATUS_NONE = 0,
    CLAUDE_STATUS_PROCESSING,   /* Green blink */
    CLAUDE_STATUS_COMPLETED,    /* Legacy */
    CLAUDE_STATUS_WAITING,      /* Red static */
    CLAUDE_STATUS_STARTED,      /* LED off */
    CLAUDE_STATUS_ENDED         /* LED off */
} claude_status_t;

/* Init / Deinit */
void claude_status_init(void);
void claude_status_deinit(void);

/* Timer callback — call from main loop timer (100ms) */
void claude_status_poll(lv_timer_t *timer);

/* Get current status */
claude_status_t claude_status_get(void);

/* Check if status receiver is active */
bool claude_status_is_active(void);

/* Re-apply current status to LED (call when light switch turned ON) */
void claude_status_reapply(void);

#ifdef __cplusplus
}
#endif

#endif /* HOME_SCENSE_UI_STATUS_H */
