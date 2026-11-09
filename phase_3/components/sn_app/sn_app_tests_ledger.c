#include <string.h>

#include "sn_app_internal.h"

static esp_err_t genesis_with(sn_ledger_t *ledger,
                              const sn_test_output_t *outputs, size_t count)
{
    if (ledger == NULL || outputs == NULL || count == 0 ||
        count > SN_MAX_UTXOS) {
        return ESP_ERR_INVALID_ARG;
    }
    sn_genesis_output_t genesis[SN_MAX_UTXOS];
    memset(genesis, 0, sizeof(genesis));
    for (size_t i = 0; i < count; i++) {
        const sn_test_key_t *key = sn_app_test_key(outputs[i].key_index);
        if (key == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        genesis[i].value = outputs[i].value;
        memcpy(genesis[i].owner_pubkey, key->pubkey, SN_PUBKEY_LEN);
    }
    uint8_t zero_hash[SN_HASH_LEN];
    memset(zero_hash, 0, sizeof(zero_hash));
    return sn_ledger_genesis(ledger, genesis, count, 0, zero_hash);
}

static bool root_equal(const sn_ledger_t *a, const sn_ledger_t *b)
{
    uint8_t ra[SN_HASH_LEN];
    uint8_t rb[SN_HASH_LEN];
    if (sn_ledger_state_root(a, ra) != ESP_OK ||
        sn_ledger_state_root(b, rb) != ESP_OK) {
        return false;
    }
    return memcmp(ra, rb, SN_HASH_LEN) == 0;
}

static void test_spend_valid(sn_test_ctx_t *ctx)
{
    const sn_test_key_t *alice = sn_app_test_key(4);
    const sn_test_key_t *bob = sn_app_test_key(5);
    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_test_output_t genesis[2] = {{1000, 4}, {500, 5}};
    esp_err_t err = genesis_with(&ledger, genesis, 2);
    sn_test_expect_ok(ctx, "ledger.genesis", "genesis accepted", err);
    sn_test_expect_true(ctx, "ledger.genesis.balances", "balances match",
                        sn_ledger_balance(&ledger, alice->pubkey) == 1000 &&
                            sn_ledger_balance(&ledger, bob->pubkey) == 500,
                        "1000/500");

    sn_test_input_t in;
    sn_test_genesis_input(&in, 0, 4);
    sn_test_output_t outputs[2] = {{600, 5}, {400, 4}};
    sn_tx_t tx;
    err = sn_test_build_tx(&tx, &in, 1, outputs, 2);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&tx);
    }
    sn_ledger_report_t report;
    err = sn_ledger_apply_tx(&ledger, &tx, &report);
    sn_test_expect_ok(ctx, "ledger.spend.valid", "spend accepted", err);
    sn_test_expect_true(ctx, "ledger.spend.report", "report ok",
                        report.code == SN_LEDGER_OK &&
                            report.input_sum == 1000 &&
                            report.output_sum == 1000,
                        "ok/1000/1000");
    sn_test_expect_true(ctx, "ledger.spend.balances", "balances updated",
                        sn_ledger_balance(&ledger, alice->pubkey) == 400 &&
                            sn_ledger_balance(&ledger, bob->pubkey) == 1100,
                        "400/1100");
    uint8_t txid[SN_TXID_LEN];
    (void)sn_tx_id(&tx, txid);
    sn_test_expect_true(ctx, "ledger.spend.utxos", "utxo set updated",
                        !sn_ledger_has_utxo(&ledger, tx.inputs[0].prev_txid,
                                            0) &&
                            sn_ledger_has_utxo(&ledger, txid, 0) &&
                            sn_ledger_has_utxo(&ledger, txid, 1),
                        "spent/created");
}

static void test_conservation(sn_test_ctx_t *ctx)
{
    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_test_output_t genesis[1] = {{1000, 4}};
    (void)genesis_with(&ledger, genesis, 1);
    sn_ledger_t before = ledger;

    sn_test_input_t in;
    sn_test_genesis_input(&in, 0, 4);
    sn_test_output_t outputs[1] = {{1001, 5}};
    sn_tx_t tx;
    esp_err_t err = sn_test_build_tx(&tx, &in, 1, outputs, 1);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&tx);
    }
    sn_ledger_report_t report;
    err = sn_ledger_apply_tx(&ledger, &tx, &report);
    sn_test_expect_err(ctx, "ledger.spend.conservation", "rejected", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "ledger.spend.conservation.code",
                        "conservation result",
                        report.code == SN_LEDGER_ERR_CONSERVATION,
                        sn_ledger_result_name(report.code));
    sn_test_expect_true(ctx, "ledger.spend.conservation.atomic",
                        "state unchanged", root_equal(&ledger, &before),
                        "unchanged");
}

