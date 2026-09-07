/****************************************************************************
 * app/home_scense/wakeup/wakeup.h
 * Hands-free wake-word trigger for the Doubao voice assistant.
 ****************************************************************************/

#ifndef HOME_SCENSE_WAKEUP_H
#define HOME_SCENSE_WAKEUP_H

#include <stdbool.h>

/* Start the background wake-word thread.  It streams the microphone while
 * the assistant is idle, detects the wake word with the pure-C CNN (same
 * model/thresholds as apps/wake_demo), releases the mic, and starts a
 * Doubao session as if 点击说话 had been tapped.  Returns 0 on success. */
int wakeup_init(void);

/* Stop the thread and release the microphone. */
void wakeup_deinit(void);

/* True while the wake thread holds the mic — set optimistically BEFORE the
 * device is opened (claim), cleared only after it is fully released.
 * Doubao polls this before opening its own capture and waits for it to
 * clear: the other half of the mic arbitration handshake. */
bool wakeup_is_recording(void);

#endif /* HOME_SCENSE_WAKEUP_H */
