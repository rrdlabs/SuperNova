#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sn_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SN_LEDGER_SNAPSHOT_MAGIC 0x534E324Cu /* "SN2L" */
#define SN_LEDGER_SNAPSHOT_VERSION 1u

typedef struct {
    uint8_t txid[SN_TXID_LEN];
    uint32_t output_index;
    uint8_t owner_pubkey[SN_PUBKEY_LEN];
    uint64_t value;
    bool spent;
} sn_utxo_t;

typedef struct {
    sn_utxo_t utxos[SN_MAX_UTXOS];
    size_t count;
    uint32_t applied_height;
    uint8_t tip_hash[SN_HASH_LEN];
    uint8_t state_root[SN_HASH_LEN];
    bool state_root_valid;
    uint8_t recent_txids[SN_MAX_RECENT_TXIDS][SN_TXID_LEN];
    size_t recent_count;
    size_t recent_next;
} sn_ledger_t;

typedef struct {
    uint8_t owner_pubkey[SN_PUBKEY_LEN];
    uint64_t value;
} sn_genesis_output_t;

typedef enum {
    SN_LEDGER_OK = 0,
    SN_LEDGER_ERR_ARG,
    SN_LEDGER_ERR_SHAPE,
    SN_LEDGER_ERR_DUPLICATE_INPUT,
    SN_LEDGER_ERR_REPLAY,
    SN_LEDGER_ERR_UNKNOWN_INPUT,
    SN_LEDGER_ERR_INPUT_SPENT,
    SN_LEDGER_ERR_OWNER_MISMATCH,
    SN_LEDGER_ERR_BAD_SIGNATURE,
    SN_LEDGER_ERR_ZERO_OUTPUT,
    SN_LEDGER_ERR_CONSERVATION,
    SN_LEDGER_ERR_OVERFLOW,
    SN_LEDGER_ERR_CAPACITY,
    SN_LEDGER_ERR_BAD_PREV,
    SN_LEDGER_ERR_BAD_HEIGHT,
    SN_LEDGER_ERR_BAD_TX_ROOT,
    SN_LEDGER_ERR_BAD_STATE_ROOT,
    SN_LEDGER_ERR_NO_MEM,
    SN_LEDGER_ERR_SNAPSHOT,
} sn_ledger_result_t;

typedef struct {
    sn_ledger_result_t code;
    uint64_t input_sum;
    uint64_t output_sum;
    size_t new_utxo_count;
} sn_ledger_report_t;

const char *sn_ledger_result_name(sn_ledger_result_t code);

void sn_ledger_init(sn_ledger_t *ledger);

esp_err_t sn_ledger_genesis_txid(uint8_t out[SN_TXID_LEN]);
esp_err_t sn_ledger_genesis(sn_ledger_t *ledger,
                            const sn_genesis_output_t *outputs, size_t count,
                            uint32_t height, const uint8_t tip_hash[SN_HASH_LEN]);
esp_err_t sn_ledger_set_tip(sn_ledger_t *ledger,
                            const uint8_t tip_hash[SN_HASH_LEN]);

esp_err_t sn_ledger_apply_tx(sn_ledger_t *ledger, const sn_tx_t *tx,
                             sn_ledger_report_t *report);
esp_err_t sn_ledger_apply_block(sn_ledger_t *ledger, const sn_block_t *block,
                                sn_ledger_report_t *report);

uint64_t sn_ledger_balance(const sn_ledger_t *ledger,
                           const uint8_t owner_pubkey[SN_PUBKEY_LEN]);
esp_err_t sn_ledger_state_root(const sn_ledger_t *ledger,
                               uint8_t out[SN_HASH_LEN]);
bool sn_ledger_has_utxo(const sn_ledger_t *ledger,
                        const uint8_t txid[SN_TXID_LEN],
                        uint32_t output_index);

esp_err_t sn_ledger_snapshot_encode(const sn_ledger_t *ledger, uint8_t *buf,
                                    size_t cap, size_t *out_len);
esp_err_t sn_ledger_snapshot_decode(const uint8_t *buf, size_t len,
                                    sn_ledger_t *ledger);

#ifdef __cplusplus
}
#endif
