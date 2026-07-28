/****************************************************************************
 * app/home_scense/wifi_status.c
 * Wi-Fi association, IP readiness, and RSSI sampling for the status bar.
 ****************************************************************************/

#include "wifi_status.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <nuttx/wireless/wireless.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define WIFI_INTERFACE "wlan0"

static wifi_status_t g_status;

static void set_interface_name(struct iwreq *request)
{
    strncpy(request->ifr_name, WIFI_INTERFACE, IFNAMSIZ - 1);
    request->ifr_name[IFNAMSIZ - 1] = '\0';
}

static bool has_access_point(const struct iwreq *request)
{
    const uint8_t *address =
        (const uint8_t *)request->u.ap_addr.sa_data;
    size_t i;

    for (i = 0; i < 6; i++) {
        if (address[i] != 0) return true;
    }
    return false;
}

static uint8_t signal_level_for_rssi(int rssi_dbm, uint8_t previous_level)
{
    int level = previous_level;

    if (level == 4 && rssi_dbm >= -58) return 4;
    if (level == 3 && rssi_dbm >= -70 && rssi_dbm < -53) return 3;
    if (level == 2 && rssi_dbm >= -83 && rssi_dbm < -65) return 2;
    if (level == 1 && rssi_dbm < -77) return 1;

    if (rssi_dbm >= -55) return 4;
    if (rssi_dbm >= -68) return 3;
    if (rssi_dbm >= -80) return 2;
    return 1;
}

bool wifi_status_is_connected(void)
{
    return g_status.has_ip;
}

void wifi_status_get(wifi_status_t *status)
{
    if (status) *status = g_status;
}

void wifi_status_refresh(lv_timer_t *timer)
{
    struct ifreq ifr;
    struct iwreq iwreq;
    struct sockaddr_in *address;
    wifi_status_t next;
    int fd;

    (void)timer;
    memset(&next, 0, sizeof(next));
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        g_status = next;
        return;
    }

    memset(&iwreq, 0, sizeof(iwreq));
    set_interface_name(&iwreq);
    if (ioctl(fd, SIOCGIWAP, &iwreq) == 0 && has_access_point(&iwreq)) {
        next.associated = true;

        memset(&iwreq, 0, sizeof(iwreq));
        set_interface_name(&iwreq);
        if (ioctl(fd, SIOCGIWSENS, &iwreq) == 0 && iwreq.u.sens.value > 0) {
            next.has_rssi = true;
            next.rssi_dbm = -iwreq.u.sens.value;
            next.signal_level = signal_level_for_rssi(next.rssi_dbm,
                                                       g_status.signal_level);
        }
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, WIFI_INTERFACE, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
        address = (struct sockaddr_in *)&ifr.ifr_addr;
        next.has_ip = address->sin_addr.s_addr != INADDR_ANY;
    }
    close(fd);
    g_status = next;
}
