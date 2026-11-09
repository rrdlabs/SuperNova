#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sn_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SN_TRANSPORT_MAX_FRAME (SN_FRAME_HEADER_LEN + SN_MAX_FRAME_PAYLOAD)
#define SN_TRANSPORT_QUEUE_DEPTH 8u
#define SN_TRANSPORT_ESPNOW_MAX_PAYLOAD 250u

typedef struct {
    const char *name;
    void *ctx;
    esp_err_t (*init)(void *ctx);
    esp_err_t (*send)(void *ctx, const uint8_t *data, size_t len);
    esp_err_t (*recv)(void *ctx, uint8_t *buf, size_t cap, size_t *out_len);
    esp_err_t (*deinit)(void *ctx);
} sn_transport_t;

typedef struct {
    uint8_t data[SN_TRANSPORT_MAX_FRAME];
    size_t len;
} sn_transport_packet_t;

typedef struct {
    sn_transport_packet_t queue[SN_TRANSPORT_QUEUE_DEPTH];
    size_t head;
    size_t tail;
    size_t count;
    size_t drop_count;
    size_t duplicate_count;
    bool reorder_pending;
    sn_transport_packet_t reorder_hold;
    bool reorder_hold_valid;
} sn_transport_loopback_t;

typedef struct {
    bool initialized;
    uint8_t peer_mac[6];
    uint8_t channel;
    void *rx_queue;
} sn_transport_espnow_t;

esp_err_t sn_transport_loopback_init(sn_transport_loopback_t *lb);
void sn_transport_loopback_deinit(sn_transport_loopback_t *lb);
void sn_transport_loopback_inject_drop(sn_transport_loopback_t *lb,
                                       size_t count);
void sn_transport_loopback_inject_duplicate(sn_transport_loopback_t *lb,
                                            size_t count);
void sn_transport_loopback_inject_reorder(sn_transport_loopback_t *lb);
sn_transport_t sn_transport_loopback_as_transport(sn_transport_loopback_t *lb);

esp_err_t sn_transport_espnow_init(sn_transport_espnow_t *en);
esp_err_t sn_transport_espnow_configure(sn_transport_espnow_t *en,
                                        const uint8_t peer_mac[6],
                                        uint8_t channel);
void sn_transport_espnow_deinit(sn_transport_espnow_t *en);
sn_transport_t sn_transport_espnow_as_transport(sn_transport_espnow_t *en);

#ifdef __cplusplus
}
#endif
