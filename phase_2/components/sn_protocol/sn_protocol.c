#include "sn_protocol.h"

#include <string.h>

#include "sn_crypto.h"

#define SN_BLOCK_HEADER_LEN 178u

typedef struct {
    const uint8_t *data;
    size_t len;
} sn_part_t;

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t off;
    bool ok;
} writer_t;

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t off;
    bool ok;
} reader_t;

static void wr_u8(writer_t *w, uint8_t v)
{
    if (!w->ok || w->off + 1u > w->cap) {
        w->ok = false;
        return;
    }
    w->buf[w->off++] = v;
}

static void wr_u16(writer_t *w, uint16_t v)
{
    wr_u8(w, (uint8_t)(v >> 8));
    wr_u8(w, (uint8_t)v);
}

static void wr_u32(writer_t *w, uint32_t v)
{
    wr_u8(w, (uint8_t)(v >> 24));
    wr_u8(w, (uint8_t)(v >> 16));
    wr_u8(w, (uint8_t)(v >> 8));
    wr_u8(w, (uint8_t)v);
}

static void wr_u64(writer_t *w, uint64_t v)
{
    wr_u8(w, (uint8_t)(v >> 56));
    wr_u8(w, (uint8_t)(v >> 48));
    wr_u8(w, (uint8_t)(v >> 40));
    wr_u8(w, (uint8_t)(v >> 32));
    wr_u8(w, (uint8_t)(v >> 24));
    wr_u8(w, (uint8_t)(v >> 16));
    wr_u8(w, (uint8_t)(v >> 8));
    wr_u8(w, (uint8_t)v);
}

static void wr_bytes(writer_t *w, const uint8_t *p, size_t n)
{
    if (!w->ok || w->off + n > w->cap) {
        w->ok = false;
        return;
    }
    memcpy(w->buf + w->off, p, n);
    w->off += n;
}

static bool rd_u8(reader_t *r, uint8_t *v)
{
    if (!r->ok || r->off + 1u > r->len) {
        r->ok = false;
        return false;
    }
    *v = r->buf[r->off++];
    return true;
}

static bool rd_u16(reader_t *r, uint16_t *v)
{
    uint8_t a, b;
    if (!rd_u8(r, &a) || !rd_u8(r, &b)) {
        return false;
    }
    *v = (uint16_t)(((uint16_t)a << 8) | b);
    return true;
}

static bool rd_u32(reader_t *r, uint32_t *v)
{
    uint8_t b[4];
    for (size_t i = 0; i < sizeof(b); i++) {
        if (!rd_u8(r, &b[i])) {
            return false;
        }
    }
    *v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8) | (uint32_t)b[3];
    return true;
}

static bool rd_u64(reader_t *r, uint64_t *v)
{
    uint8_t b[8];
    for (size_t i = 0; i < sizeof(b); i++) {
        if (!rd_u8(r, &b[i])) {
            return false;
        }
    }
    *v = ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
         ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
         ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
         ((uint64_t)b[6] << 8) | (uint64_t)b[7];
    return true;
}

static bool rd_bytes(reader_t *r, uint8_t *p, size_t n)
{
    if (!r->ok || r->off + n > r->len) {
        r->ok = false;
        return false;
    }
    memcpy(p, r->buf + r->off, n);
    r->off += n;
    return true;
}

static esp_err_t hash_domain_parts(const char *domain, const sn_part_t *parts,
                                   size_t count, uint8_t out[SN_HASH_LEN])
{
    sn_iovec_t vec[1 + SN_MAX_BLOCK_TXS + 1];
    if (count + 1u > sizeof(vec) / sizeof(vec[0])) {
        return ESP_ERR_INVALID_ARG;
    }
    vec[0].data = (const uint8_t *)domain;
    vec[0].len = strlen(domain);
    for (size_t i = 0; i < count; i++) {
        vec[i + 1u].data = parts[i].data;
        vec[i + 1u].len = parts[i].len;
    }
    return sn_sha256_many(vec, count + 1u, out);
}

static esp_err_t hash_domain_data(const char *domain, const uint8_t *data,
                                  size_t len, uint8_t out[SN_HASH_LEN])
{
    sn_part_t part = {.data = data, .len = len};
    return hash_domain_parts(domain, &part, 1, out);
}

static bool pubkey_shape_ok(const uint8_t pubkey[SN_PUBKEY_LEN])
{
    return pubkey[0] == 0x04u;
}

