#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SN_PROTOCOL_VERSION 1u
#define SN_NETWORK_ID_TEST 0x02u

#ifndef SN_HASH_LEN
#define SN_HASH_LEN 32u
#endif
#ifndef SN_TXID_LEN
#define SN_TXID_LEN 32u
#endif
#ifndef SN_PUBKEY_LEN
#define SN_PUBKEY_LEN 65u
#endif
#ifndef SN_SIG_LEN
#define SN_SIG_LEN 64u
#endif
#define SN_SIG_ALG_ECDSA_P256_SHA256 1u

#define SN_MAX_TX_INPUTS 4u
#define SN_MAX_TX_OUTPUTS 4u
#define SN_MAX_TX_BYTES 2048u
#define SN_MAX_BLOCK_TXS 8u
#define SN_MAX_BLOCK_BYTES 16384u

/*
 * Canonical block-header wire size:
 *   2 version + 1 network + 4 height + 32 prev_hash + 8 timestamp
 *   + 2 tx_count + 32 tx_root + 32 state_root + 65 proposer_pubkey = 178.
 * This is a byte count, never sizeof(sn_block_t).
 */
#define SN_BLOCK_HEADER_LEN 178u
#define SN_MAX_UTXOS 32u
#define SN_MAX_RECENT_TXIDS 32u
#define SN_MAX_AMOUNT (UINT64_MAX - 1u)

#define SN_FRAME_MAGIC 0x534E3246u /* "SN2F" */
#define SN_FRAME_VERSION 1u
#define SN_FRAME_HEADER_LEN 10u
#define SN_MAX_FRAME_PAYLOAD 4096u

/* Domain-separation strings. Hashes are SHA256(domain || data). */
#define SN_DOMAIN_TX "SN2/TX/v1"
#define SN_DOMAIN_TXID "SN2/TXID/v1"
#define SN_DOMAIN_TX_SIG "SN2/TXSIG/v1"
#define SN_DOMAIN_BLOCK "SN2/BLOCK/v1"
#define SN_DOMAIN_BLOCKID "SN2/BLOCKID/v1"
#define SN_DOMAIN_BLOCK_SIG "SN2/BLOCKSIG/v1"
#define SN_DOMAIN_TXROOT "SN2/TXROOT/v1"
#define SN_DOMAIN_STATE "SN2/STATE/v1"
#define SN_DOMAIN_GENESIS "SN2/GENESIS/v1"
#define SN_DOMAIN_VOTE "SN2/VOTE/v1"

typedef struct {
    uint8_t prev_txid[SN_TXID_LEN];
    uint32_t prev_output_index;
    uint8_t pubkey[SN_PUBKEY_LEN];
    uint8_t sig_alg;
    uint8_t signature[SN_SIG_LEN];
} sn_tx_input_t;

typedef struct {
    uint64_t value;
    uint8_t owner_pubkey[SN_PUBKEY_LEN];
} sn_tx_output_t;

typedef struct {
    uint16_t version;
    uint8_t network_id;
    uint16_t input_count;
    sn_tx_input_t inputs[SN_MAX_TX_INPUTS];
    uint16_t output_count;
    sn_tx_output_t outputs[SN_MAX_TX_OUTPUTS];
} sn_tx_t;

typedef struct {
    uint16_t version;
    uint8_t network_id;
    uint32_t height;
    uint8_t prev_block_hash[SN_HASH_LEN];
    uint64_t timestamp;
    uint16_t tx_count;
    uint8_t tx_root[SN_HASH_LEN];
    uint8_t state_root[SN_HASH_LEN];
    uint8_t proposer_pubkey[SN_PUBKEY_LEN];
    sn_tx_t txs[SN_MAX_BLOCK_TXS];
    uint8_t proposer_signature[SN_SIG_LEN];
} sn_block_t;

typedef enum {
    SN_FRAME_TYPE_TX = 1,
    SN_FRAME_TYPE_BLOCK = 2,
    SN_FRAME_TYPE_VOTE = 3,
} sn_frame_type_t;

/* Shape validation: version, network, counts, algorithm ids, pubkey prefix. */
esp_err_t sn_tx_validate(const sn_tx_t *tx);
esp_err_t sn_block_validate(const sn_block_t *block);

/* Canonical encoding. No packed C struct is written directly. */
esp_err_t sn_tx_encode_unsigned(const sn_tx_t *tx, uint8_t *buf, size_t cap,
                                size_t *out_len);
esp_err_t sn_tx_encode(const sn_tx_t *tx, uint8_t *buf, size_t cap,
                       size_t *out_len);
esp_err_t sn_tx_decode(const uint8_t *buf, size_t len, sn_tx_t *out);

esp_err_t sn_tx_id(const sn_tx_t *tx, uint8_t out[SN_TXID_LEN]);
esp_err_t sn_tx_signing_digest(const sn_tx_t *tx, uint8_t out[SN_HASH_LEN]);
esp_err_t sn_tx_root(const sn_tx_t *txs, size_t count, uint8_t out[SN_HASH_LEN]);

/*
 * Canonical block-header marshalling. The encoder writes each field in
 * big-endian order and never casts a C struct onto the wire. The decoder
 * accepts exactly SN_BLOCK_HEADER_LEN bytes and rejects truncation/trailing
 * bytes. These are the hardened primitives used by block encode/hash/decode.
 */
esp_err_t sn_block_header_encode(const sn_block_t *block, uint8_t *buf,
                                 size_t cap, size_t *out_len);
esp_err_t sn_block_header_decode(const uint8_t *buf, size_t len,
                                 sn_block_t *out);

esp_err_t sn_block_encode(const sn_block_t *block, uint8_t *buf, size_t cap,
                          size_t *out_len);
esp_err_t sn_block_decode(const uint8_t *buf, size_t len, sn_block_t *out);
esp_err_t sn_block_hash(const sn_block_t *block, uint8_t out[SN_HASH_LEN]);
esp_err_t sn_block_signing_digest(const sn_block_t *block,
                                  uint8_t out[SN_HASH_LEN]);

esp_err_t sn_frame_encode(sn_frame_type_t type, const uint8_t *payload,
                          size_t payload_len, uint8_t *buf, size_t cap,
                          size_t *out_len);
/* On success, payload points into buf and remains valid while buf is alive. */
esp_err_t sn_frame_decode(const uint8_t *buf, size_t len, sn_frame_type_t *type,
                          const uint8_t **payload, size_t *payload_len);

#ifdef __cplusplus
}
#endif
