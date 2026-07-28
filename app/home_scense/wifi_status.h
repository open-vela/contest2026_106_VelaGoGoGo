/****************************************************************************
 * app/home_scense/wifi_status.h
 * Wi-Fi association, IP readiness, and RSSI state for the UI.
 ****************************************************************************/

#ifndef HOME_SCENSE_WIFI_STATUS_H
#define HOME_SCENSE_WIFI_STATUS_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

typedef struct wifi_status_s
{
    bool associated;
    bool has_ip;
    bool has_rssi;
    int rssi_dbm;
    uint8_t signal_level;
} wifi_status_t;

/* Internet-facing features require an IPv4 lease, not merely association. */
bool wifi_status_is_connected(void);
void wifi_status_get(wifi_status_t *status);
void wifi_status_refresh(lv_timer_t *timer);

#endif /* HOME_SCENSE_WIFI_STATUS_H */