esp_err_t sn_tx_validate(const sn_tx_t *tx)
{
    if (tx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tx->version != SN_PROTOCOL_VERSION ||
        tx->network_id != SN_NETWORK_ID_TEST) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tx->input_count == 0 || tx->input_count > SN_MAX_TX_INPUTS ||
        tx->output_count == 0 || tx->output_count > SN_MAX_TX_OUTPUTS) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint16_t i = 0; i < tx->input_count; i++) {
        if (tx->inputs[i].sig_alg != SN_SIG_ALG_ECDSA_P256_SHA256 ||
            !pubkey_shape_ok(tx->inputs[i].pubkey)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (uint16_t i = 0; i < tx->output_count; i++) {
        if (!pubkey_shape_ok(tx->outputs[i].owner_pubkey)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}

static esp_err_t encode_tx_inner(const sn_tx_t *tx, bool with_signatures,
                                 uint8_t *buf, size_t cap, size_t *out_len)
{
    if (buf == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = sn_tx_validate(tx);
    if (err != ESP_OK) {
        return err;
    }
    writer_t w = {.buf = buf, .cap = cap, .off = 0, .ok = true};
    wr_u16(&w, tx->version);
    wr_u8(&w, tx->network_id);
    wr_u16(&w, tx->input_count);
    for (uint16_t i = 0; i < tx->input_count; i++) {
        const sn_tx_input_t *in = &tx->inputs[i];
        wr_bytes(&w, in->prev_txid, sizeof(in->prev_txid));
        wr_u32(&w, in->prev_output_index);
        wr_bytes(&w, in->pubkey, sizeof(in->pubkey));
        wr_u8(&w, in->sig_alg);
        if (with_signatures) {
            wr_bytes(&w, in->signature, sizeof(in->signature));
        }
    }
    wr_u16(&w, tx->output_count);
    for (uint16_t i = 0; i < tx->output_count; i++) {
        wr_u64(&w, tx->outputs[i].value);
        wr_bytes(&w, tx->outputs[i].owner_pubkey,
                 sizeof(tx->outputs[i].owner_pubkey));
    }
    if (!w.ok) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_len = w.off;
    return ESP_OK;
}

esp_err_t sn_tx_encode_unsigned(const sn_tx_t *tx, uint8_t *buf, size_t cap,
                                size_t *out_len)
{
    return encode_tx_inner(tx, false, buf, cap, out_len);
}

esp_err_t sn_tx_encode(const sn_tx_t *tx, uint8_t *buf, size_t cap,
                       size_t *out_len)
{
    return encode_tx_inner(tx, true, buf, cap, out_len);
}

esp_err_t sn_tx_decode(const uint8_t *buf, size_t len, sn_tx_t *out)
{
    if (buf == NULL || out == NULL || len == 0 || len > SN_MAX_TX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    reader_t r = {.buf = buf, .len = len, .off = 0, .ok = true};
    if (!rd_u16(&r, &out->version) || !rd_u8(&r, &out->network_id) ||
        !rd_u16(&r, &out->input_count)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (out->input_count == 0 || out->input_count > SN_MAX_TX_INPUTS) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint16_t i = 0; i < out->input_count; i++) {
        sn_tx_input_t *in = &out->inputs[i];
        if (!rd_bytes(&r, in->prev_txid, sizeof(in->prev_txid)) ||
            !rd_u32(&r, &in->prev_output_index) ||
            !rd_bytes(&r, in->pubkey, sizeof(in->pubkey)) ||
            !rd_u8(&r, &in->sig_alg) ||
            !rd_bytes(&r, in->signature, sizeof(in->signature))) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    if (!rd_u16(&r, &out->output_count)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (out->output_count == 0 || out->output_count > SN_MAX_TX_OUTPUTS) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint16_t i = 0; i < out->output_count; i++) {
        if (!rd_u64(&r, &out->outputs[i].value) ||
            !rd_bytes(&r, out->outputs[i].owner_pubkey,
                      sizeof(out->outputs[i].owner_pubkey))) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    if (!r.ok || r.off != r.len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return sn_tx_validate(out);
}

esp_err_t sn_tx_id(const sn_tx_t *tx, uint8_t out[SN_TXID_LEN])
{
    uint8_t buf[SN_MAX_TX_BYTES];
    size_t len = 0;
    esp_err_t err = sn_tx_encode(tx, buf, sizeof(buf), &len);
    if (err != ESP_OK) {
        return err;
    }
    return hash_domain_data(SN_DOMAIN_TXID, buf, len, out);
}

esp_err_t sn_tx_signing_digest(const sn_tx_t *tx, uint8_t out[SN_HASH_LEN])
{
    uint8_t buf[SN_MAX_TX_BYTES];
    size_t len = 0;
    esp_err_t err = sn_tx_encode_unsigned(tx, buf, sizeof(buf), &len);
    if (err != ESP_OK) {
        return err;
    }
    return hash_domain_data(SN_DOMAIN_TX_SIG, buf, len, out);
}

esp_err_t sn_tx_root(const sn_tx_t *txs, size_t count, uint8_t out[SN_HASH_LEN])
{
    if (count > SN_MAX_BLOCK_TXS || (count > 0 && txs == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    sn_part_t parts[SN_MAX_BLOCK_TXS];
    uint8_t ids[SN_MAX_BLOCK_TXS][SN_TXID_LEN];
    for (size_t i = 0; i < count; i++) {
        esp_err_t err = sn_tx_id(&txs[i], ids[i]);
        if (err != ESP_OK) {
            return err;
        }
        parts[i].data = ids[i];
        parts[i].len = sizeof(ids[i]);
    }
    return hash_domain_parts(SN_DOMAIN_TXROOT, parts, count, out);
}

static esp_err_t encode_block_header(const sn_block_t *block, uint8_t *buf,
                                     size_t cap, size_t *out_len)
{
    if (block == NULL || buf == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    writer_t w = {.buf = buf, .cap = cap, .off = 0, .ok = true};
    wr_u16(&w, block->version);
    wr_u8(&w, block->network_id);
    wr_u32(&w, block->height);
    wr_bytes(&w, block->prev_block_hash, sizeof(block->prev_block_hash));
    wr_u64(&w, block->timestamp);
    wr_u16(&w, block->tx_count);
    wr_bytes(&w, block->tx_root, sizeof(block->tx_root));
    wr_bytes(&w, block->state_root, sizeof(block->state_root));
    wr_bytes(&w, block->proposer_pubkey, sizeof(block->proposer_pubkey));
    if (!w.ok || w.off != SN_BLOCK_HEADER_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_len = w.off;
    return ESP_OK;
}

esp_err_t sn_block_validate(const sn_block_t *block)
{
    if (block == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (block->version != SN_PROTOCOL_VERSION ||
        block->network_id != SN_NETWORK_ID_TEST) {
        return ESP_ERR_INVALID_ARG;
    }
    if (block->tx_count > SN_MAX_BLOCK_TXS ||
        !pubkey_shape_ok(block->proposer_pubkey)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint16_t i = 0; i < block->tx_count; i++) {
        esp_err_t err = sn_tx_validate(&block->txs[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    uint8_t root[SN_HASH_LEN];
    esp_err_t err = sn_tx_root(block->txs, block->tx_count, root);
    if (err != ESP_OK) {
        return err;
    }
    if (memcmp(root, block->tx_root, sizeof(root)) != 0) {
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

esp_err_t sn_block_encode(const sn_block_t *block, uint8_t *buf, size_t cap,
                          size_t *out_len)
{
    if (buf == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = sn_block_validate(block);
    if (err != ESP_OK) {
        return err;
    }
    writer_t w = {.buf = buf, .cap = cap, .off = 0, .ok = true};
    size_t header_len = 0;
    uint8_t header[SN_BLOCK_HEADER_LEN];
    err = encode_block_header(block, header, sizeof(header), &header_len);
    if (err != ESP_OK) {
        return err;
    }
    wr_bytes(&w, header, header_len);
    for (uint16_t i = 0; i < block->tx_count; i++) {
        uint8_t txbuf[SN_MAX_TX_BYTES];
        size_t txlen = 0;
        err = sn_tx_encode(&block->txs[i], txbuf, sizeof(txbuf), &txlen);
        if (err != ESP_OK) {
            return err;
        }
        if (txlen > UINT16_MAX) {
            return ESP_ERR_INVALID_SIZE;
        }
        wr_u16(&w, (uint16_t)txlen);
        wr_bytes(&w, txbuf, txlen);
    }
    if (!w.ok) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_len = w.off;
    return ESP_OK;
}

esp_err_t sn_block_decode(const uint8_t *buf, size_t len, sn_block_t *out)
{
    if (buf == NULL || out == NULL || len < SN_BLOCK_HEADER_LEN ||
        len > SN_MAX_BLOCK_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    reader_t r = {.buf = buf, .len = len, .off = 0, .ok = true};
    if (!rd_u16(&r, &out->version) || !rd_u8(&r, &out->network_id) ||
        !rd_u32(&r, &out->height) ||
        !rd_bytes(&r, out->prev_block_hash, sizeof(out->prev_block_hash)) ||
        !rd_u64(&r, &out->timestamp) || !rd_u16(&r, &out->tx_count) ||
        !rd_bytes(&r, out->tx_root, sizeof(out->tx_root)) ||
        !rd_bytes(&r, out->state_root, sizeof(out->state_root)) ||
        !rd_bytes(&r, out->proposer_pubkey, sizeof(out->proposer_pubkey))) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (out->tx_count > SN_MAX_BLOCK_TXS) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint16_t i = 0; i < out->tx_count; i++) {
        uint16_t txlen = 0;
        if (!rd_u16(&r, &txlen) || txlen == 0 || txlen > SN_MAX_TX_BYTES ||
            r.off + txlen > r.len) {
            return ESP_ERR_INVALID_SIZE;
        }
        esp_err_t err = sn_tx_decode(r.buf + r.off, txlen, &out->txs[i]);
        if (err != ESP_OK) {
            return err;
        }
        r.off += txlen;
    }
    if (!r.ok || r.off != r.len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return sn_block_validate(out);
}

esp_err_t sn_block_hash(const sn_block_t *block, uint8_t out[SN_HASH_LEN])
{
    uint8_t header[SN_BLOCK_HEADER_LEN];
    size_t len = 0;
    esp_err_t err = encode_block_header(block, header, sizeof(header), &len);
    if (err != ESP_OK) {
        return err;
    }
    return hash_domain_data(SN_DOMAIN_BLOCKID, header, len, out);
}

esp_err_t sn_block_signing_digest(const sn_block_t *block,
                                  uint8_t out[SN_HASH_LEN])
{
    uint8_t block_hash[SN_HASH_LEN];
    esp_err_t err = sn_block_hash(block, block_hash);
    if (err != ESP_OK) {
        return err;
    }
    return hash_domain_data(SN_DOMAIN_BLOCK_SIG, block_hash, sizeof(block_hash),
                            out);
}

esp_err_t sn_frame_encode(sn_frame_type_t type, const uint8_t *payload,
                          size_t payload_len, uint8_t *buf, size_t cap,
                          size_t *out_len)
{
    if (buf == NULL || out_len == NULL ||
        (payload_len > 0 && payload == NULL) ||
        payload_len > SN_MAX_FRAME_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }
    if (type != SN_FRAME_TYPE_TX && type != SN_FRAME_TYPE_BLOCK &&
        type != SN_FRAME_TYPE_VOTE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cap < SN_FRAME_HEADER_LEN + payload_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    writer_t w = {.buf = buf, .cap = cap, .off = 0, .ok = true};
    wr_u32(&w, SN_FRAME_MAGIC);
    wr_u8(&w, SN_FRAME_VERSION);
    wr_u8(&w, (uint8_t)type);
    wr_u16(&w, 0);
    wr_u16(&w, (uint16_t)payload_len);
    wr_bytes(&w, payload, payload_len);
    if (!w.ok) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_len = w.off;
    return ESP_OK;
}

esp_err_t sn_frame_decode(const uint8_t *buf, size_t len, sn_frame_type_t *type,
                          const uint8_t **payload, size_t *payload_len)
{
    if (buf == NULL || type == NULL || payload == NULL || payload_len == NULL ||
        len < SN_FRAME_HEADER_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    reader_t r = {.buf = buf, .len = len, .off = 0, .ok = true};
    uint32_t magic = 0;
    uint8_t version = 0, type_raw = 0;
    uint16_t reserved = 0, plen = 0;
    if (!rd_u32(&r, &magic) || !rd_u8(&r, &version) || !rd_u8(&r, &type_raw) ||
        !rd_u16(&r, &reserved) || !rd_u16(&r, &plen)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (magic != SN_FRAME_MAGIC || version != SN_FRAME_VERSION ||
        reserved != 0 || plen > SN_MAX_FRAME_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }
    if (plen != len - SN_FRAME_HEADER_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (type_raw != SN_FRAME_TYPE_TX && type_raw != SN_FRAME_TYPE_BLOCK &&
        type_raw != SN_FRAME_TYPE_VOTE) {
        return ESP_ERR_INVALID_ARG;
    }
    *type = (sn_frame_type_t)type_raw;
    *payload = buf + SN_FRAME_HEADER_LEN;
    *payload_len = plen;
    return ESP_OK;
}
