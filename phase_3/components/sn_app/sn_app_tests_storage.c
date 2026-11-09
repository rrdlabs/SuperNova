#include <stdlib.h>
#include <string.h>

#include "sn_app_internal.h"

static bool value_is(const uint8_t *buf, size_t len, const char *expected)
{
    size_t expected_len = strlen(expected);
    return len == expected_len && memcmp(buf, expected, len) == 0;
}

static void test_mem_backend(sn_test_ctx_t *ctx)
{
    sn_storage_mem_t *mem = calloc(1, sizeof(*mem));
    sn_test_begin(ctx, "storage.mem.alloc", "memory backend allocated");
    if (mem == NULL) {
        sn_test_fail(ctx, "no_memory");
        return;
    }
    sn_test_pass(ctx, "allocated");
    esp_err_t err = sn_storage_mem_init(mem);
    sn_test_expect_ok(ctx, "storage.mem.init", "initialized", err);
    sn_storage_backend_t backend = sn_storage_mem_as_backend(mem);

    const uint8_t value1[] = "value-1";
    err = sn_storage_put(&backend, "key", value1, sizeof(value1) - 1u);
    sn_test_expect_ok(ctx, "storage.mem.stage", "write staged", err);
    uint8_t buf[64];
    size_t len = 0;
    err = sn_storage_get(&backend, "key", buf, sizeof(buf), &len);
    sn_test_expect_err(ctx, "storage.mem.atomic.before", "invisible before commit",
                       err, ESP_ERR_NOT_FOUND);
    err = sn_storage_commit(&backend);
    sn_test_expect_ok(ctx, "storage.mem.commit", "commit ok", err);
    err = sn_storage_get(&backend, "key", buf, sizeof(buf), &len);
    sn_test_expect_ok(ctx, "storage.mem.atomic.after", "visible after commit",
                      err);
    sn_test_expect_true(ctx, "storage.mem.atomic.value", "value matches",
                        value_is(buf, len, "value-1"), "value-1");

    const uint8_t value2[] = "value-2";
    err = sn_storage_put(&backend, "key", value2, sizeof(value2) - 1u);
    if (err == ESP_OK) {
        err = sn_storage_rollback(&backend);
    }
    sn_test_expect_ok(ctx, "storage.mem.rollback", "rollback ok", err);
    err = sn_storage_get(&backend, "key", buf, sizeof(buf), &len);
    sn_test_expect_true(ctx, "storage.mem.rollback.value",
                        "old value preserved",
                        err == ESP_OK && value_is(buf, len, "value-1"),
                        "value-1");

    sn_storage_mem_deinit(mem);
    free(mem);
}

static void test_journal_commit(sn_test_ctx_t *ctx)
{
    sn_storage_journal_t *j = calloc(1, sizeof(*j));
    sn_test_begin(ctx, "storage.journal.alloc", "journal backend allocated");
    if (j == NULL) {
        sn_test_fail(ctx, "no_memory");
        return;
    }
    sn_test_pass(ctx, "allocated");
    esp_err_t err = sn_storage_journal_init(j);
    sn_test_expect_ok(ctx, "storage.journal.init", "initialized", err);
    sn_storage_backend_t backend = sn_storage_journal_as_backend(j);

    const uint8_t value[] = "journal-value-1";
    err = sn_storage_put(&backend, "ledger", value, sizeof(value) - 1u);
    if (err == ESP_OK) {
        err = sn_storage_commit(&backend);
    }
    sn_test_expect_ok(ctx, "storage.journal.commit", "commit ok", err);
    uint8_t buf[64];
    size_t len = 0;
    err = sn_storage_get(&backend, "ledger", buf, sizeof(buf), &len);
    sn_test_expect_true(ctx, "storage.journal.read", "new value returned",
                        err == ESP_OK && value_is(buf, len, "journal-value-1"),
                        "journal-value-1");

    sn_storage_journal_deinit(j);
    free(j);
}