static void test_zero_and_overflow(sn_test_ctx_t *ctx)
{
    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_test_output_t genesis[1] = {{1000, 4}};
    (void)genesis_with(&ledger, genesis, 1);

    sn_test_input_t in;
    sn_test_genesis_input(&in, 0, 4);
    sn_test_output_t zero_outputs[2] = {{0, 5}, {1000, 4}};
    sn_tx_t tx;
    esp_err_t err = sn_test_build_tx(&tx, &in, 1, zero_outputs, 2);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&tx);
    }
    sn_ledger_report_t report;
    err = sn_ledger_apply_tx(&ledger, &tx, &report);
    sn_test_expect_err(ctx, "ledger.spend.zero_output", "rejected", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "ledger.spend.zero_output.code",
                        "zero output result",
                        report.code == SN_LEDGER_ERR_ZERO_OUTPUT,
                        sn_ledger_result_name(report.code));

    sn_ledger_t overflow_ledger;
    sn_ledger_init(&overflow_ledger);
    sn_test_output_t overflow_genesis[1] = {{SN_MAX_AMOUNT, 4}};
    (void)genesis_with(&overflow_ledger, overflow_genesis, 1);
    sn_test_input_t overflow_in;
    sn_test_genesis_input(&overflow_in, 0, 4);
    sn_test_output_t overflow_outputs[2] = {
        {0x8000000000000000ull, 5},
        {0x8000000000000000ull, 4},
    };
    sn_tx_t overflow_tx;
    err = sn_test_build_tx(&overflow_tx, &overflow_in, 1, overflow_outputs, 2);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&overflow_tx);
    }
    err = sn_ledger_apply_tx(&overflow_ledger, &overflow_tx, &report);
    sn_test_expect_err(ctx, "ledger.spend.overflow", "rejected", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "ledger.spend.overflow.code", "overflow result",
                        report.code == SN_LEDGER_ERR_OVERFLOW,
                        sn_ledger_result_name(report.code));
}

static void test_bad_inputs(sn_test_ctx_t *ctx)
{
    const sn_test_key_t *alice = sn_app_test_key(4);
    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_test_output_t genesis[1] = {{1000, 4}};
    (void)genesis_with(&ledger, genesis, 1);
    sn_ledger_t before = ledger;
    sn_ledger_report_t report;
    esp_err_t err;

    sn_test_input_t duplicate[2];
    (void)sn_test_genesis_input(&duplicate[0], 0, 4);
    duplicate[1] = duplicate[0];
    sn_test_output_t spend_all[1] = {{1000, 5}};
    sn_tx_t tx;
    err = sn_test_build_tx(&tx, duplicate, 2, spend_all, 1);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&tx);
    }
    err = sn_ledger_apply_tx(&ledger, &tx, &report);
    sn_test_expect_err(ctx, "ledger.spend.duplicate_input", "rejected", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "ledger.spend.duplicate_input.code",
                        "duplicate result",
                        report.code == SN_LEDGER_ERR_DUPLICATE_INPUT,
                        sn_ledger_result_name(report.code));

    sn_test_input_t unknown;
    memset(&unknown, 0, sizeof(unknown));
    memset(unknown.txid, 0xAA, SN_TXID_LEN);
    unknown.output_index = 0;
    unknown.key_index = 4;
    err = sn_test_build_tx(&tx, &unknown, 1, spend_all, 1);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&tx);
    }
    err = sn_ledger_apply_tx(&ledger, &tx, &report);
    sn_test_expect_err(ctx, "ledger.spend.unknown_input", "rejected", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "ledger.spend.unknown_input.code",
                        "unknown result",
                        report.code == SN_LEDGER_ERR_UNKNOWN_INPUT,
                        sn_ledger_result_name(report.code));

    sn_test_input_t wrong_owner;
    (void)sn_test_genesis_input(&wrong_owner, 0, 5);
    err = sn_test_build_tx(&tx, &wrong_owner, 1, spend_all, 1);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&tx);
    }
    err = sn_ledger_apply_tx(&ledger, &tx, &report);
    sn_test_expect_err(ctx, "ledger.spend.owner_mismatch", "rejected", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "ledger.spend.owner_mismatch.code",
                        "owner result",
                        report.code == SN_LEDGER_ERR_OWNER_MISMATCH,
                        sn_ledger_result_name(report.code));
    sn_test_expect_true(ctx, "ledger.spend.bad_inputs.atomic",
                        "state unchanged", root_equal(&ledger, &before),
                        "unchanged");
    (void)alice;
}

