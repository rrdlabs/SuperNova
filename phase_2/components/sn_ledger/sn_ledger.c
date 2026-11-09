#include "sn_ledger.h"

#include <stdlib.h>
#include <string.h>

#include "sn_crypto.h"

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t off;
    bool ok;
} lw_t;

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t off;
    bool ok;
} lr_t;

static void lw_u8(lw_t *w, uint8_t v)
{
    if (!w->ok || w->off + 1u > w->cap) {
        w->ok = false;
        return;
    }
    w->buf[w->off++] = v;
}

static void lw_u16(lw_t *w, uint16_t v)
{
    lw_u8(w, (uint8_t)(v >> 8));
    lw_u8(w, (uint8_t)v);
}

static void lw_u32(lw_t *w, uint32_t v)
{
    lw_u8(w, (uint8_t)(v >> 24));
    lw_u8(w, (uint8_t)(v >> 16));
    lw_u8(w, (uint8_t)(v >> 8));
    lw_u8(w, (uint8_t)v);
}

static void lw_u64(lw_t *w, uint64_t v)
{
    lw_u8(w, (uint8_t)(v >> 56));
    lw_u8(w, (uint8_t)(v >> 48));
    lw_u8(w, (uint8_t)(v >> 40));
    lw_u8(w, (uint8_t)(v >> 32));
    lw_u8(w, (uint8_t)(v >> 24));
    lw_u8(w, (uint8_t)(v >> 16));
    lw_u8(w, (uint8_t)(v >> 8));
    lw_u8(w, (uint8_t)v);
}

static void lw_bytes(lw_t *w, const uint8_t *p, size_t n)
{
    if (!w->ok || w->off + n > w->cap) {
        w->ok = false;
        return;
    }
    memcpy(w->buf + w->off, p, n);
    w->off += n;
}

static bool lr_u8(lr_t *r, uint8_t *v)
{
    if (!r->ok || r->off + 1u > r->len) {
        r->ok = false;
        return false;
    }
    *v = r->buf[r->off++];
    return true;
}

static bool lr_u16(lr_t *r, uint16_t *v)
{
    uint8_t a, b;
    if (!lr_u8(r, &a) || !lr_u8(r, &b)) {
        return false;
    }
    *v = (uint16_t)(((uint16_t)a << 8) | b);
    return true;
}

static bool lr_u32(lr_t *r, uint32_t *v)
{
    uint8_t b[4];
    for (size_t i = 0; i < sizeof(b); i++) {
        if (!lr_u8(r, &b[i])) {
            return false;
        }
    }
    *v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8) | (uint32_t)b[3];
    return true;
}

static bool lr_u64(lr_t *r, uint64_t *v)
{
    uint8_t b[8];
    for (size_t i = 0; i < sizeof(b); i++) {
        if (!lr_u8(r, &b[i])) {
            return false;
        }
    }
    *v = ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
         ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
         ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
         ((uint64_t)b[6] << 8) | (uint64_t)b[7];
    return true;
}

static bool lr_bytes(lr_t *r, uint8_t *p, size_t n)
{
    if (!r->ok || r->off + n > r->len) {
        r->ok = false;
        return false;
    }
    memcpy(p, r->buf + r->off, n);
    r->off += n;
    return true;
}

static bool add_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a > UINT64_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}

