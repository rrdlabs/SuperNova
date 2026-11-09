/*
 * Phase 3 hardening regressions.
 *
 * The original security flaw was treating a packed C struct as the wire
 * format. That couples the protocol to host endianness, compiler padding, and
 * struct layout. Phase 3 exposes and tests an explicit canonical big-endian
 * block-header marshaller: the wire size is a byte count (178), never
 * sizeof(sn_block_t), and every field is written in a fixed order.
 */
#include <string.h>

#include "sn_app_internal.h"
#include "sn_app_test_vectors.h"

static void make_vector_tx(sn_tx_t *tx)
{
    memset(tx, 0, sizeof(*tx));
    tx->version = SN_PROTOCOL_VERSION;
    tx->network_id = SN_NETWORK_ID_TEST;
    tx->input_count = 1;
    for (size_t i = 0; i < SN_TXID_LEN; i++) {
        tx->inputs[0].prev_txid[i] = (uint8_t)i;
    }
    tx->inputs[0].prev_output_index = 7;
    memcpy(tx->inputs[0].pubkey, sn_vector_pubkey, SN_PUBKEY_LEN);
    tx->inputs[0].sig_alg = SN_SIG_ALG_ECDSA_P256_SHA256;
    memcpy(tx->inputs[0].signature, sn_vector_tx_signature, SN_SIG_LEN);
    tx->output_count = 2;
    tx->outputs[0].value = 1000;
    memcpy(tx->outputs[0].owner_pubkey, sn_vector_pubkey, SN_PUBKEY_LEN);
    tx->outputs[1].value = 500;
    memcpy(tx->outputs[1].owner_pubkey, sn_vector_pubkey, SN_PUBKEY_LEN);
}

static void make_vector_block(sn_block_t *block, const sn_tx_t *tx)
{
    memset(block, 0, sizeof(*block));
    block->version = SN_PROTOCOL_VERSION;
    block->network_id = SN_NETWORK_ID_TEST;
    block->height = 1;
    for (size_t i = 0; i < SN_HASH_LEN; i++) {
        block->prev_block_hash[i] = (uint8_t)(0x20u + i);
        block->state_root[i] = (uint8_t)(0x40u + i);
    }
    block->timestamp = 0x0102030405060708ull;
    block->tx_count = 1;
    block->txs[0] = *tx;
    (void)sn_tx_root(block->txs, 1, block->tx_root);
    memcpy(block->proposer_pubkey, sn_vector_pubkey, SN_PUBKEY_LEN);
}

