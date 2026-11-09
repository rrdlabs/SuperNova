#include "sn_app.h"

#include <stdio.h>
#include <string.h>

#include "sn_app_internal.h"

static void log_test(sn_test_ctx_t *ctx, const char *status, const char *actual)
{
    ctx->sim_ms++;
    printf("SN2|event=TEST|id=%s|status=%s|expected=%s|actual=%s|sim_ms=%u\n",
           ctx->current_id != NULL ? ctx->current_id : "unknown", status,
           ctx->current_expected != NULL ? ctx->current_expected : "unspecified",
           actual != NULL ? actual : "unspecified", (unsigned)ctx->sim_ms);
    fflush(stdout);
}

void sn_test_begin(sn_test_ctx_t *ctx, const char *id, const char *expected)
{
    ctx->current_id = id;
    ctx->current_expected = expected;
}

void sn_test_pass(sn_test_ctx_t *ctx, const char *actual)
{
    ctx->total++;
    ctx->passed++;
    log_test(ctx, "PASS", actual);
}

void sn_test_fail(sn_test_ctx_t *ctx, const char *actual)
{
    ctx->total++;
    ctx->failed++;
    log_test(ctx, "FAIL", actual);
}

void sn_test_expect_ok(sn_test_ctx_t *ctx, const char *id, const char *expected,
                       esp_err_t err)
{
    sn_test_begin(ctx, id, expected);
    if (err == ESP_OK) {
        sn_test_pass(ctx, "ok");
    } else {
        sn_test_fail(ctx, esp_err_to_name(err));
    }
}

void sn_test_expect_err(sn_test_ctx_t *ctx, const char *id,
                        const char *expected, esp_err_t err, esp_err_t want)
{
    sn_test_begin(ctx, id, expected);
    if (err == want) {
        sn_test_pass(ctx, esp_err_to_name(err));
    } else {
        sn_test_fail(ctx, esp_err_to_name(err));
    }
}

void sn_test_expect_true(sn_test_ctx_t *ctx, const char *id,
                         const char *expected, bool condition,
                         const char *actual)
{
    sn_test_begin(ctx, id, expected);
    if (condition) {
        sn_test_pass(ctx, actual);
    } else {
        sn_test_fail(ctx, actual);
    }
}

