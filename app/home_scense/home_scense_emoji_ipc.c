/****************************************************************************
 * app/home_scense/home_scense_emoji_ipc.c
 * NuttX message queue endpoint for cross-application Emoji control.
 ****************************************************************************/

#include "home_scense_emoji_ipc.h"
#include "ui_emoji_idle.h"
#include "main.h"

#include <lvgl/lvgl.h>
#include <syslog.h>

#define HOME_SCENSE_EMOJI_MQ_DEPTH 8

static mqd_t g_emoji_mq = (mqd_t)-1;

int home_scense_emoji_ipc_init(void)
{
    struct mq_attr attr;
    attr.mq_maxmsg = HOME_SCENSE_EMOJI_MQ_DEPTH;
    attr.mq_msgsize = sizeof(struct home_scense_emoji_message);
    attr.mq_flags = O_NONBLOCK;
    attr.mq_curmsgs = 0;

    g_emoji_mq = mq_open(HOME_SCENSE_EMOJI_MQ,
                          O_RDWR | O_CREAT | O_NONBLOCK, 0600, &attr);
    if (g_emoji_mq == (mqd_t)-1) {
        syslog(LOG_ERR, "[home_scense] emoji mq_open failed: %d\n", errno);
        return -errno;
    }

    return 0;
}

void home_scense_emoji_ipc_deinit(void)
{
    if (g_emoji_mq != (mqd_t)-1) {
        mq_close(g_emoji_mq);
        mq_unlink(HOME_SCENSE_EMOJI_MQ);
        g_emoji_mq = (mqd_t)-1;
    }
}

void home_scense_emoji_ipc_poll(lv_timer_t *timer)
{
    (void)timer;
    if (g_emoji_mq == (mqd_t)-1) return;

    struct home_scense_emoji_message message;
    while (mq_receive(g_emoji_mq, (char *)&message, sizeof(message), NULL) >= 0) {
        switch (message.command) {
        case HOME_SCENSE_EMOJI_PLAY:
            message.name[sizeof(message.name) - 1] = '\0';
            if (!emoji_idle_play(lv_scr_act(), message.name, NULL)) {
                syslog(LOG_ERR, "[home_scense] unknown emoji: %s\n", message.name);
            }
            break;
        case HOME_SCENSE_EMOJI_STOP:
            emoji_idle_stop();
            break;
        default:
            syslog(LOG_ERR, "[home_scense] unknown emoji command: %d\n",
                   message.command);
            break;
        }
    }
}
