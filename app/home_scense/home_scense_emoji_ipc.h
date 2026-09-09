/****************************************************************************
 * app/home_scense/home_scense_emoji_ipc.h
 * Cross-application Emoji control through a NuttX POSIX message queue.
 ****************************************************************************/

#ifndef HOME_SCENSE_EMOJI_IPC_H
#define HOME_SCENSE_EMOJI_IPC_H

#include <lvgl/lvgl.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stddef.h>
#include <string.h>

#define HOME_SCENSE_EMOJI_MQ       "/home_scense_emoji"
#define HOME_SCENSE_EMOJI_NAME_MAX 16

enum home_scense_emoji_command {
    HOME_SCENSE_EMOJI_PLAY = 1,
    HOME_SCENSE_EMOJI_STOP = 2,
};

struct home_scense_emoji_message {
    int command;
    char name[HOME_SCENSE_EMOJI_NAME_MAX];
};

/* These helpers are header-only so a caller does not need to link home_scense. */
static inline int home_scense_emoji_play(const char *name)
{
    if (!name || !name[0] || strlen(name) >= HOME_SCENSE_EMOJI_NAME_MAX)
        return -EINVAL;

    mqd_t mq = mq_open(HOME_SCENSE_EMOJI_MQ, O_WRONLY | O_NONBLOCK);
    if (mq == (mqd_t)-1) return -errno;

    struct home_scense_emoji_message message;
    memset(&message, 0, sizeof(message));
    message.command = HOME_SCENSE_EMOJI_PLAY;
    strncpy(message.name, name, sizeof(message.name) - 1);

    int ret = mq_send(mq, (const char *)&message, sizeof(message), 0);
    int saved_errno = errno;
    mq_close(mq);
    return ret == 0 ? 0 : -saved_errno;
}

int home_scense_emoji_ipc_init(void);
void home_scense_emoji_ipc_deinit(void);
void home_scense_emoji_ipc_poll(lv_timer_t *timer);

static inline int home_scense_emoji_stop(void)
{
    mqd_t mq = mq_open(HOME_SCENSE_EMOJI_MQ, O_WRONLY | O_NONBLOCK);
    if (mq == (mqd_t)-1) return -errno;

    struct home_scense_emoji_message message;
    memset(&message, 0, sizeof(message));
    message.command = HOME_SCENSE_EMOJI_STOP;

    int ret = mq_send(mq, (const char *)&message, sizeof(message), 0);
    int saved_errno = errno;
    mq_close(mq);
    return ret == 0 ? 0 : -saved_errno;
}

#endif /* HOME_SCENSE_EMOJI_IPC_H */
