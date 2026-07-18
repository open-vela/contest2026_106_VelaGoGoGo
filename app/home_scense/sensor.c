/****************************************************************************
 * app/home_scense/sensor.c
 * uORB-based temperature, humidity and proximity sensor driver.
 *
 * Originally part of OpenVela luncher_mini.c.
 ****************************************************************************/

#include "sensor.h"
#include "main.h"

#include <poll.h>
#include <sensor/temp.h>
#include <sensor/humi.h>
#include <sensor/prox.h>
#include <uORB/uORB.h>
#include <syslog.h>

/*-----------------------------------------------------------------------
 * Internal types & state
 *---------------------------------------------------------------------*/
#define POLLFD_NUM 3

typedef struct {
    int           temperature_sub;
    int           humidity_sub;
    int           prox_sub;
    struct pollfd fds[POLLFD_NUM];
    bool          initialized;
} sensor_subscriber_t;

static sensor_subscriber_t s_sub = {
    .temperature_sub = -1,
    .humidity_sub    = -1,
    .prox_sub        = -1,
    .initialized     = false
};

/*-----------------------------------------------------------------------
 * Init subscriptions
 *---------------------------------------------------------------------*/
int sensor_init(void)
{
    syslog(LOG_INFO, "[home_scense] sensor_init: subscribing...\n");
    int count = 0;

    s_sub.temperature_sub = orb_subscribe_multi(ORB_ID(sensor_temp), 0);
    if (s_sub.temperature_sub >= 0) {
        s_sub.fds[count].fd     = s_sub.temperature_sub;
        s_sub.fds[count].events = POLLIN;
        count++;
    }

    s_sub.humidity_sub = orb_subscribe_multi(ORB_ID(sensor_humi), 0);
    if (s_sub.humidity_sub >= 0) {
        s_sub.fds[count].fd     = s_sub.humidity_sub;
        s_sub.fds[count].events = POLLIN;
        count++;
    }

    s_sub.prox_sub = orb_subscribe_multi(ORB_ID(sensor_prox), 0);
    if (s_sub.prox_sub >= 0) {
        s_sub.fds[count].fd     = s_sub.prox_sub;
        s_sub.fds[count].events = POLLIN;
        count++;
    }

    for (int i = count; i < POLLFD_NUM; i++) {
        s_sub.fds[i].fd     = -1;
        s_sub.fds[i].events = 0;
    }

    if (count > 0) {
        s_sub.initialized = true;
        syslog(LOG_INFO, "[home_scense] sensor_init: %d sensors available\n", count);
        return 0;
    }
    syslog(LOG_ERR, "[home_scense] sensor_init: no sensors found\n");
    return -1;
}

/*-----------------------------------------------------------------------
 * Cleanup
 *---------------------------------------------------------------------*/
void sensor_deinit(void)
{
    syslog(LOG_INFO, "[home_scense] sensor_deinit\n");
    if (s_sub.temperature_sub >= 0) {
        orb_unsubscribe(s_sub.temperature_sub);
        s_sub.temperature_sub = -1;
    }
    if (s_sub.humidity_sub >= 0) {
        orb_unsubscribe(s_sub.humidity_sub);
        s_sub.humidity_sub = -1;
    }
    if (s_sub.prox_sub >= 0) {
        orb_unsubscribe(s_sub.prox_sub);
        s_sub.prox_sub = -1;
    }
    s_sub.initialized = false;
}

/*-----------------------------------------------------------------------
 * Poll & update one cycle
 *---------------------------------------------------------------------*/
int sensor_update(sensor_select_t mask, void *ctx)
{
    (void)ctx;
    if (!s_sub.initialized) return -1;

    int valid_count = 0;
    for (int i = 0; i < POLLFD_NUM; i++) {
        if (s_sub.fds[i].fd != -1) valid_count++;
    }
    if (!valid_count) return 0;

    int ret = poll(s_sub.fds, POLLFD_NUM, 100);
    if (ret <= 0) return ret;

    int updated = 0;

    /* Temperature */
    if ((mask & SENSOR_UPDATE_TEMPERATURE) && s_sub.temperature_sub >= 0
        && (s_sub.fds[0].revents & POLLIN)) {
        struct sensor_temp data;
        if (orb_copy(ORB_ID(sensor_temp), s_sub.temperature_sub, &data) == 0) {
            lv_subject_set_int(&temperature_subject, (int)(data.temperature * 10));
            updated++;
        }
    }

    /* Humidity */
    if ((mask & SENSOR_UPDATE_HUMIDITY) && s_sub.humidity_sub >= 0
        && (s_sub.fds[1].revents & POLLIN)) {
        struct sensor_humi data;
        if (orb_copy(ORB_ID(sensor_humi), s_sub.humidity_sub, &data) == 0) {
            lv_subject_set_int(&humidity_subject, (int)(data.humidity * 10));
            updated++;
        }
    }

    /* Proximity */
    if ((mask & SENSOR_UPDATE_PROX) && s_sub.prox_sub >= 0
        && (s_sub.fds[2].revents & POLLIN)) {
        uint8_t raw[32];
        memset(raw, 0, sizeof(raw));
        if (orb_copy(ORB_ID(sensor_prox), s_sub.prox_sub, raw) == 0) {
            float *fp = (float *)(raw + 8);
            float cm = *fp * 100.0f;
            if (cm < 0) cm = 0;
            lv_subject_set_int(&prox_subject, (int)(cm * 10));
            updated++;
        }
    }

    return updated;
}

/*-----------------------------------------------------------------------
 * LVGL timer callback
 *---------------------------------------------------------------------*/
static void sensor_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    sensor_update(SENSOR_UPDATE_ALL, NULL);
}

lv_timer_t *sensor_timer_create(void)
{
    lv_timer_t *t = lv_timer_create(sensor_timer_cb, 1000, NULL);
    if (t) lv_timer_set_repeat_count(t, -1);
    return t;
}
