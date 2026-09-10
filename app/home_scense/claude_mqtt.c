#include "claude_mqtt.h"
#include "ui_emoji_idle.h"
#include "main.h"
#include "led_control.h"
#include <mqtt.h>
#include <pthread.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <syslog.h>

#define BROKER "test.mosquitto.org"
#define PORT "1883"
#define TOPIC "vela-go/claude-code/status/v1"
#define LEASE_SECONDS 20

static pthread_t g_thread;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile bool g_running;
static bool g_started;
static char g_state[16] = "default";
static char g_pending[16] = "default";
static bool g_changed;

struct mqtt_ctx { uint8_t sendbuf[2048]; uint8_t recvbuf[2048]; };

static int open_socket(void)
{
    struct addrinfo hints = {0}, *res, *p;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    int dns_rc = getaddrinfo(BROKER, PORT, &hints, &res);
    if (dns_rc != 0) {
        syslog(LOG_INFO, "[claude-mqtt] DNS failed for %s:%s rc=%d\n", BROKER, PORT, dns_rc);
        return -1;
    }
    int fd = -1;
    for (p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd >= 0 && connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            syslog(LOG_INFO, "[claude-mqtt] TCP connected to %s:%s\n", BROKER, PORT);
            break;
        }
        if (fd >= 0) { syslog(LOG_INFO, "[claude-mqtt] TCP connect failed errno=%d\n", errno); close(fd); fd = -1; }
    }
    freeaddrinfo(res);
    /* MQTT-C requires a non-blocking socket (mqtt_pal_recvall treats
     * EAGAIN/EWOULDBLOCK as "no data"). Without this, mqtt_sync blocks in
     * recv(), CONNECT never gets sent, and the broker drops the idle socket
     * after ~30s -> SOCKET_ERROR reconnect loop. */
    if (fd >= 0)
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

static void publish_cb(void **unused, struct mqtt_response_publish *p)
{
    char state[16];
    (void)unused;
    if (p->topic_name_size != sizeof(TOPIC) - 1 ||
        memcmp(p->topic_name, TOPIC, sizeof(TOPIC) - 1) != 0 ||
        p->application_message_size >= sizeof(state)) {
        syslog(LOG_INFO, "[claude-mqtt] ignored publish topic_len=%u payload_len=%u\n",
               p->topic_name_size, (unsigned)p->application_message_size);
        return;
    }
    memcpy(state, p->application_message, p->application_message_size);
    state[p->application_message_size] = '\0';
    if (strcmp(state, "online") && strcmp(state, "offline") &&
        strcmp(state, "idle") && strcmp(state, "thinking") && strcmp(state, "executing")) {
        syslog(LOG_INFO, "[claude-mqtt] ignored state=%s\n", state);
        return;
    }
    syslog(LOG_INFO, "[claude-mqtt] received state=%s\n", state);
    pthread_mutex_lock(&g_lock);
    if (!strcmp(state, "online")) {
        strcpy(g_pending, "idle");
    } else if (!strcmp(state, "offline")) {
        strcpy(g_pending, "default");
    } else {
        strcpy(g_pending, state);
    }
    g_changed = true;
    pthread_mutex_unlock(&g_lock);
}

static void *mqtt_worker(void *arg)
{
    struct mqtt_ctx *ctx = arg;
    struct mqtt_client client;

    syslog(LOG_INFO, "[claude-mqtt] worker entered\n");

    while (g_running) {
        /* 1. Establish TCP to the broker (waits for Wi-Fi/DNS to come up). */
        int fd = open_socket();
        if (fd < 0) {
            sleep(3);           /* backoff: avoid CPU/log flood before network is up */
            continue;
        }

        /* 2. Fresh client each connection: init + CONNECT + SUBSCRIBE. */
        if (mqtt_init(&client, fd, ctx->sendbuf, sizeof(ctx->sendbuf),
                      ctx->recvbuf, sizeof(ctx->recvbuf), publish_cb) != MQTT_OK) {
            syslog(LOG_INFO, "[claude-mqtt] mqtt_init failed\n");
            close(fd);
            sleep(3);
            continue;
        }
        mqtt_connect(&client, "vela-home-scense", NULL, NULL, 0, NULL, NULL,
                     MQTT_CONNECT_CLEAN_SESSION, 60);
        if (client.error != MQTT_OK) {
            syslog(LOG_INFO, "[claude-mqtt] connect failed, retrying\n");
            close(fd);
            sleep(3);
            continue;
        }
        mqtt_subscribe(&client, TOPIC, 0);
        syslog(LOG_INFO, "[claude-mqtt] connected + subscribed, topic=%s\n", TOPIC);

        /* 3. Pump traffic until an error, then reconnect. */
        while (g_running) {
            enum MQTTErrors err = mqtt_sync(&client);
            if (err != MQTT_OK) {
                syslog(LOG_INFO, "[claude-mqtt] sync error=%s, reconnecting\n",
                       mqtt_error_str(err));
                break;
            }
            usleep(100000);
        }
        close(fd);
        sleep(1);
    }
    return NULL;
}