static void test_double_spend_and_replay(sn_test_ctx_t *ctx)
{
    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_test_output_t genesis[1] = {{1000, 4}};
    (void)genesis_with(&ledger, genesis, 1);

    sn_test_input_t in;
    sn_test_genesis_input(&in, 0, 4);
    sn_test_output_t first_outputs[1] = {{1000, 5}};
    sn_tx_t first;
    esp_err_t err = sn_test_build_tx(&first, &in, 1, first_outputs, 1);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&first);
    }
    sn_ledger_report_t report;
    err = sn_ledger_apply_tx(&ledger, &first, &report);
    sn_test_expect_ok(ctx, "ledger.spend.first", "first spend accepted", err);

    sn_test_output_t second_outputs[2] = {{999, 5}, {1, 4}};
    sn_tx_t second;
    err = sn_test_build_tx(&second, &in, 1, second_outputs, 2);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&second);
    }
    err = sn_ledger_apply_tx(&ledger, &second, &report);
    sn_test_expect_err(ctx, "ledger.spend.double_spend", "rejected", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "ledger.spend.double_spend.code",
                        "input spent result",
                        report.code == SN_LEDGER_ERR_INPUT_SPENT,
                        sn_ledger_result_name(report.code));

    err = sn_ledger_apply_tx(&ledger, &first, &report);
    sn_test_expect_err(ctx, "ledger.spend.replay", "rejected", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "ledger.spend.replay.code", "replay result",
                        report.code == SN_LEDGER_ERR_REPLAY,
                        sn_ledger_result_name(report.code));
}

static void test_capacity(sn_test_ctx_t *ctx)
{
    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_test_output_t genesis[SN_MAX_UTXOS];
    for (size_t i = 0; i < SN_MAX_UTXOS; i++) {
        genesis[i].value = i == 0 ? 2 : 1;
        genesis[i].key_index = 4;
    }
    esp_err_t err = genesis_with(&ledger, genesis, SN_MAX_UTXOS);
    sn_test_expect_ok(ctx, "ledger.capacity.genesis", "full utxo set", err);

    sn_test_input_t in;
    sn_test_genesis_input(&in, 0, 4);
    sn_test_output_t outputs[2] = {{1, 5}, {1, 4}};
    sn_tx_t tx;
    err = sn_test_build_tx(&tx, &in, 1, outputs, 2);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&tx);
    }
    sn_ledger_report_t report;
    err = sn_ledger_apply_tx(&ledger, &tx, &report);
    sn_test_expect_err(ctx, "ledger.capacity", "rejected atomically", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "ledger.capacity.code", "capacity result",
                        report.code == SN_LEDGER_ERR_CAPACITY,
                        sn_ledger_result_name(report.code));
}

static void test_atomicity(sn_test_ctx_t *ctx)
{
    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_test_output_t genesis[2] = {{1000, 4}, {500, 4}};
    (void)genesis_with(&ledger, genesis, 2);
    sn_ledger_t before = ledger;

    sn_test_input_t inputs[2];
    (void)sn_test_genesis_input(&inputs[0], 0, 4);
    (void)sn_test_genesis_input(&inputs[1], 1, 4);
    sn_test_output_t outputs[1] = {{1500, 5}};
    sn_tx_t tx;
    esp_err_t err = sn_test_build_tx(&tx, inputs, 2, outputs, 1);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&tx);
    }
    tx.inputs[1].signature[0] ^= 0xFFu;
    sn_ledger_report_t report;
    err = sn_ledger_apply_tx(&ledger, &tx, &report);
    sn_test_expect_err(ctx, "ledger.atomicity", "rejected", err,
                       ESP_ERR_INVALID_CRC);
    sn_test_expect_true(ctx, "ledger.atomicity.code", "bad signature result",
                        report.code == SN_LEDGER_ERR_BAD_SIGNATURE,
                        sn_ledger_result_name(report.code));
    sn_test_expect_true(ctx, "ledger.atomicity.state", "state unchanged",
                        root_equal(&ledger, &before), "unchanged");
}