const char *sn_ledger_result_name(sn_ledger_result_t code)
{
    switch (code) {
    case SN_LEDGER_OK: return "ok";
    case SN_LEDGER_ERR_ARG: return "invalid_argument";
    case SN_LEDGER_ERR_SHAPE: return "invalid_shape";
    case SN_LEDGER_ERR_DUPLICATE_INPUT: return "duplicate_input";
    case SN_LEDGER_ERR_REPLAY: return "replay";
    case SN_LEDGER_ERR_UNKNOWN_INPUT: return "unknown_input";
    case SN_LEDGER_ERR_INPUT_SPENT: return "input_spent";
    case SN_LEDGER_ERR_OWNER_MISMATCH: return "owner_mismatch";
    case SN_LEDGER_ERR_BAD_SIGNATURE: return "bad_signature";
    case SN_LEDGER_ERR_ZERO_OUTPUT: return "zero_output";
    case SN_LEDGER_ERR_CONSERVATION: return "conservation";
    case SN_LEDGER_ERR_OVERFLOW: return "overflow";
    case SN_LEDGER_ERR_CAPACITY: return "capacity";
    case SN_LEDGER_ERR_BAD_PREV: return "bad_previous_hash";
    case SN_LEDGER_ERR_BAD_HEIGHT: return "bad_height";
    case SN_LEDGER_ERR_BAD_TX_ROOT: return "bad_tx_root";
    case SN_LEDGER_ERR_BAD_STATE_ROOT: return "bad_state_root";
    case SN_LEDGER_ERR_NO_MEM: return "no_memory";
    case SN_LEDGER_ERR_SNAPSHOT: return "bad_snapshot";
    default: return "unknown";
    }
}

void sn_ledger_init(sn_ledger_t *ledger)
{
    if (ledger != NULL) {
        memset(ledger, 0, sizeof(*ledger));
    }
}

static void recent_add(sn_ledger_t *ledger, const uint8_t txid[SN_TXID_LEN])
{
    if (ledger->recent_count < SN_MAX_RECENT_TXIDS) {
        memcpy(ledger->recent_txids[ledger->recent_count], txid, SN_TXID_LEN);
        ledger->recent_count++;
    } else {
        memcpy(ledger->recent_txids[ledger->recent_next], txid, SN_TXID_LEN);
        ledger->recent_next =
            (ledger->recent_next + 1u) % SN_MAX_RECENT_TXIDS;
    }
}

static bool recent_contains(const sn_ledger_t *ledger,
                            const uint8_t txid[SN_TXID_LEN])
{
    for (size_t i = 0; i < ledger->recent_count; i++) {
        if (memcmp(ledger->recent_txids[i], txid, SN_TXID_LEN) == 0) {
            return true;
        }
    }
    return false;
}

