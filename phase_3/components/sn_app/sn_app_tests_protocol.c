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

void sn_app_test_protocol(sn_test_ctx_t *ctx)
{
    sn_tx_t tx;
    make_vector_tx(&tx);

    uint8_t buf[SN_MAX_TX_BYTES];
    size_t len = 0;
    esp_err_t err = sn_tx_encode_unsigned(&tx, buf, sizeof(buf), &len);
    sn_test_begin(ctx, "protocol.tx.vector", "exact unsigned vector");
    if (err == ESP_OK && len == SN_VECTOR_TX_UNSIGNED_LEN &&
        memcmp(buf, sn_vector_tx_unsigned, len) == 0) {
        sn_test_pass(ctx, "vector_match");
    } else {
        sn_test_fail(ctx, "vector_mismatch");
    }

    err = sn_tx_encode(&tx, buf, sizeof(buf), &len);
    sn_test_begin(ctx, "protocol.tx.full_vector", "exact signed vector");
    if (err == ESP_OK && len == SN_VECTOR_TX_FULL_LEN &&
        memcmp(buf, sn_vector_tx_full, len) == 0) {
        sn_test_pass(ctx, "vector_match");
    } else {
        sn_test_fail(ctx, "vector_mismatch");
    }

    uint8_t txid[SN_TXID_LEN];
    err = sn_tx_id(&tx, txid);
    sn_test_begin(ctx, "protocol.tx.txid", "reference txid");
    if (err == ESP_OK && memcmp(txid, sn_vector_txid, SN_TXID_LEN) == 0) {
        sn_test_pass(ctx, "txid_match");
    } else {
        sn_test_fail(ctx, "txid_mismatch");
    }

    uint8_t digest[SN_HASH_LEN];
    err = sn_tx_signing_digest(&tx, digest);
    sn_test_begin(ctx, "protocol.tx.signing_digest", "reference digest");
    if (err == ESP_OK &&
        memcmp(digest, sn_vector_tx_signing_digest, SN_HASH_LEN) == 0) {
        sn_test_pass(ctx, "digest_match");
    } else {
        sn_test_fail(ctx, "digest_mismatch");
    }

    sn_tx_t decoded;
    err = sn_tx_decode(sn_vector_tx_full, SN_VECTOR_TX_FULL_LEN, &decoded);
    sn_test_begin(ctx, "protocol.tx.roundtrip", "decode equals source");
    if (err == ESP_OK && decoded.input_count == tx.input_count &&
        decoded.output_count == tx.output_count &&
        memcmp(decoded.inputs[0].signature, tx.inputs[0].signature,
               SN_SIG_LEN) == 0) {
        sn_test_pass(ctx, "roundtrip_ok");
    } else {
        sn_test_fail(ctx, "roundtrip_mismatch");
    }

    sn_block_t block;
    make_vector_block(&block, &tx);
    uint8_t block_buf[SN_MAX_BLOCK_BYTES];
    size_t block_len = 0;
    err = sn_block_encode(&block, block_buf, sizeof(block_buf), &block_len);
    sn_test_begin(ctx, "protocol.block.vector", "exact block vector");
    if (err == ESP_OK && block_len == SN_VECTOR_BLOCK_FULL_LEN &&
        memcmp(block_buf, sn_vector_block_full, block_len) == 0 &&
        memcmp(block_buf, sn_vector_block_header,
               SN_VECTOR_BLOCK_HEADER_LEN) == 0) {
        sn_test_pass(ctx, "vector_match");
    } else {
        sn_test_fail(ctx, "vector_mismatch");
    }

    uint8_t root[SN_HASH_LEN];
    err = sn_tx_root(block.txs, block.tx_count, root);
    sn_test_begin(ctx, "protocol.block.tx_root", "reference tx root");
    if (err == ESP_OK && memcmp(root, sn_vector_tx_root, SN_HASH_LEN) == 0) {
        sn_test_pass(ctx, "tx_root_match");
    } else {
        sn_test_fail(ctx, "tx_root_mismatch");
    }

    uint8_t block_hash[SN_HASH_LEN];
    err = sn_block_hash(&block, block_hash);
    sn_test_begin(ctx, "protocol.block.hash", "reference block hash");
    if (err == ESP_OK &&
        memcmp(block_hash, sn_vector_block_hash, SN_HASH_LEN) == 0) {
        sn_test_pass(ctx, "block_hash_match");
    } else {
        sn_test_fail(ctx, "block_hash_mismatch");
    }

    err = sn_block_signing_digest(&block, digest);
    sn_test_begin(ctx, "protocol.block.signing_digest",
                  "reference block digest");
    if (err == ESP_OK &&
        memcmp(digest, sn_vector_block_signing_digest, SN_HASH_LEN) == 0) {
        sn_test_pass(ctx, "digest_match");
    } else {
        sn_test_fail(ctx, "digest_mismatch");
    }

    uint8_t vote_digest[SN_HASH_LEN];
    uint8_t vote_hash[SN_HASH_LEN];
    for (size_t i = 0; i < SN_HASH_LEN; i++) {
        vote_hash[i] = (uint8_t)(0x60u + i);
    }
    err = sn_vote_signing_digest(1, 0, vote_hash, 2, SN_VOTE_ACCEPT,
                                 vote_digest);
    sn_test_begin(ctx, "protocol.vote.digest", "reference vote digest");
    if (err == ESP_OK &&
        memcmp(vote_digest, sn_vector_vote_digest, SN_HASH_LEN) == 0) {
        sn_test_pass(ctx, "digest_match");
    } else {
        sn_test_fail(ctx, "digest_mismatch");
    }

    /* Malformed transaction inputs. */
    uint8_t malformed[SN_MAX_TX_BYTES + 4];
    memcpy(malformed, sn_vector_tx_full, SN_VECTOR_TX_FULL_LEN);
    sn_tx_t scratch;
    err = sn_tx_decode(malformed, SN_VECTOR_TX_FULL_LEN - 1, &scratch);
    sn_test_expect_err(ctx, "protocol.malformed.truncated",
                       "length error", err, ESP_ERR_INVALID_SIZE);

    memcpy(malformed, sn_vector_tx_full, SN_VECTOR_TX_FULL_LEN);
    malformed[0] = 0;
    malformed[1] = 2;
    err = sn_tx_decode(malformed, SN_VECTOR_TX_FULL_LEN, &scratch);
    sn_test_expect_err(ctx, "protocol.malformed.version", "rejected", err,
                       ESP_ERR_INVALID_ARG);

    memcpy(malformed, sn_vector_tx_full, SN_VECTOR_TX_FULL_LEN);
    malformed[3] = 0;
    malformed[4] = 0;
    err = sn_tx_decode(malformed, SN_VECTOR_TX_FULL_LEN, &scratch);
    sn_test_expect_err(ctx, "protocol.malformed.input_count", "rejected", err,
                       ESP_ERR_INVALID_ARG);

    memcpy(malformed, sn_vector_tx_full, SN_VECTOR_TX_FULL_LEN);
    malformed[171] = 0;
    malformed[172] = 5;
    err = sn_tx_decode(malformed, SN_VECTOR_TX_FULL_LEN, &scratch);
    sn_test_expect_err(ctx, "protocol.malformed.output_count", "rejected", err,
                       ESP_ERR_INVALID_ARG);

    memcpy(malformed, sn_vector_tx_full, SN_VECTOR_TX_FULL_LEN);
    malformed[SN_VECTOR_TX_FULL_LEN] = 0;
    err = sn_tx_decode(malformed, SN_VECTOR_TX_FULL_LEN + 1, &scratch);
    sn_test_expect_err(ctx, "protocol.malformed.trailing", "rejected", err,
                       ESP_ERR_INVALID_SIZE);

    /* Malformed frame envelopes. */
    uint8_t frame[SN_FRAME_HEADER_LEN + SN_MAX_FRAME_PAYLOAD];
    size_t frame_len = 0;
    err = sn_frame_encode(SN_FRAME_TYPE_TX, sn_vector_tx_full,
                          SN_VECTOR_TX_FULL_LEN, frame, sizeof(frame),
                          &frame_len);
    sn_test_expect_ok(ctx, "protocol.frame.encode", "frame encoded", err);

    sn_frame_type_t type;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    frame[0] ^= 0xFFu;
    err = sn_frame_decode(frame, frame_len, &type, &payload, &payload_len);
    sn_test_expect_err(ctx, "protocol.malformed.frame_magic", "rejected", err,
                       ESP_ERR_INVALID_ARG);

    err = sn_frame_encode(SN_FRAME_TYPE_TX, sn_vector_tx_full,
                          SN_VECTOR_TX_FULL_LEN, frame, sizeof(frame),
                          &frame_len);
    err = sn_frame_decode(frame, frame_len - 1, &type, &payload, &payload_len);
    sn_test_expect_err(ctx, "protocol.malformed.frame_length", "rejected", err,
                       ESP_ERR_INVALID_SIZE);

    err = sn_frame_encode(SN_FRAME_TYPE_TX, sn_vector_tx_full,
                          SN_VECTOR_TX_FULL_LEN, frame, sizeof(frame),
                          &frame_len);
    frame[8] = 0xFF;
    frame[9] = 0xFF;
    err = sn_frame_decode(frame, frame_len, &type, &payload, &payload_len);
    sn_test_expect_err(ctx, "protocol.malformed.frame_payload_len",
                       "rejected", err, ESP_ERR_INVALID_ARG);

    err = sn_frame_encode(SN_FRAME_TYPE_TX, sn_vector_tx_full,
                          SN_VECTOR_TX_FULL_LEN, frame, sizeof(frame),
                          &frame_len);
    frame[6] = 0;
    frame[7] = 1;
    err = sn_frame_decode(frame, frame_len, &type, &payload, &payload_len);
    sn_test_expect_err(ctx, "protocol.malformed.frame_reserved", "rejected",
                       err, ESP_ERR_INVALID_ARG);
}
