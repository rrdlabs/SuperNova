#include <string.h>

#include "sn_app_internal.h"

/* The loopback queue is large; keep it out of the main-task stack. Tests are
 * single-threaded and reinitialize it before each use. */
static sn_transport_loopback_t s_loopback;

static esp_err_t make_tx(sn_tx_t *tx, const sn_test_input_t *inputs,
                         size_t input_count, const sn_test_output_t *outputs,
                         size_t output_count)
{
    esp_err_t err =
        sn_test_build_tx(tx, inputs, input_count, outputs, output_count);
    if (err != ESP_OK) {
        return err;
    }
    return sn_test_sign_tx(tx);
}

static esp_err_t encode_tx_frame(const sn_tx_t *tx, uint8_t *frame,
                                 size_t cap, size_t *frame_len)
{
    uint8_t payload[SN_MAX_TX_BYTES];
    size_t payload_len = 0;
    esp_err_t err = sn_tx_encode(tx, payload, sizeof(payload), &payload_len);
    if (err != ESP_OK) {
        return err;
    }
    return sn_frame_encode(SN_FRAME_TYPE_TX, payload, payload_len, frame, cap,
                           frame_len);
}

static esp_err_t recv_tx_frame(sn_transport_t *transport, sn_tx_t *tx)
{
    uint8_t frame[SN_TRANSPORT_MAX_FRAME];
    size_t frame_len = 0;
    esp_err_t err =
        transport->recv(transport->ctx, frame, sizeof(frame), &frame_len);
    if (err != ESP_OK) {
        return err;
    }
    sn_frame_type_t type;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    err = sn_frame_decode(frame, frame_len, &type, &payload, &payload_len);
    if (err != ESP_OK) {
        return err;
    }
    if (type != SN_FRAME_TYPE_TX) {
        return ESP_ERR_INVALID_ARG;
    }
    return sn_tx_decode(payload, payload_len, tx);
}

static void test_loopback(sn_test_ctx_t *ctx)
{
    sn_transport_loopback_t *lb = &s_loopback;
    esp_err_t err = sn_transport_loopback_init(lb);
    sn_test_expect_ok(ctx, "transport.loopback.init", "initialized", err);
    sn_transport_t transport = sn_transport_loopback_as_transport(lb);

    const uint8_t payload[] = {0x53, 0x4E, 0x32, 0x01};
    uint8_t frame[SN_TRANSPORT_MAX_FRAME];
    size_t frame_len = 0;
    err = sn_frame_encode(SN_FRAME_TYPE_TX, payload, sizeof(payload), frame,
                          sizeof(frame), &frame_len);
    if (err == ESP_OK) {
        err = transport.send(transport.ctx, frame, frame_len);
    }
    sn_test_expect_ok(ctx, "transport.loopback.send", "frame sent", err);
    uint8_t received[SN_TRANSPORT_MAX_FRAME];
    size_t received_len = 0;
    err = transport.recv(transport.ctx, received, sizeof(received),
                         &received_len);
    sn_test_expect_true(ctx, "transport.loopback", "exact bytes delivered",
                        err == ESP_OK && received_len == frame_len &&
                            memcmp(received, frame, frame_len) == 0,
                        "match");

    sn_transport_loopback_inject_drop(lb, 1);
    err = transport.send(transport.ctx, frame, frame_len);
    if (err == ESP_OK) {
        err = transport.recv(transport.ctx, received, sizeof(received),
                             &received_len);
    }
    sn_test_expect_err(ctx, "transport.drop", "receive times out", err,
                       ESP_ERR_TIMEOUT);

    sn_transport_loopback_inject_duplicate(lb, 1);
    err = transport.send(transport.ctx, frame, frame_len);
    if (err == ESP_OK) {
        err = transport.recv(transport.ctx, received, sizeof(received),
                             &received_len);
    }
    sn_test_expect_ok(ctx, "transport.duplicate.first", "first copy delivered",
                      err);
    err = transport.recv(transport.ctx, received, sizeof(received),
                         &received_len);
    sn_test_expect_ok(ctx, "transport.duplicate", "duplicate delivered", err);
    err = transport.recv(transport.ctx, received, sizeof(received),
                         &received_len);
    sn_test_expect_err(ctx, "transport.duplicate.end", "queue empty", err,
                       ESP_ERR_TIMEOUT);
}