static int find_utxo(const sn_ledger_t *ledger, const uint8_t txid[SN_TXID_LEN],
                     uint32_t output_index)
{
    for (size_t i = 0; i < ledger->count; i++) {
        if (ledger->utxos[i].output_index == output_index &&
            memcmp(ledger->utxos[i].txid, txid, SN_TXID_LEN) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool sn_ledger_has_utxo(const sn_ledger_t *ledger,
                        const uint8_t txid[SN_TXID_LEN], uint32_t output_index)
{
    if (ledger == NULL || txid == NULL) {
        return false;
    }
    int idx = find_utxo(ledger, txid, output_index);
    return idx >= 0 && !ledger->utxos[idx].spent;
}

static int cmp_utxo(const sn_utxo_t *a, const sn_utxo_t *b)
{
    int c = memcmp(a->txid, b->txid, SN_TXID_LEN);
    if (c != 0) {
        return c;
    }
    if (a->output_index < b->output_index) {
        return -1;
    }
    if (a->output_index > b->output_index) {
        return 1;
    }
    return 0;
}

esp_err_t sn_ledger_state_root(const sn_ledger_t *ledger,
                               uint8_t out[SN_HASH_LEN])
{
    if (ledger == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const sn_utxo_t *sorted[SN_MAX_UTXOS];
    size_t n = 0;
    for (size_t i = 0; i < ledger->count; i++) {
        if (!ledger->utxos[i].spent) {
            sorted[n++] = &ledger->utxos[i];
        }
    }
    for (size_t i = 1; i < n; i++) {
        const sn_utxo_t *key = sorted[i];
        size_t j = i;
        while (j > 0 && cmp_utxo(sorted[j - 1], key) > 0) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = key;
    }

    uint8_t buf[2u + SN_MAX_UTXOS * (SN_TXID_LEN + 4u + 8u + SN_PUBKEY_LEN)];
    lw_t w = {.buf = buf, .cap = sizeof(buf), .off = 0, .ok = true};
    lw_u16(&w, (uint16_t)n);
    for (size_t i = 0; i < n; i++) {
        lw_bytes(&w, sorted[i]->txid, SN_TXID_LEN);
        lw_u32(&w, sorted[i]->output_index);
        lw_u64(&w, sorted[i]->value);
        lw_bytes(&w, sorted[i]->owner_pubkey, SN_PUBKEY_LEN);
    }
    if (!w.ok) {
        return ESP_ERR_INVALID_SIZE;
    }
    sn_iovec_t parts[2] = {
        {.data = (const uint8_t *)SN_DOMAIN_STATE,
         .len = strlen(SN_DOMAIN_STATE)},
        {.data = buf, .len = w.off},
    };
    return sn_sha256_many(parts, 2, out);
}

static void refresh_state_root(sn_ledger_t *ledger)
{
    if (sn_ledger_state_root(ledger, ledger->state_root) == ESP_OK) {
        ledger->state_root_valid = true;
    } else {
        ledger->state_root_valid = false;
    }
}

esp_err_t sn_ledger_genesis_txid(uint8_t out[SN_TXID_LEN])
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t network = SN_NETWORK_ID_TEST;
    sn_iovec_t parts[2] = {
        {.data = (const uint8_t *)SN_DOMAIN_GENESIS,
         .len = strlen(SN_DOMAIN_GENESIS)},
        {.data = &network, .len = 1},
    };
    return sn_sha256_many(parts, 2, out);
}

esp_err_t sn_ledger_genesis(sn_ledger_t *ledger,
                            const sn_genesis_output_t *outputs, size_t count,
                            uint32_t height, const uint8_t tip_hash[SN_HASH_LEN])
{
    if (ledger == NULL || outputs == NULL || count == 0 ||
        count > SN_MAX_UTXOS || tip_hash == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    sn_ledger_init(ledger);
    uint8_t genesis_txid[SN_TXID_LEN];
    esp_err_t err = sn_ledger_genesis_txid(genesis_txid);
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < count; i++) {
        if (outputs[i].value == 0 || outputs[i].value > SN_MAX_AMOUNT ||
            outputs[i].owner_pubkey[0] != 0x04u) {
            sn_ledger_init(ledger);
            return ESP_ERR_INVALID_ARG;
        }
        sn_utxo_t *u = &ledger->utxos[ledger->count++];
        memcpy(u->txid, genesis_txid, SN_TXID_LEN);
        u->output_index = (uint32_t)i;
        u->value = outputs[i].value;
        memcpy(u->owner_pubkey, outputs[i].owner_pubkey, SN_PUBKEY_LEN);
        u->spent = false;
    }
    ledger->applied_height = height;
    memcpy(ledger->tip_hash, tip_hash, SN_HASH_LEN);
    refresh_state_root(ledger);
    return ESP_OK;
}

esp_err_t sn_ledger_set_tip(sn_ledger_t *ledger,
                            const uint8_t tip_hash[SN_HASH_LEN])
{
    if (ledger == NULL || tip_hash == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(ledger->tip_hash, tip_hash, SN_HASH_LEN);
    return ESP_OK;
}

static esp_err_t apply_tx_internal(sn_ledger_t *ledger, const sn_tx_t *tx,
                                   sn_ledger_report_t *report)
{
    sn_ledger_report_t local = {0};
    if (report == NULL) {
        report = &local;
    }
    memset(report, 0, sizeof(*report));
    if (ledger == NULL || tx == NULL) {
        report->code = SN_LEDGER_ERR_ARG;
        return ESP_ERR_INVALID_ARG;
    }
    if (sn_tx_validate(tx) != ESP_OK) {
        report->code = SN_LEDGER_ERR_SHAPE;
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t txid[SN_TXID_LEN];
    if (sn_tx_id(tx, txid) != ESP_OK) {
        report->code = SN_LEDGER_ERR_SHAPE;
        return ESP_ERR_INVALID_ARG;
    }
    if (recent_contains(ledger, txid)) {
        report->code = SN_LEDGER_ERR_REPLAY;
        return ESP_ERR_INVALID_ARG;
    }

    for (uint16_t i = 0; i < tx->input_count; i++) {
        for (uint16_t j = 0; j < i; j++) {
            if (tx->inputs[i].prev_output_index ==
                    tx->inputs[j].prev_output_index &&
                memcmp(tx->inputs[i].prev_txid, tx->inputs[j].prev_txid,
                       SN_TXID_LEN) == 0) {
                report->code = SN_LEDGER_ERR_DUPLICATE_INPUT;
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    uint8_t digest[SN_HASH_LEN];
    if (sn_tx_signing_digest(tx, digest) != ESP_OK) {
        report->code = SN_LEDGER_ERR_SHAPE;
        return ESP_ERR_INVALID_ARG;
    }

    int input_indices[SN_MAX_TX_INPUTS];
    uint64_t input_sum = 0;
    for (uint16_t i = 0; i < tx->input_count; i++) {
        const sn_tx_input_t *in = &tx->inputs[i];
        int idx = find_utxo(ledger, in->prev_txid, in->prev_output_index);
        if (idx < 0) {
            report->code = SN_LEDGER_ERR_UNKNOWN_INPUT;
            return ESP_ERR_INVALID_ARG;
        }
        if (ledger->utxos[idx].spent) {
            report->code = SN_LEDGER_ERR_INPUT_SPENT;
            return ESP_ERR_INVALID_ARG;
        }
        if (memcmp(ledger->utxos[idx].owner_pubkey, in->pubkey,
                   SN_PUBKEY_LEN) != 0) {
            report->code = SN_LEDGER_ERR_OWNER_MISMATCH;
            return ESP_ERR_INVALID_ARG;
        }
        if (sn_verify_digest(in->pubkey, digest, in->signature) != ESP_OK) {
            report->code = SN_LEDGER_ERR_BAD_SIGNATURE;
            return ESP_ERR_INVALID_CRC;
        }
        if (!add_u64(input_sum, ledger->utxos[idx].value, &input_sum) ||
            input_sum > SN_MAX_AMOUNT) {
            report->code = SN_LEDGER_ERR_OVERFLOW;
            return ESP_ERR_INVALID_ARG;
        }
        input_indices[i] = idx;
    }

    uint64_t output_sum = 0;
    for (uint16_t i = 0; i < tx->output_count; i++) {
        if (tx->outputs[i].value == 0) {
            report->code = SN_LEDGER_ERR_ZERO_OUTPUT;
            return ESP_ERR_INVALID_ARG;
        }
        if (!add_u64(output_sum, tx->outputs[i].value, &output_sum) ||
            output_sum > SN_MAX_AMOUNT) {
            report->code = SN_LEDGER_ERR_OVERFLOW;
            return ESP_ERR_INVALID_ARG;
        }
    }
    report->input_sum = input_sum;
    report->output_sum = output_sum;
    if (output_sum != input_sum) {
        report->code = SN_LEDGER_ERR_CONSERVATION;
        return ESP_ERR_INVALID_ARG;
    }

    if (ledger->count < tx->input_count) {
        report->code = SN_LEDGER_ERR_SHAPE;
        return ESP_ERR_INVALID_ARG;
    }
    size_t new_count =
        ledger->count - (size_t)tx->input_count + (size_t)tx->output_count;
    if (new_count > SN_MAX_UTXOS) {
        report->code = SN_LEDGER_ERR_CAPACITY;
        return ESP_ERR_INVALID_ARG;
    }
    report->new_utxo_count = new_count;

    /* Validation is complete; commit atomically without further failure. */
    for (uint16_t i = 0; i < tx->input_count; i++) {
        ledger->utxos[input_indices[i]].spent = true;
    }
    for (uint16_t i = 0; i < tx->output_count; i++) {
        sn_utxo_t *u = &ledger->utxos[ledger->count++];
        memcpy(u->txid, txid, SN_TXID_LEN);
        u->output_index = (uint32_t)i;
        u->value = tx->outputs[i].value;
        memcpy(u->owner_pubkey, tx->outputs[i].owner_pubkey, SN_PUBKEY_LEN);
        u->spent = false;
    }
    recent_add(ledger, txid);
    refresh_state_root(ledger);
    report->code = SN_LEDGER_OK;
    return ESP_OK;
}

esp_err_t sn_ledger_apply_tx(sn_ledger_t *ledger, const sn_tx_t *tx,
                             sn_ledger_report_t *report)
{
    return apply_tx_internal(ledger, tx, report);
}

esp_err_t sn_ledger_apply_block(sn_ledger_t *ledger, const sn_block_t *block,
                                sn_ledger_report_t *report)
{
    sn_ledger_report_t local = {0};
    if (report == NULL) {
        report = &local;
    }
    memset(report, 0, sizeof(*report));
    if (ledger == NULL || block == NULL) {
        report->code = SN_LEDGER_ERR_ARG;
        return ESP_ERR_INVALID_ARG;
    }
    if (sn_block_validate(block) != ESP_OK) {
        report->code = SN_LEDGER_ERR_SHAPE;
        return ESP_ERR_INVALID_ARG;
    }
    if (block->height != ledger->applied_height + 1u) {
        report->code = SN_LEDGER_ERR_BAD_HEIGHT;
        return ESP_ERR_INVALID_ARG;
    }
    if (memcmp(block->prev_block_hash, ledger->tip_hash, SN_HASH_LEN) != 0) {
        report->code = SN_LEDGER_ERR_BAD_PREV;
        return ESP_ERR_INVALID_ARG;
    }

    sn_ledger_t *scratch = malloc(sizeof(*scratch));
    if (scratch == NULL) {
        report->code = SN_LEDGER_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    memcpy(scratch, ledger, sizeof(*scratch));
    for (uint16_t i = 0; i < block->tx_count; i++) {
        sn_ledger_report_t tx_report = {0};
        esp_err_t err = apply_tx_internal(scratch, &block->txs[i], &tx_report);
        if (err != ESP_OK) {
            *report = tx_report;
            free(scratch);
            return err;
        }
    }
    uint8_t state_root[SN_HASH_LEN];
    esp_err_t err = sn_ledger_state_root(scratch, state_root);
    if (err != ESP_OK) {
        report->code = SN_LEDGER_ERR_BAD_STATE_ROOT;
        free(scratch);
        return err;
    }
    if (memcmp(state_root, block->state_root, SN_HASH_LEN) != 0) {
        report->code = SN_LEDGER_ERR_BAD_STATE_ROOT;
        free(scratch);
        return ESP_ERR_INVALID_CRC;
    }
    scratch->applied_height = block->height;
    err = sn_block_hash(block, scratch->tip_hash);
    if (err != ESP_OK) {
        report->code = SN_LEDGER_ERR_BAD_STATE_ROOT;
        free(scratch);
        return err;
    }
    scratch->state_root_valid = true;
    memcpy(scratch->state_root, state_root, SN_HASH_LEN);
    memcpy(ledger, scratch, sizeof(*ledger));
    free(scratch);
    report->code = SN_LEDGER_OK;
    return ESP_OK;
}

uint64_t sn_ledger_balance(const sn_ledger_t *ledger,
                           const uint8_t owner_pubkey[SN_PUBKEY_LEN])
{
    if (ledger == NULL || owner_pubkey == NULL) {
        return 0;
    }
    uint64_t total = 0;
    for (size_t i = 0; i < ledger->count; i++) {
        if (ledger->utxos[i].spent) {
            continue;
        }
        if (memcmp(ledger->utxos[i].owner_pubkey, owner_pubkey,
                   SN_PUBKEY_LEN) != 0) {
            continue;
        }
        if (!add_u64(total, ledger->utxos[i].value, &total)) {
            return UINT64_MAX;
        }
    }
    return total;
}

esp_err_t sn_ledger_snapshot_encode(const sn_ledger_t *ledger, uint8_t *buf,
                                    size_t cap, size_t *out_len)
{
    if (ledger == NULL || buf == NULL || out_len == NULL ||
        ledger->count > SN_MAX_UTXOS ||
        ledger->recent_count > SN_MAX_RECENT_TXIDS) {
        return ESP_ERR_INVALID_ARG;
    }
    lw_t w = {.buf = buf, .cap = cap, .off = 0, .ok = true};
    lw_u32(&w, SN_LEDGER_SNAPSHOT_MAGIC);
    lw_u16(&w, SN_LEDGER_SNAPSHOT_VERSION);
    lw_u8(&w, SN_NETWORK_ID_TEST);
    lw_u8(&w, 0);
    lw_u32(&w, ledger->applied_height);
    lw_bytes(&w, ledger->tip_hash, SN_HASH_LEN);
    lw_u16(&w, (uint16_t)ledger->count);
    lw_u16(&w, (uint16_t)ledger->recent_count);
    lw_u16(&w, (uint16_t)ledger->recent_next);
    for (size_t i = 0; i < ledger->count; i++) {
        const sn_utxo_t *u = &ledger->utxos[i];
        lw_bytes(&w, u->txid, SN_TXID_LEN);
        lw_u32(&w, u->output_index);
        lw_u64(&w, u->value);
        lw_bytes(&w, u->owner_pubkey, SN_PUBKEY_LEN);
        lw_u8(&w, u->spent ? 1u : 0u);
    }
    for (size_t i = 0; i < ledger->recent_count; i++) {
        lw_bytes(&w, ledger->recent_txids[i], SN_TXID_LEN);
    }
    if (!w.ok) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_len = w.off;
    return ESP_OK;
}

esp_err_t sn_ledger_snapshot_decode(const uint8_t *buf, size_t len,
                                    sn_ledger_t *ledger)
{
    if (buf == NULL || ledger == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    sn_ledger_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    lr_t r = {.buf = buf, .len = len, .off = 0, .ok = true};
    uint32_t magic = 0;
    uint16_t version = 0, count = 0, recent_count = 0, recent_next = 0;
    uint8_t network = 0, reserved = 0;
    if (!lr_u32(&r, &magic) || !lr_u16(&r, &version) ||
        !lr_u8(&r, &network) || !lr_u8(&r, &reserved) ||
        !lr_u32(&r, &tmp.applied_height) ||
        !lr_bytes(&r, tmp.tip_hash, SN_HASH_LEN) ||
        !lr_u16(&r, &count) || !lr_u16(&r, &recent_count) ||
        !lr_u16(&r, &recent_next)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (magic != SN_LEDGER_SNAPSHOT_MAGIC ||
        version != SN_LEDGER_SNAPSHOT_VERSION ||
        network != SN_NETWORK_ID_TEST || reserved != 0 ||
        count > SN_MAX_UTXOS || recent_count > SN_MAX_RECENT_TXIDS ||
        recent_next >= SN_MAX_RECENT_TXIDS) {
        return ESP_ERR_INVALID_ARG;
    }
    tmp.count = count;
    tmp.recent_count = recent_count;
    tmp.recent_next = recent_next;
    for (size_t i = 0; i < count; i++) {
        sn_utxo_t *u = &tmp.utxos[i];
        uint8_t spent = 0;
        if (!lr_bytes(&r, u->txid, SN_TXID_LEN) ||
            !lr_u32(&r, &u->output_index) || !lr_u64(&r, &u->value) ||
            !lr_bytes(&r, u->owner_pubkey, SN_PUBKEY_LEN) ||
            !lr_u8(&r, &spent)) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (spent > 1u || u->value == 0 || u->value > SN_MAX_AMOUNT ||
            u->owner_pubkey[0] != 0x04u) {
            return ESP_ERR_INVALID_ARG;
        }
        u->spent = spent == 1u;
    }
    for (size_t i = 0; i < recent_count; i++) {
        if (!lr_bytes(&r, tmp.recent_txids[i], SN_TXID_LEN)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    if (!r.ok || r.off != r.len) {
        return ESP_ERR_INVALID_SIZE;
    }
    refresh_state_root(&tmp);
    memcpy(ledger, &tmp, sizeof(*ledger));
    return ESP_OK;
}