void sn_app_test_hardening(sn_test_ctx_t *ctx)
{
    sn_tx_t tx;
    make_vector_tx(&tx);
    sn_block_t block;
    make_vector_block(&block, &tx);

    sn_test_expect_true(
        ctx, "hardening.header.byte_length",
        "wire length is 178 bytes, not sizeof(struct)",
        SN_BLOCK_HEADER_LEN == 178u && sizeof(sn_block_t) > SN_BLOCK_HEADER_LEN,
        "178_bytes");

    uint8_t header[SN_BLOCK_HEADER_LEN];
    size_t header_len = 0;
    esp_err_t err =
        sn_block_header_encode(&block, header, sizeof(header), &header_len);
    sn_test_begin(ctx, "hardening.header.canonical",
                  "exact big-endian header vector");
    if (err == ESP_OK && header_len == SN_BLOCK_HEADER_LEN &&
        memcmp(header, sn_vector_block_header, SN_BLOCK_HEADER_LEN) == 0) {
        sn_test_pass(ctx, "vector_match");
    } else {
        sn_test_fail(ctx, "vector_mismatch");
    }

    sn_block_t parsed;
    err = sn_block_header_decode(header, header_len, &parsed);
    sn_test_begin(ctx, "hardening.header.roundtrip",
                  "header fields survive canonical roundtrip");
    if (err == ESP_OK && parsed.version == block.version &&
        parsed.network_id == block.network_id &&
        parsed.height == block.height &&
        parsed.timestamp == block.timestamp &&
        parsed.tx_count == block.tx_count &&
        memcmp(parsed.prev_block_hash, block.prev_block_hash,
               SN_HASH_LEN) == 0 &&
        memcmp(parsed.tx_root, block.tx_root, SN_HASH_LEN) == 0 &&
        memcmp(parsed.state_root, block.state_root, SN_HASH_LEN) == 0 &&
        memcmp(parsed.proposer_pubkey, block.proposer_pubkey,
               SN_PUBKEY_LEN) == 0) {
        sn_test_pass(ctx, "roundtrip_ok");
    } else {
        sn_test_fail(ctx, "roundtrip_mismatch");
    }

    err = sn_block_header_decode(header, header_len - 1, &parsed);
    sn_test_expect_err(ctx, "hardening.header.truncated",
                       "truncated header rejected", err, ESP_ERR_INVALID_ARG);

    err = sn_block_header_decode(header, header_len + 1, &parsed);
    sn_test_expect_err(ctx, "hardening.header.trailing",
                       "trailing byte rejected", err, ESP_ERR_INVALID_ARG);

    /* Explicit byte-level endianness checks (offsets are part of the format). */
    sn_block_t endian = block;
    endian.height = 0x01020304u;
    endian.timestamp = 0x0102030405060708ull;
    err = sn_block_header_encode(&endian, header, sizeof(header), &header_len);
    sn_test_begin(ctx, "hardening.header.big_endian",
                  "u32/u64 fields are network byte order");
    if (err == ESP_OK && header[3] == 0x01 && header[4] == 0x02 &&
        header[5] == 0x03 && header[6] == 0x04 && header[39] == 0x01 &&
        header[40] == 0x02 && header[41] == 0x03 && header[42] == 0x04 &&
        header[43] == 0x05 && header[44] == 0x06 && header[45] == 0x07 &&
        header[46] == 0x08) {
        sn_test_pass(ctx, "big_endian_ok");
    } else {
        sn_test_fail(ctx, "endianness_mismatch");
    }

    /* The block hash must commit to exactly the canonical header bytes. */
    err = sn_block_header_encode(&block, header, sizeof(header), &header_len);
    uint8_t expected_hash[SN_HASH_LEN];
    sn_iovec_t hash_parts[2] = {
        {.data = (const uint8_t *)SN_DOMAIN_BLOCKID,
         .len = strlen(SN_DOMAIN_BLOCKID)},
        {.data = header, .len = header_len},
    };
    esp_err_t hash_err = sn_sha256_many(hash_parts, 2, expected_hash);
    uint8_t block_hash[SN_HASH_LEN];
    esp_err_t block_hash_err = sn_block_hash(&block, block_hash);
    sn_test_begin(ctx, "hardening.header.hash_binding",
                  "block hash covers canonical bytes");
    if (err == ESP_OK && hash_err == ESP_OK && block_hash_err == ESP_OK &&
        memcmp(expected_hash, block_hash, SN_HASH_LEN) == 0) {
        sn_test_pass(ctx, "hash_match");
    } else {
        sn_test_fail(ctx, "hash_mismatch");
    }

    header[39] ^= 0x01u;
    uint8_t mutated_hash[SN_HASH_LEN];
    hash_parts[1].data = header;
    hash_err = sn_sha256_many(hash_parts, 2, mutated_hash);
    sn_test_expect_true(
        ctx, "hardening.header.hash_sensitivity",
        "one changed header byte changes the hash",
        hash_err == ESP_OK &&
            memcmp(mutated_hash, block_hash, SN_HASH_LEN) != 0,
        "hash_changed");

    /* Full-block decode must still reject a header whose count exceeds bounds. */
    uint8_t block_buf[SN_MAX_BLOCK_BYTES];
    size_t block_len = 0;
    err = sn_block_encode(&block, block_buf, sizeof(block_buf), &block_len);
    if (err == ESP_OK) {
        block_buf[47] = 0;
        block_buf[48] = (uint8_t)(SN_MAX_BLOCK_TXS + 1u);
        sn_block_t scratch;
        err = sn_block_decode(block_buf, block_len, &scratch);
    }
    sn_test_expect_err(ctx, "hardening.header.count_bound",
                       "oversized tx_count rejected", err,
                       ESP_ERR_INVALID_ARG);

    err = sn_block_encode(&block, block_buf, sizeof(block_buf), &block_len);
    if (err == ESP_OK) {
        block_buf[0] = 0;
        block_buf[1] = 2;
        sn_block_t scratch;
        err = sn_block_decode(block_buf, block_len, &scratch);
    }
    sn_test_expect_err(ctx, "hardening.header.version_bound",
                       "unsupported version rejected", err,
                       ESP_ERR_INVALID_ARG);
}