static void test_journal_crash_before(sn_test_ctx_t *ctx)
{
    sn_storage_journal_t *j = calloc(1, sizeof(*j));
    if (j == NULL) {
        sn_test_begin(ctx, "storage.journal.crash_before_commit", "no_memory");
        sn_test_fail(ctx, "no_memory");
        return;
    }
    (void)sn_storage_journal_init(j);
    sn_storage_backend_t backend = sn_storage_journal_as_backend(j);
    const uint8_t v1[] = "committed-1";
    const uint8_t v2[] = "uncommitted-2";
    (void)sn_storage_put(&backend, "k", v1, sizeof(v1) - 1u);
    (void)sn_storage_commit(&backend);
    (void)sn_storage_put(&backend, "k", v2, sizeof(v2) - 1u);
    sn_storage_journal_set_crash(j, SN_STORAGE_CRASH_BEFORE_COMMIT);
    esp_err_t err = sn_storage_commit(&backend);
    sn_test_expect_err(ctx, "storage.journal.crash_before_commit",
                       "simulated power loss", err, ESP_ERR_INVALID_STATE);
    uint8_t buf[64];
    size_t len = 0;
    err = sn_storage_get(&backend, "k", buf, sizeof(buf), &len);
    sn_test_expect_true(ctx, "storage.journal.crash_before.value",
                        "previous value preserved",
                        err == ESP_OK && value_is(buf, len, "committed-1"),
                        "committed-1");
    sn_storage_journal_deinit(j);
    free(j);
}

static void test_journal_crash_after(sn_test_ctx_t *ctx)
{
    sn_storage_journal_t *j = calloc(1, sizeof(*j));
    if (j == NULL) {
        sn_test_begin(ctx, "storage.journal.crash_after_commit", "no_memory");
        sn_test_fail(ctx, "no_memory");
        return;
    }
    (void)sn_storage_journal_init(j);
    sn_storage_backend_t backend = sn_storage_journal_as_backend(j);
    const uint8_t v1[] = "committed-1";
    const uint8_t v2[] = "committed-2";
    (void)sn_storage_put(&backend, "k", v1, sizeof(v1) - 1u);
    (void)sn_storage_commit(&backend);
    (void)sn_storage_put(&backend, "k", v2, sizeof(v2) - 1u);
    sn_storage_journal_set_crash(j, SN_STORAGE_CRASH_AFTER_COMMIT);
    esp_err_t err = sn_storage_commit(&backend);
    sn_test_expect_err(ctx, "storage.journal.crash_after_commit",
                       "simulated power loss", err, ESP_ERR_INVALID_STATE);
    uint8_t buf[64];
    size_t len = 0;
    err = sn_storage_get(&backend, "k", buf, sizeof(buf), &len);
    sn_test_expect_true(ctx, "storage.journal.crash_after.value",
                        "new value preserved",
                        err == ESP_OK && value_is(buf, len, "committed-2"),
                        "committed-2");
    sn_storage_journal_deinit(j);
    free(j);
}

static void test_journal_corruption(sn_test_ctx_t *ctx)
{
    sn_storage_journal_t *j = calloc(1, sizeof(*j));
    if (j == NULL) {
        sn_test_begin(ctx, "storage.journal.corrupt_slot", "no_memory");
        sn_test_fail(ctx, "no_memory");
        return;
    }
    (void)sn_storage_journal_init(j);
    sn_storage_backend_t backend = sn_storage_journal_as_backend(j);
    const uint8_t v1[] = "old-value";
    const uint8_t v2[] = "new-value";
    (void)sn_storage_put(&backend, "k", v1, sizeof(v1) - 1u);
    (void)sn_storage_commit(&backend);
    (void)sn_storage_put(&backend, "k", v2, sizeof(v2) - 1u);
    (void)sn_storage_commit(&backend);

    esp_err_t err = sn_storage_journal_corrupt_slot(j, "k", 1);
    sn_test_expect_ok(ctx, "storage.journal.corrupt_slot.inject",
                      "active slot corrupted", err);
    uint8_t buf[64];
    size_t len = 0;
    err = sn_storage_get(&backend, "k", buf, sizeof(buf), &len);
    sn_test_expect_true(ctx, "storage.journal.corrupt_slot",
                        "falls back to valid slot",
                        err == ESP_OK && value_is(buf, len, "old-value"),
                        "old-value");

    err = sn_storage_journal_corrupt_slot(j, "k", 0);
    sn_test_expect_ok(ctx, "storage.journal.both_corrupt.inject",
                      "other slot corrupted", err);
    err = sn_storage_get(&backend, "k", buf, sizeof(buf), &len);
    sn_test_expect_err(ctx, "storage.journal.both_corrupt", "read error", err,
                       ESP_ERR_INVALID_CRC);
    err = sn_storage_get(&backend, "k", buf, sizeof(buf), &len);
    sn_test_expect_err(ctx, "storage.journal.both_corrupt.no_format",
                       "still refused, no auto-format", err,
                       ESP_ERR_INVALID_CRC);
    sn_storage_journal_deinit(j);
    free(j);
}