static void test_block_apply(sn_test_ctx_t *ctx)
{
    const sn_test_key_t *alice = sn_app_test_key(4);
    const sn_test_key_t *bob = sn_app_test_key(5);
    const sn_test_key_t *validator = sn_app_test_key(0);
    sn_ledger_t ledger;
    sn_ledger_init(&ledger);
    sn_test_output_t genesis[1] = {{1000, 4}};
    (void)genesis_with(&ledger, genesis, 1);

    sn_test_input_t in;
    sn_test_genesis_input(&in, 0, 4);
    sn_test_output_t outputs[1] = {{1000, 5}};
    sn_tx_t tx;
    esp_err_t err = sn_test_build_tx(&tx, &in, 1, outputs, 1);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&tx);
    }
    uint8_t state_root[SN_HASH_LEN];
    err = sn_test_state_after(&ledger, &tx, 1, state_root);
    sn_test_expect_ok(ctx, "ledger.block.state_prepare", "state root ready",
                      err);

    sn_block_t block;
    err = sn_test_build_block(&block, &ledger, &tx, 1, 1, 0x11223344u,
                              validator->pubkey, state_root);
    sn_test_expect_ok(ctx, "ledger.block.build", "block built", err);
    sn_ledger_report_t report;
    err = sn_ledger_apply_block(&ledger, &block, &report);
    sn_test_expect_ok(ctx, "ledger.block.apply", "block accepted", err);
    uint8_t block_hash[SN_HASH_LEN];
    (void)sn_block_hash(&block, block_hash);
    sn_test_expect_true(ctx, "ledger.block.tip", "tip and height updated",
                        ledger.applied_height == 1 &&
                            memcmp(ledger.tip_hash, block_hash, SN_HASH_LEN) ==
                                0,
                        "height=1/tip_match");
    sn_test_expect_true(ctx, "ledger.block.balances", "balances updated",
                        sn_ledger_balance(&ledger, alice->pubkey) == 0 &&
                            sn_ledger_balance(&ledger, bob->pubkey) == 1000,
                        "0/1000");

    sn_ledger_t bad_root_ledger;
    sn_ledger_init(&bad_root_ledger);
    (void)genesis_with(&bad_root_ledger, genesis, 1);
    uint8_t bad_root[SN_HASH_LEN];
    (void)sn_test_state_after(&bad_root_ledger, &tx, 1, bad_root);
    bad_root[0] ^= 0xFFu;
    sn_block_t bad_root_block;
    (void)sn_test_build_block(&bad_root_block, &bad_root_ledger, &tx, 1, 1,
                              0x11223344u, validator->pubkey, bad_root);
    sn_ledger_t bad_root_before = bad_root_ledger;
    err = sn_ledger_apply_block(&bad_root_ledger, &bad_root_block, &report);
    sn_test_expect_err(ctx, "ledger.block.bad_state_root", "rejected", err,
                       ESP_ERR_INVALID_CRC);
    sn_test_expect_true(ctx, "ledger.block.bad_state_root.code",
                        "bad state root result",
                        report.code == SN_LEDGER_ERR_BAD_STATE_ROOT,
                        sn_ledger_result_name(report.code));
    sn_test_expect_true(ctx, "ledger.block.bad_state_root.atomic",
                        "state unchanged",
                        root_equal(&bad_root_ledger, &bad_root_before),
                        "unchanged");

    sn_ledger_t bad_prev_ledger;
    sn_ledger_init(&bad_prev_ledger);
    (void)genesis_with(&bad_prev_ledger, genesis, 1);
    uint8_t good_root[SN_HASH_LEN];
    (void)sn_test_state_after(&bad_prev_ledger, &tx, 1, good_root);
    sn_block_t bad_prev_block;
    (void)sn_test_build_block(&bad_prev_block, &bad_prev_ledger, &tx, 1, 1,
                              0x11223344u, validator->pubkey, good_root);
    bad_prev_block.prev_block_hash[0] ^= 0xFFu;
    sn_ledger_t bad_prev_before = bad_prev_ledger;
    err = sn_ledger_apply_block(&bad_prev_ledger, &bad_prev_block, &report);
    sn_test_expect_err(ctx, "ledger.block.bad_prev", "rejected", err,
                       ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "ledger.block.bad_prev.code",
                        "bad previous hash result",
                        report.code == SN_LEDGER_ERR_BAD_PREV,
                        sn_ledger_result_name(report.code));
    sn_test_expect_true(ctx, "ledger.block.bad_prev.atomic",
                        "state unchanged",
                        root_equal(&bad_prev_ledger, &bad_prev_before),
                        "unchanged");
}

void sn_app_test_ledger(sn_test_ctx_t *ctx)
{
    test_spend_valid(ctx);
    test_conservation(ctx);
    test_zero_and_overflow(ctx);
    test_bad_inputs(ctx);
    test_double_spend_and_replay(ctx);
    test_capacity(ctx);
    test_atomicity(ctx);
    test_block_apply(ctx);
}