void claude_mqtt_init(void)
{
    static struct mqtt_ctx ctx;
    pthread_attr_t attr;
    int rc;

    if (g_started) {
        syslog(LOG_INFO, "[claude-mqtt] init skipped: already started\n");
        return;
    }
    syslog(LOG_INFO, "[claude-mqtt] init begin\n");
    g_running = true;
    pthread_attr_init(&attr);
    rc = pthread_attr_setstacksize(&attr, 65536);
    if (rc != 0)
        syslog(LOG_INFO, "[claude-mqtt] pthread_attr_setstacksize failed rc=%d\n", rc);
    rc = pthread_create(&g_thread, &attr, mqtt_worker, &ctx);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        syslog(LOG_INFO, "[claude-mqtt] pthread_create failed rc=%d\n", rc);
        g_running = false;
        return;
    }
    g_started = true;
    syslog(LOG_INFO, "[claude-mqtt] worker created\n");
}

/* Map an MQTT status to the emoji_blob resource driving the shared animation,
 * or NULL to withdraw the Claude request (overlay released -> idle rotation).
 * Names must match g_emoji_names in emoji_blob.h. */
static const char *claude_emoji_name(const char *state)
{
    if (!strcmp(state, "executing")) return "claude-executing";
    if (!strcmp(state, "thinking"))  return "claude-thinking";
    if (!strcmp(state, "idle"))      return "claude-idle";
    return NULL;   /* "default" / offline: no agent animation */
}

void claude_mqtt_poll(lv_timer_t *timer)
{
    static uint32_t lease;
    char state[16];
    bool changed;
    (void)timer;
    pthread_mutex_lock(&g_lock);
    changed = g_changed;
    if (changed) { strcpy(state, g_pending); strcpy(g_state, g_pending); g_changed = false; lease = lv_tick_get(); }
    else strcpy(state, g_state);
    pthread_mutex_unlock(&g_lock);
    if (changed || !strcmp(state, "default") || lv_tick_elaps(lease) < LEASE_SECONDS * 1000) {
        /* 通过 emoji_idle 仲裁接口投递 Agent 状态动画。CLAUDE 源优先级低于
         * VOICE:豆包 listen/speak 期间语音动画胜出,不会被状态动画抢占。 */
        emoji_idle_request(EMOJI_SRC_CLAUDE, claude_emoji_name(state));
        /* 手电筒开启时独占 LED,状态灯只更新 UI 不抢灯(否则会周期性熄灭手电筒)。 */
        if (!g_flashlight_override) {
            if (!strcmp(state, "default")) { led_off(); }
            else if (!strcmp(state, "idle")) { led_set_mode_static(LED_COLOR_BLUE); led_on(); }
            else { led_set_mode_blink(LED_COLOR_GREEN, 2); led_on(); }
        }
    } else {
        pthread_mutex_lock(&g_lock); strcpy(g_state, "default"); pthread_mutex_unlock(&g_lock);
        emoji_idle_request(EMOJI_SRC_CLAUDE, NULL);
        if (!g_flashlight_override) { led_off(); }
    }
}

void claude_mqtt_deinit(void)
{
    if (!g_started) return;
    g_running = false;
    pthread_join(g_thread, NULL);
    g_started = false;
}
