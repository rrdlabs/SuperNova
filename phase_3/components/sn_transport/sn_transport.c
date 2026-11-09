#include "sn_transport.h"

#include <string.h>

#ifdef CONFIG_SN_ENABLE_ESPNOW
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#endif

/* ------------------------------------------------------------------------- */
/* Deterministic loopback transport                                          */
/* ------------------------------------------------------------------------- */

esp_err_t sn_transport_loopback_init(sn_transport_loopback_t *lb)
{
    if (lb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(lb, 0, sizeof(*lb));
    return ESP_OK;
}

void sn_transport_loopback_deinit(sn_transport_loopback_t *lb)
{
    if (lb != NULL) {
        memset(lb, 0, sizeof(*lb));
    }
}

void sn_transport_loopback_inject_drop(sn_transport_loopback_t *lb,
                                       size_t count)
{
    if (lb != NULL && count <= SIZE_MAX - lb->drop_count) {
        lb->drop_count += count;
    }
}

void sn_transport_loopback_inject_duplicate(sn_transport_loopback_t *lb,
                                            size_t count)
{
    if (lb != NULL && count <= SIZE_MAX - lb->duplicate_count) {
        lb->duplicate_count += count;
    }
}

void sn_transport_loopback_inject_reorder(sn_transport_loopback_t *lb)
{
    if (lb != NULL) {
        lb->reorder_pending = true;
    }
}

static esp_err_t loopback_enqueue(sn_transport_loopback_t *lb,
                                  const uint8_t *data, size_t len)
{
    if (len > SN_TRANSPORT_MAX_FRAME || (len > 0 && data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (lb->count >= SN_TRANSPORT_QUEUE_DEPTH) {
        return ESP_ERR_NO_MEM;
    }
    sn_transport_packet_t *packet = &lb->queue[lb->tail];
    if (len > 0) {
        memcpy(packet->data, data, len);
    }
    packet->len = len;
    lb->tail = (lb->tail + 1u) % SN_TRANSPORT_QUEUE_DEPTH;
    lb->count++;
    return ESP_OK;
}

static esp_err_t loopback_send(void *ctx, const uint8_t *data, size_t len)
{
    sn_transport_loopback_t *lb = ctx;
    if (lb == NULL || len > SN_TRANSPORT_MAX_FRAME ||
        (len > 0 && data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (lb->drop_count > 0) {
        lb->drop_count--;
        return ESP_OK;
    }
    if (lb->reorder_pending) {
        if (!lb->reorder_hold_valid) {
            if (len > 0) {
                memcpy(lb->reorder_hold.data, data, len);
            }
            lb->reorder_hold.len = len;
            lb->reorder_hold_valid = true;
            return ESP_OK;
        }
        if (SN_TRANSPORT_QUEUE_DEPTH - lb->count < 2u) {
            return ESP_ERR_NO_MEM;
        }
        /* Deliver the held frame first, then the current frame. */
        esp_err_t err = loopback_enqueue(lb, lb->reorder_hold.data,
                                         lb->reorder_hold.len);
        if (err != ESP_OK) {
            return err;
        }
        lb->reorder_hold_valid = false;
        lb->reorder_pending = false;
        err = loopback_enqueue(lb, data, len);
        if (err != ESP_OK) {
            return err;
        }
        if (lb->duplicate_count > 0 &&
            lb->count < SN_TRANSPORT_QUEUE_DEPTH) {
            (void)loopback_enqueue(lb, data, len);
            lb->duplicate_count--;
        }
        return ESP_OK;
    }
    esp_err_t err = loopback_enqueue(lb, data, len);
    if (err != ESP_OK) {
        return err;
    }
    if (lb->duplicate_count > 0 && lb->count < SN_TRANSPORT_QUEUE_DEPTH) {
        (void)loopback_enqueue(lb, data, len);
        lb->duplicate_count--;
    }
    return ESP_OK;
}

static esp_err_t loopback_recv(void *ctx, uint8_t *buf, size_t cap,
                               size_t *out_len)
{
    sn_transport_loopback_t *lb = ctx;
    if (lb == NULL || (cap > 0 && buf == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (lb->count == 0) {
        return ESP_ERR_TIMEOUT;
    }
    sn_transport_packet_t *packet = &lb->queue[lb->head];
    if (cap < packet->len) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (packet->len > 0) {
        memcpy(buf, packet->data, packet->len);
    }
    if (out_len != NULL) {
        *out_len = packet->len;
    }
    lb->head = (lb->head + 1u) % SN_TRANSPORT_QUEUE_DEPTH;
    lb->count--;
    return ESP_OK;
}

static esp_err_t loopback_init_cb(void *ctx)
{
    return sn_transport_loopback_init((sn_transport_loopback_t *)ctx);
}

static esp_err_t loopback_deinit_cb(void *ctx)
{
    sn_transport_loopback_deinit((sn_transport_loopback_t *)ctx);
    return ESP_OK;
}

sn_transport_t sn_transport_loopback_as_transport(sn_transport_loopback_t *lb)
{
    sn_transport_t transport = {
        .name = "loopback",
        .ctx = lb,
        .init = loopback_init_cb,
        .send = loopback_send,
        .recv = loopback_recv,
        .deinit = loopback_deinit_cb,
    };
    return transport;
}

/* ------------------------------------------------------------------------- */
/* ESP-NOW adapter, compiled only when explicitly enabled                    */
/* ------------------------------------------------------------------------- */

#ifdef CONFIG_SN_ENABLE_ESPNOW

static sn_transport_espnow_t *s_espnow_active;

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data,
                           int len)
{
    (void)info;
    if (s_espnow_active == NULL || s_espnow_active->rx_queue == NULL ||
        data == NULL || len <= 0 || (size_t)len > SN_TRANSPORT_MAX_FRAME) {
        return;
    }
    sn_transport_packet_t packet;
    packet.len = (size_t)len;
    memcpy(packet.data, data, packet.len);
    QueueHandle_t queue = (QueueHandle_t)s_espnow_active->rx_queue;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(queue, &packet, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t sn_transport_espnow_configure(sn_transport_espnow_t *en,
                                        const uint8_t peer_mac[6],
                                        uint8_t channel)
{
    if (en == NULL || peer_mac == NULL || channel == 0 || channel > 14) {
        return ESP_ERR_INVALID_ARG;
    }
    if (en->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(en->peer_mac, peer_mac, 6);
    en->channel = channel;
    return ESP_OK;
}

esp_err_t sn_transport_espnow_init(sn_transport_espnow_t *en)
{
    if (en == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (en->initialized) {
        return ESP_OK;
    }
    bool default_peer = true;
    for (size_t i = 0; i < sizeof(en->peer_mac); i++) {
        if (en->peer_mac[i] != 0) {
            default_peer = false;
            break;
        }
    }
    if (default_peer) {
        memset(en->peer_mac, 0xFF, sizeof(en->peer_mac));
    }
    if (en->channel == 0) {
        en->channel = 1;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_cfg);
    if (err != ESP_OK) {
        return err;
    }
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_channel(en->channel, WIFI_SECOND_CHAN_NONE);
    }
    if (err == ESP_OK) {
        err = esp_now_init();
    }
    if (err == ESP_OK) {
        err = esp_now_register_recv_cb(espnow_recv_cb);
    }
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, en->peer_mac, 6);
    peer.channel = en->channel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if (err == ESP_OK) {
        err = esp_now_add_peer(&peer);
    }
    if (err != ESP_OK) {
        (void)esp_now_deinit();
        (void)esp_wifi_stop();
        (void)esp_wifi_deinit();
        return err;
    }
    QueueHandle_t queue =
        xQueueCreate(4, sizeof(sn_transport_packet_t));
    if (queue == NULL) {
        (void)esp_now_deinit();
        (void)esp_wifi_stop();
        (void)esp_wifi_deinit();
        return ESP_ERR_NO_MEM;
    }
    en->rx_queue = queue;
    s_espnow_active = en;
    en->initialized = true;
    return ESP_OK;
}

static esp_err_t espnow_send(void *ctx, const uint8_t *data, size_t len)
{
    sn_transport_espnow_t *en = ctx;
    if (en == NULL || !en->initialized) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (data == NULL || len == 0 || len > SN_TRANSPORT_ESPNOW_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }
    return esp_now_send(en->peer_mac, data, len) == ESP_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t espnow_recv(void *ctx, uint8_t *buf, size_t cap,
                             size_t *out_len)
{
    sn_transport_espnow_t *en = ctx;
    if (en == NULL || !en->initialized || en->rx_queue == NULL ||
        (cap > 0 && buf == NULL)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    sn_transport_packet_t packet;
    QueueHandle_t queue = (QueueHandle_t)en->rx_queue;
    if (xQueueReceive(queue, &packet, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (cap < packet.len) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (packet.len > 0) {
        memcpy(buf, packet.data, packet.len);
    }
    if (out_len != NULL) {
        *out_len = packet.len;
    }
    return ESP_OK;
}

void sn_transport_espnow_deinit(sn_transport_espnow_t *en)
{
    if (en == NULL) {
        return;
    }
    if (en->initialized) {
        (void)esp_now_deinit();
        (void)esp_wifi_stop();
        (void)esp_wifi_deinit();
        if (en->rx_queue != NULL) {
            vQueueDelete((QueueHandle_t)en->rx_queue);
        }
        if (s_espnow_active == en) {
            s_espnow_active = NULL;
        }
    }
    memset(en, 0, sizeof(*en));
}

#else /* !CONFIG_SN_ENABLE_ESPNOW */

esp_err_t sn_transport_espnow_configure(sn_transport_espnow_t *en,
                                        const uint8_t peer_mac[6],
                                        uint8_t channel)
{
    (void)en;
    (void)peer_mac;
    (void)channel;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sn_transport_espnow_init(sn_transport_espnow_t *en)
{
    if (en == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(en, 0, sizeof(*en));
    /* Radios are disabled by default and are not touched in this build. */
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t espnow_send(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    (void)data;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t espnow_recv(void *ctx, uint8_t *buf, size_t cap,
                             size_t *out_len)
{
    (void)ctx;
    (void)buf;
    (void)cap;
    (void)out_len;
    return ESP_ERR_NOT_SUPPORTED;
}

void sn_transport_espnow_deinit(sn_transport_espnow_t *en)
{
    if (en != NULL) {
        memset(en, 0, sizeof(*en));
    }
}

#endif /* CONFIG_SN_ENABLE_ESPNOW */

static esp_err_t espnow_init_cb(void *ctx)
{
    return sn_transport_espnow_init((sn_transport_espnow_t *)ctx);
}

static esp_err_t espnow_deinit_cb(void *ctx)
{
    sn_transport_espnow_deinit((sn_transport_espnow_t *)ctx);
    return ESP_OK;
}

sn_transport_t sn_transport_espnow_as_transport(sn_transport_espnow_t *en)
{
    sn_transport_t transport = {
        .name = "esp-now",
        .ctx = en,
        .init = espnow_init_cb,
        .send = espnow_send,
        .recv = espnow_recv,
        .deinit = espnow_deinit_cb,
    };
    return transport;
}
