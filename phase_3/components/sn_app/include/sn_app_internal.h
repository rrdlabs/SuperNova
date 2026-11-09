#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sn_consensus.h"
#include "sn_crypto.h"
#include "sn_ledger.h"
#include "sn_protocol.h"
#include "sn_storage.h"
#include "sn_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SN_TEST_KEY_COUNT 8u
#define SN_TEST_VALIDATOR_COUNT 4u

typedef struct {
    sn_soft_signer_t signer;
    uint8_t pubkey[SN_PUBKEY_LEN];
    bool ready;
} sn_test_key_t;

typedef struct {
    int total;
    int passed;
    int failed;
    uint32_t sim_ms;
    const char *current_id;
    const char *current_expected;
} sn_test_ctx_t;

typedef struct {
    uint8_t txid[SN_TXID_LEN];
    uint32_t output_index;
    size_t key_index;
} sn_test_input_t;

typedef struct {
    uint64_t value;
    size_t key_index;
} sn_test_output_t;

esp_err_t sn_app_test_keys_init(void);
void sn_app_test_keys_deinit(void);
const sn_test_key_t *sn_app_test_key(size_t index);
sn_signer_t sn_app_test_signer(size_t index);
int sn_app_test_key_index_for_pubkey(const uint8_t pubkey[SN_PUBKEY_LEN]);

void sn_test_begin(sn_test_ctx_t *ctx, const char *id, const char *expected);
void sn_test_pass(sn_test_ctx_t *ctx, const char *actual);
void sn_test_fail(sn_test_ctx_t *ctx, const char *actual);
void sn_test_expect_ok(sn_test_ctx_t *ctx, const char *id, const char *expected,
                       esp_err_t err);
void sn_test_expect_err(sn_test_ctx_t *ctx, const char *id,
                        const char *expected, esp_err_t err,
                        esp_err_t want);
void sn_test_expect_true(sn_test_ctx_t *ctx, const char *id,
                         const char *expected, bool condition,
                         const char *actual);

esp_err_t sn_test_build_tx(sn_tx_t *tx, const sn_test_input_t *inputs,
                           size_t input_count, const sn_test_output_t *outputs,
                           size_t output_count);
esp_err_t sn_test_sign_tx(sn_tx_t *tx);
esp_err_t sn_test_genesis_input(sn_test_input_t *input, uint32_t output_index,
                                size_t key_index);
esp_err_t sn_test_build_block(sn_block_t *block, const sn_ledger_t *ledger,
                              const sn_tx_t *txs, size_t tx_count,
                              uint32_t height, uint64_t timestamp,
                              const uint8_t proposer_pubkey[SN_PUBKEY_LEN],
                              const uint8_t state_root[SN_HASH_LEN]);
esp_err_t sn_test_state_after(const sn_ledger_t *ledger, const sn_tx_t *txs,
                              size_t tx_count, uint8_t state_root[SN_HASH_LEN]);

void sn_app_test_protocol(sn_test_ctx_t *ctx);
void sn_app_test_hardening(sn_test_ctx_t *ctx);
void sn_app_test_crypto(sn_test_ctx_t *ctx);
void sn_app_test_ledger(sn_test_ctx_t *ctx);
void sn_app_test_consensus(sn_test_ctx_t *ctx);
void sn_app_test_storage(sn_test_ctx_t *ctx);
void sn_app_test_transport(sn_test_ctx_t *ctx);
void sn_app_test_e2e(sn_test_ctx_t *ctx);

/* Phase 3 empirical benchmark; emits ---RAW_SAMPLE::...--- UART records. */
void run_empirical_benchmark(void);

#ifdef __cplusplus
}
#endif