static void test_reorder(sn_test_ctx_t *ctx)
{
    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_genesis_output_t genesis[1];
    const sn_test_key_t *alice = sn_app_test_key(4);
    genesis[0].value = 1000;
    memcpy(genesis[0].owner_pubkey, alice->pubkey, SN_PUBKEY_LEN);
    uint8_t zero[SN_HASH_LEN];
    memset(zero, 0, sizeof(zero));
    (void)sn_ledger_genesis(&ledger, genesis, 1, 0, zero);

    sn_test_input_t parent_in;
    (void)sn_test_genesis_input(&parent_in, 0, 4);
    sn_test_output_t parent_out[1] = {{1000, 5}};
    sn_tx_t parent;
    esp_err_t err = make_tx(&parent, &parent_in, 1, parent_out, 1);
    sn_test_expect_ok(ctx, "transport.reorder.parent", "parent built", err);

    uint8_t parent_txid[SN_TXID_LEN];
    (void)sn_tx_id(&parent, parent_txid);
    sn_test_input_t child_in;
    memcpy(child_in.txid, parent_txid, SN_TXID_LEN);
    child_in.output_index = 0;
    child_in.key_index = 5;
    sn_test_output_t child_out[1] = {{1000, 4}};
    sn_tx_t child;
    err = make_tx(&child, &child_in, 1, child_out, 1);
    sn_test_expect_ok(ctx, "transport.reorder.child", "child built", err);

    uint8_t parent_frame[SN_TRANSPORT_MAX_FRAME];
    uint8_t child_frame[SN_TRANSPORT_MAX_FRAME];
    size_t parent_len = 0;
    size_t child_len = 0;
    (void)encode_tx_frame(&parent, parent_frame, sizeof(parent_frame),
                          &parent_len);
    (void)encode_tx_frame(&child, child_frame, sizeof(child_frame), &child_len);

    sn_transport_loopback_t *lb = &s_loopback;
    (void)sn_transport_loopback_init(lb);
    sn_transport_t transport = sn_transport_loopback_as_transport(lb);
    sn_transport_loopback_inject_reorder(lb);
    /* The child is sent first but held; the parent is sent second. The held
     * child is delivered first, reproducing an out-of-order arrival. */
    (void)transport.send(transport.ctx, child_frame, child_len);
    (void)transport.send(transport.ctx, parent_frame, parent_len);

    sn_tx_t received;
    err = recv_tx_frame(&transport, &received);
    sn_test_expect_ok(ctx, "transport.reorder.recv.child", "child received",
                      err);
    sn_ledger_report_t report;
    err = sn_ledger_apply_tx(&ledger, &received, &report);
    sn_test_expect_err(ctx, "transport.reorder.child_first",
                       "child rejected before parent", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "transport.reorder.child_code",
                        "unknown input result",
                        report.code == SN_LEDGER_ERR_UNKNOWN_INPUT,
                        sn_ledger_result_name(report.code));

    err = recv_tx_frame(&transport, &received);
    sn_test_expect_ok(ctx, "transport.reorder.recv.parent", "parent received",
                      err);
    err = sn_ledger_apply_tx(&ledger, &received, &report);
    sn_test_expect_ok(ctx, "transport.reorder.parent_applied",
                      "parent accepted", err);

    (void)transport.send(transport.ctx, child_frame, child_len);
    err = recv_tx_frame(&transport, &received);
    if (err == ESP_OK) {
        err = sn_ledger_apply_tx(&ledger, &received, &report);
    }
    sn_test_expect_ok(ctx, "transport.reorder.child_after",
                      "child accepted after parent", err);
    sn_test_expect_true(ctx, "transport.reorder.balances",
                        "final balances restored",
                        sn_ledger_balance(&ledger, alice->pubkey) == 1000,
                        "alice=1000");
}

static void test_espnow_disabled(sn_test_ctx_t *ctx)
{
    sn_transport_espnow_t espnow;
    memset(&espnow, 0, sizeof(espnow));
    esp_err_t err = sn_transport_espnow_init(&espnow);
    sn_test_expect_err(ctx, "transport.espnow.disabled", "not supported", err,
                       ESP_ERR_NOT_SUPPORTED);
    sn_transport_t transport = sn_transport_espnow_as_transport(&espnow);
    uint8_t frame[SN_TRANSPORT_MAX_FRAME];
    size_t frame_len = 0;
    (void)sn_frame_encode(SN_FRAME_TYPE_TX, (const uint8_t *)"x", 1, frame,
                          sizeof(frame), &frame_len);
    err = transport.send(transport.ctx, frame, frame_len);
    sn_test_expect_err(ctx, "transport.espnow.send_disabled", "not supported",
                       err, ESP_ERR_NOT_SUPPORTED);
}

void sn_app_test_transport(sn_test_ctx_t *ctx)
{
    test_loopback(ctx);
    test_reorder(ctx);
    test_espnow_disabled(ctx);
}