esp_err_t sn_test_build_tx(sn_tx_t *tx, const sn_test_input_t *inputs,
                           size_t input_count, const sn_test_output_t *outputs,
                           size_t output_count)
{
    if (tx == NULL || inputs == NULL || outputs == NULL || input_count == 0 ||
        input_count > SN_MAX_TX_INPUTS || output_count == 0 ||
        output_count > SN_MAX_TX_OUTPUTS) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(tx, 0, sizeof(*tx));
    tx->version = SN_PROTOCOL_VERSION;
    tx->network_id = SN_NETWORK_ID_TEST;
    tx->input_count = (uint16_t)input_count;
    for (size_t i = 0; i < input_count; i++) {
        const sn_test_key_t *key = sn_app_test_key(inputs[i].key_index);
        if (key == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        sn_tx_input_t *in = &tx->inputs[i];
        memcpy(in->prev_txid, inputs[i].txid, SN_TXID_LEN);
        in->prev_output_index = inputs[i].output_index;
        memcpy(in->pubkey, key->pubkey, SN_PUBKEY_LEN);
        in->sig_alg = SN_SIG_ALG_ECDSA_P256_SHA256;
    }
    tx->output_count = (uint16_t)output_count;
    for (size_t i = 0; i < output_count; i++) {
        const sn_test_key_t *key = sn_app_test_key(outputs[i].key_index);
        if (key == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        tx->outputs[i].value = outputs[i].value;
        memcpy(tx->outputs[i].owner_pubkey, key->pubkey, SN_PUBKEY_LEN);
    }
    return sn_tx_validate(tx);
}

esp_err_t sn_test_sign_tx(sn_tx_t *tx)
{
    if (tx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t digest[SN_HASH_LEN];
    esp_err_t err = sn_tx_signing_digest(tx, digest);
    if (err != ESP_OK) {
        return err;
    }
    for (uint16_t i = 0; i < tx->input_count; i++) {
        int key_index =
            sn_app_test_key_index_for_pubkey(tx->inputs[i].pubkey);
        if (key_index < 0) {
            return ESP_ERR_INVALID_ARG;
        }
        sn_signer_t signer = sn_app_test_signer((size_t)key_index);
        if (signer.sign_digest == NULL) {
            return ESP_ERR_INVALID_STATE;
        }
        err = signer.sign_digest(signer.ctx, digest, tx->inputs[i].signature);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t sn_test_genesis_input(sn_test_input_t *input, uint32_t output_index,
                                size_t key_index)
{
    if (input == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = sn_ledger_genesis_txid(input->txid);
    if (err != ESP_OK) {
        return err;
    }
    input->output_index = output_index;
    input->key_index = key_index;
    return ESP_OK;
}

esp_err_t sn_test_build_block(sn_block_t *block, const sn_ledger_t *ledger,
                              const sn_tx_t *txs, size_t tx_count,
                              uint32_t height, uint64_t timestamp,
                              const uint8_t proposer_pubkey[SN_PUBKEY_LEN],
                              const uint8_t state_root[SN_HASH_LEN])
{
    if (block == NULL || ledger == NULL || proposer_pubkey == NULL ||
        state_root == NULL || tx_count > SN_MAX_BLOCK_TXS ||
        (tx_count > 0 && txs == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(block, 0, sizeof(*block));
    block->version = SN_PROTOCOL_VERSION;
    block->network_id = SN_NETWORK_ID_TEST;
    block->height = height;
    memcpy(block->prev_block_hash, ledger->tip_hash, SN_HASH_LEN);
    block->timestamp = timestamp;
    block->tx_count = (uint16_t)tx_count;
    for (size_t i = 0; i < tx_count; i++) {
        block->txs[i] = txs[i];
    }
    esp_err_t err = sn_tx_root(txs, tx_count, block->tx_root);
    if (err != ESP_OK) {
        return err;
    }
    memcpy(block->state_root, state_root, SN_HASH_LEN);
    memcpy(block->proposer_pubkey, proposer_pubkey, SN_PUBKEY_LEN);
    return sn_block_validate(block);
}

esp_err_t sn_test_state_after(const sn_ledger_t *ledger, const sn_tx_t *txs,
                              size_t tx_count, uint8_t state_root[SN_HASH_LEN])
{
    if (ledger == NULL || state_root == NULL ||
        (tx_count > 0 && txs == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    sn_ledger_t scratch;
    memcpy(&scratch, ledger, sizeof(scratch));
    for (size_t i = 0; i < tx_count; i++) {
        sn_ledger_report_t report;
        esp_err_t err = sn_ledger_apply_tx(&scratch, &txs[i], &report);
        if (err != ESP_OK) {
            return err;
        }
    }
    return sn_ledger_state_root(&scratch, state_root);
}

esp_err_t sn_app_run(void)
{
    sn_test_ctx_t ctx = {0};
    printf("SN2|event=RUN_START|suite=phase3|keys=%u|sim_ms=0\n",
           (unsigned)SN_TEST_KEY_COUNT);
    fflush(stdout);

    esp_err_t err = sn_app_test_keys_init();
    if (err != ESP_OK) {
        printf("SN2|event=RUN_COMPLETE|result=FAIL|reason=key_init|sim_ms=0\n");
        fflush(stdout);
        return err;
    }

    sn_app_test_protocol(&ctx);
    sn_app_test_hardening(&ctx);
    sn_app_test_crypto(&ctx);
    sn_app_test_ledger(&ctx);
    sn_app_test_consensus(&ctx);
    sn_app_test_storage(&ctx);
    sn_app_test_transport(&ctx);
    sn_app_test_e2e(&ctx);

    /* Phase 3: measure the hardened verification/processing path after the
     * deterministic suite has established a known-good fixture. */
    run_empirical_benchmark();

    printf("SN2|event=SUMMARY|total=%d|passed=%d|failed=%d|sim_ms=%u\n",
           ctx.total, ctx.passed, ctx.failed, (unsigned)ctx.sim_ms);
    const char *result = ctx.failed == 0 ? "PASS" : "FAIL";
    printf("SN2|event=RUN_COMPLETE|result=%s|sim_ms=%u\n", result,
           (unsigned)ctx.sim_ms);
    fflush(stdout);

    sn_app_test_keys_deinit();
    return ctx.failed == 0 ? ESP_OK : ESP_FAIL;
}
