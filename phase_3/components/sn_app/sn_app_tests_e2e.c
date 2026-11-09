#include <stdlib.h>
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

static esp_err_t prepare_block(sn_block_t *block, const sn_ledger_t *ledger,
                               const sn_tx_t *txs, size_t tx_count,
                               const uint8_t proposer[SN_PUBKEY_LEN],
                               uint64_t timestamp)
{
    uint8_t state_root[SN_HASH_LEN];
    esp_err_t err = sn_test_state_after(ledger, txs, tx_count, state_root);
    if (err != ESP_OK) {
        return err;
    }
    return sn_test_build_block(block, ledger, txs, tx_count,
                               ledger->applied_height + 1u, timestamp,
                               proposer, state_root);
}

static esp_err_t send_tx_over_loopback(sn_transport_t *transport,
                                       const sn_tx_t *tx, sn_tx_t *received)
{
    uint8_t payload[SN_MAX_TX_BYTES];
    size_t payload_len = 0;
    esp_err_t err = sn_tx_encode(tx, payload, sizeof(payload), &payload_len);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t frame[SN_TRANSPORT_MAX_FRAME];
    size_t frame_len = 0;
    err = sn_frame_encode(SN_FRAME_TYPE_TX, payload, payload_len, frame,
                          sizeof(frame), &frame_len);
    if (err != ESP_OK) {
        return err;
    }
    err = transport->send(transport->ctx, frame, frame_len);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t incoming[SN_TRANSPORT_MAX_FRAME];
    size_t incoming_len = 0;
    err = transport->recv(transport->ctx, incoming, sizeof(incoming),
                          &incoming_len);
    if (err != ESP_OK) {
        return err;
    }
    sn_frame_type_t type;
    const uint8_t *incoming_payload = NULL;
    size_t incoming_payload_len = 0;
    err = sn_frame_decode(incoming, incoming_len, &type, &incoming_payload,
                          &incoming_payload_len);
    if (err != ESP_OK || type != SN_FRAME_TYPE_TX) {
        return err != ESP_OK ? err : ESP_ERR_INVALID_ARG;
    }
    return sn_tx_decode(incoming_payload, incoming_payload_len, received);
}