static void test_ledger_restart(sn_test_ctx_t *ctx)
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
    esp_err_t err = sn_ledger_genesis(&ledger, genesis, 1, 0, zero);
    sn_test_expect_ok(ctx, "storage.ledger.genesis", "genesis ready", err);

    sn_test_input_t in;
    (void)sn_test_genesis_input(&in, 0, 4);
    sn_test_output_t outputs[1] = {{1000, 5}};
    sn_tx_t tx;
    err = sn_test_build_tx(&tx, &in, 1, outputs, 1);
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&tx);
    }
    sn_ledger_report_t report;
    err = sn_ledger_apply_tx(&ledger, &tx, &report);
    sn_test_expect_ok(ctx, "storage.ledger.spend", "spend accepted", err);

    uint8_t snapshot[SN_STORAGE_MAX_VALUE_LEN];
    size_t snapshot_len = 0;
    err = sn_ledger_snapshot_encode(&ledger, snapshot, sizeof(snapshot),
                                    &snapshot_len);
    sn_test_expect_ok(ctx, "storage.ledger.snapshot", "snapshot encoded", err);

    sn_storage_journal_t *j = calloc(1, sizeof(*j));
    if (j == NULL) {
        sn_test_begin(ctx, "storage.ledger.restart", "no_memory");
        sn_test_fail(ctx, "no_memory");
        return;
    }
    (void)sn_storage_journal_init(j);
    sn_storage_backend_t backend = sn_storage_journal_as_backend(j);
    err = sn_storage_put(&backend, "ledger", snapshot, snapshot_len);
    if (err == ESP_OK) {
        err = sn_storage_commit(&backend);
    }
    sn_test_expect_ok(ctx, "storage.ledger.persist", "snapshot committed", err);

    uint8_t restored_buf[SN_STORAGE_MAX_VALUE_LEN];
    size_t restored_len = 0;
    err = sn_storage_get(&backend, "ledger", restored_buf,
                         sizeof(restored_buf), &restored_len);
    sn_test_expect_ok(ctx, "storage.ledger.reload", "snapshot loaded", err);
    sn_ledger_t restored;
    sn_ledger_init(&restored);
    if (err == ESP_OK) {
        err = sn_ledger_snapshot_decode(restored_buf, restored_len, &restored);
    }
    sn_test_expect_ok(ctx, "storage.ledger.restart", "ledger restored", err);
    uint8_t root_a[SN_HASH_LEN];
    uint8_t root_b[SN_HASH_LEN];
    (void)sn_ledger_state_root(&ledger, root_a);
    (void)sn_ledger_state_root(&restored, root_b);
    sn_test_expect_true(
        ctx, "storage.ledger.restart.match", "state root, tip, height match",
        memcmp(root_a, root_b, SN_HASH_LEN) == 0 &&
            restored.applied_height == ledger.applied_height &&
            memcmp(restored.tip_hash, ledger.tip_hash, SN_HASH_LEN) == 0 &&
            sn_ledger_balance(&restored, bob->pubkey) == 1000 &&
            sn_ledger_balance(&restored, alice->pubkey) == 0,
        "match");
    sn_storage_journal_deinit(j);
    free(j);
}

void sn_app_test_storage(sn_test_ctx_t *ctx)
{
    test_mem_backend(ctx);
    test_journal_commit(ctx);
    test_journal_crash_before(ctx);
    test_journal_crash_after(ctx);
    test_journal_corruption(ctx);
    test_ledger_restart(ctx);
}