static esp_err_t cast_accept_votes(sn_consensus_t *consensus, size_t start,
                                   size_t count)
{
    for (size_t i = start; i < start + count; i++) {
        sn_signer_t signer = sn_app_test_signer(i);
        if (signer.sign_digest == NULL) {
            return ESP_ERR_INVALID_STATE;
        }
        sn_vote_t vote;
        esp_err_t err = sn_consensus_sign_vote(
            &signer, (uint8_t)i, consensus->height, consensus->round,
            consensus->block_hash, SN_VOTE_ACCEPT, &vote);
        if (err != ESP_OK) {
            return err;
        }
        err = sn_consensus_cast_vote(consensus, &vote);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t init_consensus(sn_consensus_t *consensus)
{
    sn_validator_t members[SN_TEST_VALIDATOR_COUNT];
    memset(members, 0, sizeof(members));
    for (size_t i = 0; i < SN_TEST_VALIDATOR_COUNT; i++) {
        const sn_test_key_t *key = sn_app_test_key(i);
        if (key == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        members[i].id = (uint8_t)i;
        memcpy(members[i].pubkey, key->pubkey, SN_PUBKEY_LEN);
    }
    return sn_consensus_init(consensus, members, SN_TEST_VALIDATOR_COUNT, 3, 10);
}

static void test_happy_path(sn_test_ctx_t *ctx)
{
    const sn_test_key_t *alice = sn_app_test_key(4);
    const sn_test_key_t *bob = sn_app_test_key(5);
    const sn_test_key_t *proposer = sn_app_test_key(0);

    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_genesis_output_t genesis[1];
    genesis[0].value = 1000;
    memcpy(genesis[0].owner_pubkey, alice->pubkey, SN_PUBKEY_LEN);
    uint8_t zero[SN_HASH_LEN];
    memset(zero, 0, sizeof(zero));
    esp_err_t err = sn_ledger_genesis(&ledger, genesis, 1, 0, zero);
    sn_test_expect_ok(ctx, "e2e.happy.genesis", "genesis accepted", err);

    sn_test_input_t in;
    (void)sn_test_genesis_input(&in, 0, 4);
    sn_test_output_t outputs[1] = {{1000, 5}};
    sn_tx_t tx;
    err = make_tx(&tx, &in, 1, outputs, 1);
    sn_test_expect_ok(ctx, "e2e.happy.sign", "transaction signed", err);

    sn_transport_loopback_t *lb = &s_loopback;
    (void)sn_transport_loopback_init(lb);
    sn_transport_t transport = sn_transport_loopback_as_transport(lb);
    sn_tx_t received;
    err = send_tx_over_loopback(&transport, &tx, &received);
    sn_test_expect_ok(ctx, "e2e.happy.transport", "transaction delivered", err);

    sn_block_t block;
    err = prepare_block(&block, &ledger, &received, 1, proposer->pubkey,
                        0x01020304u);
    sn_test_expect_ok(ctx, "e2e.happy.block", "block prepared", err);

    uint8_t block_hash[SN_HASH_LEN];
    (void)sn_block_hash(&block, block_hash);
    sn_consensus_t consensus;
    err = init_consensus(&consensus);
    if (err == ESP_OK) {
        err = sn_consensus_start_height(&consensus, 1, block_hash);
    }
    if (err == ESP_OK) {
        err = cast_accept_votes(&consensus, 0, 3);
    }
    sn_test_expect_ok(ctx, "e2e.happy.consensus", "quorum reached", err);
    sn_test_expect_true(ctx, "e2e.happy.finality", "block finalized",
                        sn_consensus_is_finalized(&consensus), "final");

    sn_ledger_report_t report;
    err = sn_ledger_apply_block(&ledger, &block, &report);
    sn_test_expect_ok(ctx, "e2e.happy.apply", "block applied", err);
    sn_test_expect_true(ctx, "e2e.happy.state", "balances and tip updated",
                        ledger.applied_height == 1 &&
                            memcmp(ledger.tip_hash, block_hash, SN_HASH_LEN) ==
                                0 &&
                            sn_ledger_balance(&ledger, bob->pubkey) == 1000 &&
                            sn_ledger_balance(&ledger, alice->pubkey) == 0,
                        "height=1/tip_match/0/1000");

    uint8_t snapshot[SN_STORAGE_MAX_VALUE_LEN];
    size_t snapshot_len = 0;
    err = sn_ledger_snapshot_encode(&ledger, snapshot, sizeof(snapshot),
                                    &snapshot_len);
    sn_test_expect_ok(ctx, "e2e.happy.snapshot", "snapshot encoded", err);

    sn_storage_journal_t *journal = calloc(1, sizeof(*journal));
    if (journal == NULL) {
        sn_test_begin(ctx, "e2e.happy.restore", "no_memory");
        sn_test_fail(ctx, "no_memory");
        return;
    }
    (void)sn_storage_journal_init(journal);
    sn_storage_backend_t backend = sn_storage_journal_as_backend(journal);
    err = sn_storage_put(&backend, "ledger", snapshot, snapshot_len);
    if (err == ESP_OK) {
        err = sn_storage_commit(&backend);
    }
    sn_test_expect_ok(ctx, "e2e.happy.persist", "snapshot committed", err);
    uint8_t restored_buf[SN_STORAGE_MAX_VALUE_LEN];
    size_t restored_len = 0;
    err = sn_storage_get(&backend, "ledger", restored_buf,
                         sizeof(restored_buf), &restored_len);
    sn_ledger_t restored;
    sn_ledger_init(&restored);
    if (err == ESP_OK) {
        err = sn_ledger_snapshot_decode(restored_buf, restored_len, &restored);
    }
    sn_test_expect_ok(ctx, "e2e.happy.restore", "ledger restored", err);
    uint8_t root_a[SN_HASH_LEN];
    uint8_t root_b[SN_HASH_LEN];
    (void)sn_ledger_state_root(&ledger, root_a);
    (void)sn_ledger_state_root(&restored, root_b);
    sn_test_expect_true(ctx, "e2e.happy.restore.match",
                        "restored state matches",
                        memcmp(root_a, root_b, SN_HASH_LEN) == 0 &&
                            restored.applied_height == ledger.applied_height &&
                            memcmp(restored.tip_hash, ledger.tip_hash,
                                   SN_HASH_LEN) == 0,
                        "match");
    sn_storage_journal_deinit(journal);
    free(journal);
}

static void test_double_spend(sn_test_ctx_t *ctx)
{
    const sn_test_key_t *alice = sn_app_test_key(4);
    const sn_test_key_t *bob = sn_app_test_key(5);
    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_genesis_output_t genesis[1];
    genesis[0].value = 1000;
    memcpy(genesis[0].owner_pubkey, alice->pubkey, SN_PUBKEY_LEN);
    uint8_t zero[SN_HASH_LEN];
    memset(zero, 0, sizeof(zero));
    (void)sn_ledger_genesis(&ledger, genesis, 1, 0, zero);

    sn_transport_loopback_t *lb = &s_loopback;
    (void)sn_transport_loopback_init(lb);
    sn_transport_t transport = sn_transport_loopback_as_transport(lb);

    sn_test_input_t in;
    (void)sn_test_genesis_input(&in, 0, 4);
    sn_test_output_t first_outputs[1] = {{1000, 5}};
    sn_tx_t first;
    esp_err_t err = make_tx(&first, &in, 1, first_outputs, 1);
    sn_tx_t received;
    if (err == ESP_OK) {
        err = send_tx_over_loopback(&transport, &first, &received);
    }
    sn_ledger_report_t report;
    if (err == ESP_OK) {
        err = sn_ledger_apply_tx(&ledger, &received, &report);
    }
    sn_test_expect_ok(ctx, "e2e.double_spend.first", "first spend accepted",
                      err);
    sn_ledger_t before = ledger;

    sn_test_output_t second_outputs[2] = {{999, 5}, {1, 4}};
    sn_tx_t second;
    err = make_tx(&second, &in, 1, second_outputs, 2);
    if (err == ESP_OK) {
        err = send_tx_over_loopback(&transport, &second, &received);
    }
    if (err == ESP_OK) {
        err = sn_ledger_apply_tx(&ledger, &received, &report);
    }
    sn_test_expect_err(ctx, "e2e.double_spend", "second spend rejected", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "e2e.double_spend.code", "input already spent",
                        report.code == SN_LEDGER_ERR_INPUT_SPENT,
                        sn_ledger_result_name(report.code));
    sn_test_expect_true(ctx, "e2e.double_spend.state", "state unchanged",
                        sn_ledger_balance(&ledger, bob->pubkey) == 1000 &&
                            ledger.applied_height == before.applied_height,
                        "unchanged");
}

static void test_partition_recovery(sn_test_ctx_t *ctx)
{
    const sn_test_key_t *alice = sn_app_test_key(4);
    const sn_test_key_t *bob = sn_app_test_key(5);
    const sn_test_key_t *proposer = sn_app_test_key(0);
    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_genesis_output_t genesis[1];
    genesis[0].value = 1000;
    memcpy(genesis[0].owner_pubkey, alice->pubkey, SN_PUBKEY_LEN);
    uint8_t zero[SN_HASH_LEN];
    memset(zero, 0, sizeof(zero));
    (void)sn_ledger_genesis(&ledger, genesis, 1, 0, zero);

    sn_test_input_t in;
    (void)sn_test_genesis_input(&in, 0, 4);
    sn_test_output_t outputs[1] = {{1000, 5}};
    sn_tx_t tx;
    esp_err_t err = make_tx(&tx, &in, 1, outputs, 1);
    sn_block_t block;
    if (err == ESP_OK) {
        err = prepare_block(&block, &ledger, &tx, 1, proposer->pubkey,
                            0x0A0B0C0Du);
    }
    sn_test_expect_ok(ctx, "e2e.partition.block", "block prepared", err);

    uint8_t block_hash[SN_HASH_LEN];
    (void)sn_block_hash(&block, block_hash);
    sn_consensus_t consensus;
    err = init_consensus(&consensus);
    if (err == ESP_OK) {
        err = sn_consensus_start_height(&consensus, 1, block_hash);
    }
    if (err == ESP_OK) {
        err = cast_accept_votes(&consensus, 0, 2);
    }
    sn_test_expect_ok(ctx, "e2e.partition.pending", "two votes pending", err);
    sn_test_expect_true(ctx, "e2e.partition.no_finality",
                        "not finalized without quorum",
                        !sn_consensus_is_finalized(&consensus), "pending");

    uint8_t snapshot[2048];
    size_t snapshot_len = 0;
    err = sn_consensus_snapshot_encode(&consensus, snapshot, sizeof(snapshot),
                                       &snapshot_len);
    sn_consensus_t restored;
    memset(&restored, 0, sizeof(restored));
    if (err == ESP_OK) {
        err = sn_consensus_snapshot_decode(snapshot, snapshot_len, &restored);
    }
    sn_test_expect_ok(ctx, "e2e.partition.restore", "votes restored", err);
    err = cast_accept_votes(&restored, 2, 1);
    sn_test_expect_ok(ctx, "e2e.partition.recover", "third vote accepted", err);
    sn_test_expect_true(ctx, "e2e.partition.finality",
                        "restored quorum finalizes",
                        sn_consensus_is_finalized(&restored), "final");

    sn_ledger_report_t report;
    err = sn_ledger_apply_block(&ledger, &block, &report);
    sn_test_expect_ok(ctx, "e2e.partition.apply", "block applied once", err);
    sn_ledger_t after_first = ledger;
    err = sn_ledger_apply_block(&ledger, &block, &report);
    sn_test_expect_err(ctx, "e2e.partition.apply_again",
                       "second application rejected", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(
        ctx, "e2e.partition.apply_once", "applied exactly once",
        ledger.applied_height == 1 &&
            sn_ledger_balance(&ledger, bob->pubkey) == 1000 &&
            memcmp(&ledger, &after_first, sizeof(ledger)) == 0,
        "height=1/bob=1000/unchanged");
}

void sn_app_test_e2e(sn_test_ctx_t *ctx)
{
    test_happy_path(ctx);
    test_double_spend(ctx);
    test_partition_recovery(ctx);
}
