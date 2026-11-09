#include "sn_consensus.h"

#include <string.h>

#include "sn_crypto.h"

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t off;
    bool ok;
} cw_t;

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t off;
    bool ok;
} cr_t;

static void cw_u8(cw_t *w, uint8_t v)
{
    if (!w->ok || w->off + 1u > w->cap) {
        w->ok = false;
        return;
    }
    w->buf[w->off++] = v;
}

static void cw_u16(cw_t *w, uint16_t v)
{
    cw_u8(w, (uint8_t)(v >> 8));
    cw_u8(w, (uint8_t)v);
}

static void cw_u32(cw_t *w, uint32_t v)
{
    cw_u8(w, (uint8_t)(v >> 24));
    cw_u8(w, (uint8_t)(v >> 16));
    cw_u8(w, (uint8_t)(v >> 8));
    cw_u8(w, (uint8_t)v);
}

static void cw_bytes(cw_t *w, const uint8_t *p, size_t n)
{
    if (!w->ok || w->off + n > w->cap) {
        w->ok = false;
        return;
    }
    memcpy(w->buf + w->off, p, n);
    w->off += n;
}

static bool cr_u8(cr_t *r, uint8_t *v)
{
    if (!r->ok || r->off + 1u > r->len) {
        r->ok = false;
        return false;
    }
    *v = r->buf[r->off++];
    return true;
}

static bool cr_u16(cr_t *r, uint16_t *v)
{
    uint8_t a, b;
    if (!cr_u8(r, &a) || !cr_u8(r, &b)) {
        return false;
    }
    *v = (uint16_t)(((uint16_t)a << 8) | b);
    return true;
}

static bool cr_u32(cr_t *r, uint32_t *v)
{
    uint8_t b[4];
    for (size_t i = 0; i < sizeof(b); i++) {
        if (!cr_u8(r, &b[i])) {
            return false;
        }
    }
    *v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8) | (uint32_t)b[3];
    return true;
}

static bool cr_bytes(cr_t *r, uint8_t *p, size_t n)
{
    if (!r->ok || r->off + n > r->len) {
        r->ok = false;
        return false;
    }
    memcpy(p, r->buf + r->off, n);
    r->off += n;
    return true;
}

esp_err_t sn_vote_signing_digest(uint32_t height, uint32_t round,
                                 const uint8_t block_hash[SN_HASH_LEN],
                                 uint8_t voter_id, uint8_t decision,
                                 uint8_t out[SN_HASH_LEN])
{
    if (block_hash == NULL || out == NULL || decision > SN_VOTE_ACCEPT) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t buf[4u + 4u + SN_HASH_LEN + 1u + 1u];
    cw_t w = {.buf = buf, .cap = sizeof(buf), .off = 0, .ok = true};
    cw_u32(&w, height);
    cw_u32(&w, round);
    cw_bytes(&w, block_hash, SN_HASH_LEN);
    cw_u8(&w, voter_id);
    cw_u8(&w, decision);
    if (!w.ok) {
        return ESP_ERR_INVALID_SIZE;
    }
    sn_iovec_t parts[2] = {
        {.data = (const uint8_t *)SN_DOMAIN_VOTE,
         .len = strlen(SN_DOMAIN_VOTE)},
        {.data = buf, .len = w.off},
    };
    return sn_sha256_many(parts, 2, out);
}

static const sn_validator_t *find_member(const sn_consensus_t *c,
                                         uint8_t voter_id)
{
    for (size_t i = 0; i < c->member_count; i++) {
        if (c->members[i].id == voter_id) {
            return &c->members[i];
        }
    }
    return NULL;
}

static bool member_id_exists(const sn_validator_t *members, size_t count,
                             uint8_t id)
{
    for (size_t i = 0; i < count; i++) {
        if (members[i].id == id) {
            return true;
        }
    }
    return false;
}

static bool member_pubkey_exists(const sn_validator_t *members, size_t count,
                                 const uint8_t pubkey[SN_PUBKEY_LEN])
{
    for (size_t i = 0; i < count; i++) {
        if (memcmp(members[i].pubkey, pubkey, SN_PUBKEY_LEN) == 0) {
            return true;
        }
    }
    return false;
}

esp_err_t sn_consensus_init(sn_consensus_t *c, const sn_validator_t *members,
                            size_t member_count, size_t quorum,
                            uint32_t timeout_steps)
{
    if (c == NULL || members == NULL || member_count == 0 ||
        member_count > SN_MAX_VALIDATORS || quorum <= member_count / 2u ||
        quorum > member_count || timeout_steps == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < member_count; i++) {
        if (members[i].pubkey[0] != 0x04u ||
            member_id_exists(members, i, members[i].id) ||
            member_pubkey_exists(members, i, members[i].pubkey)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    memset(c, 0, sizeof(*c));
    memcpy(c->members, members, member_count * sizeof(members[0]));
    c->member_count = member_count;
    c->quorum = quorum;
    c->timeout_steps = timeout_steps;
    return ESP_OK;
}

esp_err_t sn_consensus_start_height(sn_consensus_t *c, uint32_t height,
                                    const uint8_t block_hash[SN_HASH_LEN])
{
    if (c == NULL || block_hash == NULL || c->member_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    c->height = height;
    c->round = 0;
    memcpy(c->block_hash, block_hash, SN_HASH_LEN);
    c->have_height = true;
    c->finalized = false;
    c->vote_count = 0;
    memset(c->votes, 0, sizeof(c->votes));
    return ESP_OK;
}

esp_err_t sn_consensus_sign_vote(const sn_signer_t *signer, uint8_t voter_id,
                                 uint32_t height, uint32_t round,
                                 const uint8_t block_hash[SN_HASH_LEN],
                                 sn_vote_decision_t decision, sn_vote_t *out)
{
    if (signer == NULL || signer->sign_digest == NULL || block_hash == NULL ||
        out == NULL || decision > SN_VOTE_ACCEPT) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t digest[SN_HASH_LEN];
    esp_err_t err = sn_vote_signing_digest(height, round, block_hash, voter_id,
                                           (uint8_t)decision, digest);
    if (err != ESP_OK) {
        return err;
    }
    memset(out, 0, sizeof(*out));
    out->version = SN_PROTOCOL_VERSION;
    out->voter_id = voter_id;
    out->height = height;
    out->round = round;
    memcpy(out->block_hash, block_hash, SN_HASH_LEN);
    out->decision = (uint8_t)decision;
    return signer->sign_digest(signer->ctx, digest, out->signature);
}

esp_err_t sn_consensus_cast_vote(sn_consensus_t *c, const sn_vote_t *vote)
{
    if (c == NULL || vote == NULL || !c->have_height) {
        return ESP_ERR_INVALID_STATE;
    }
    if (vote->version != SN_PROTOCOL_VERSION ||
        vote->decision > SN_VOTE_ACCEPT ||
        vote->height != c->height || vote->round != c->round ||
        memcmp(vote->block_hash, c->block_hash, SN_HASH_LEN) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c->finalized) {
        return ESP_ERR_INVALID_STATE;
    }
    const sn_validator_t *member = find_member(c, vote->voter_id);
    if (member == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < c->vote_count; i++) {
        if (c->votes[i].voter_id == vote->voter_id) {
            /* Duplicate/equivocating votes are rejected, never counted. */
            return ESP_ERR_INVALID_STATE;
        }
    }
    uint8_t digest[SN_HASH_LEN];
    esp_err_t err = sn_vote_signing_digest(c->height, c->round, vote->block_hash,
                                           vote->voter_id, vote->decision,
                                           digest);
    if (err != ESP_OK) {
        return err;
    }
    if (sn_verify_digest(member->pubkey, digest, vote->signature) != ESP_OK) {
        return ESP_ERR_INVALID_CRC;
    }
    if (c->vote_count >= SN_MAX_VALIDATORS) {
        return ESP_ERR_NO_MEM;
    }
    sn_consensus_vote_record_t *rec = &c->votes[c->vote_count++];
    rec->voter_id = vote->voter_id;
    memcpy(rec->block_hash, vote->block_hash, SN_HASH_LEN);
    rec->decision = vote->decision;
    memcpy(rec->signature, vote->signature, SN_SIG_LEN);
    if (vote->decision == SN_VOTE_ACCEPT &&
        sn_consensus_accept_count(c) >= c->quorum) {
        c->finalized = true;
    }
    return ESP_OK;
}

esp_err_t sn_consensus_timeout(sn_consensus_t *c)
{
    if (c == NULL || !c->have_height || c->member_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (c->finalized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (c->round == UINT32_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    c->round++;
    c->vote_count = 0;
    memset(c->votes, 0, sizeof(c->votes));
    return ESP_OK;
}

bool sn_consensus_is_finalized(const sn_consensus_t *c)
{
    return c != NULL && c->finalized;
}

size_t sn_consensus_accept_count(const sn_consensus_t *c)
{
    if (c == NULL) {
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < c->vote_count; i++) {
        if (c->votes[i].decision == SN_VOTE_ACCEPT) {
            n++;
        }
    }
    return n;
}

esp_err_t sn_consensus_proposer_id(const sn_consensus_t *c, uint8_t *out_id)
{
    if (c == NULL || out_id == NULL || c->member_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t index = ((uint64_t)c->height + (uint64_t)c->round +
                      (uint64_t)c->member_count - 1u) %
                     (uint64_t)c->member_count;
    *out_id = c->members[index].id;
    return ESP_OK;
}

esp_err_t sn_vote_encode(const sn_vote_t *vote, uint8_t *buf, size_t cap,
                         size_t *out_len)
{
    if (vote == NULL || buf == NULL || out_len == NULL ||
        vote->version != SN_PROTOCOL_VERSION ||
        vote->decision > SN_VOTE_ACCEPT) {
        return ESP_ERR_INVALID_ARG;
    }
    cw_t w = {.buf = buf, .cap = cap, .off = 0, .ok = true};
    cw_u16(&w, vote->version);
    cw_u8(&w, vote->voter_id);
    cw_u32(&w, vote->height);
    cw_u32(&w, vote->round);
    cw_bytes(&w, vote->block_hash, SN_HASH_LEN);
    cw_u8(&w, vote->decision);
    cw_bytes(&w, vote->signature, SN_SIG_LEN);
    if (!w.ok) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_len = w.off;
    return ESP_OK;
}

esp_err_t sn_vote_decode(const uint8_t *buf, size_t len, sn_vote_t *out)
{
    if (buf == NULL || out == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    cr_t r = {.buf = buf, .len = len, .off = 0, .ok = true};
    if (!cr_u16(&r, &out->version) || !cr_u8(&r, &out->voter_id) ||
        !cr_u32(&r, &out->height) || !cr_u32(&r, &out->round) ||
        !cr_bytes(&r, out->block_hash, SN_HASH_LEN) ||
        !cr_u8(&r, &out->decision) ||
        !cr_bytes(&r, out->signature, SN_SIG_LEN)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!r.ok || r.off != r.len || out->version != SN_PROTOCOL_VERSION ||
        out->decision > SN_VOTE_ACCEPT) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t sn_consensus_snapshot_encode(const sn_consensus_t *c, uint8_t *buf,
                                       size_t cap, size_t *out_len)
{
    if (c == NULL || buf == NULL || out_len == NULL ||
        c->member_count > SN_MAX_VALIDATORS ||
        c->vote_count > SN_MAX_VALIDATORS) {
        return ESP_ERR_INVALID_ARG;
    }
    cw_t w = {.buf = buf, .cap = cap, .off = 0, .ok = true};
    cw_u32(&w, SN_CONSENSUS_SNAPSHOT_MAGIC);
    cw_u16(&w, SN_CONSENSUS_SNAPSHOT_VERSION);
    cw_u16(&w, (uint16_t)c->member_count);
    cw_u16(&w, (uint16_t)c->quorum);
    cw_u16(&w, 0);
    cw_u32(&w, c->timeout_steps);
    cw_u32(&w, c->height);
    cw_u32(&w, c->round);
    cw_u8(&w, c->have_height ? 1u : 0u);
    cw_u8(&w, c->finalized ? 1u : 0u);
    cw_u16(&w, (uint16_t)c->vote_count);
    cw_bytes(&w, c->block_hash, SN_HASH_LEN);
    for (size_t i = 0; i < c->member_count; i++) {
        cw_u8(&w, c->members[i].id);
        cw_bytes(&w, c->members[i].pubkey, SN_PUBKEY_LEN);
    }
    for (size_t i = 0; i < c->vote_count; i++) {
        cw_u8(&w, c->votes[i].voter_id);
        cw_bytes(&w, c->votes[i].block_hash, SN_HASH_LEN);
        cw_u8(&w, c->votes[i].decision);
        cw_bytes(&w, c->votes[i].signature, SN_SIG_LEN);
    }
    if (!w.ok) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_len = w.off;
    return ESP_OK;
}

esp_err_t sn_consensus_snapshot_decode(const uint8_t *buf, size_t len,
                                       sn_consensus_t *c)
{
    if (buf == NULL || c == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    sn_consensus_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    cr_t r = {.buf = buf, .len = len, .off = 0, .ok = true};
    uint32_t magic = 0;
    uint16_t version = 0, member_count = 0, quorum = 0, reserved = 0;
    uint16_t vote_count = 0;
    uint8_t have_height = 0, finalized = 0;
    if (!cr_u32(&r, &magic) || !cr_u16(&r, &version) ||
        !cr_u16(&r, &member_count) || !cr_u16(&r, &quorum) ||
        !cr_u16(&r, &reserved) || !cr_u32(&r, &tmp.timeout_steps) ||
        !cr_u32(&r, &tmp.height) || !cr_u32(&r, &tmp.round) ||
        !cr_u8(&r, &have_height) || !cr_u8(&r, &finalized) ||
        !cr_u16(&r, &vote_count) ||
        !cr_bytes(&r, tmp.block_hash, SN_HASH_LEN)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (magic != SN_CONSENSUS_SNAPSHOT_MAGIC ||
        version != SN_CONSENSUS_SNAPSHOT_VERSION || reserved != 0 ||
        member_count == 0 || member_count > SN_MAX_VALIDATORS ||
        quorum <= member_count / 2u || quorum > member_count ||
        tmp.timeout_steps == 0 || have_height > 1u || finalized > 1u ||
        vote_count > member_count) {
        return ESP_ERR_INVALID_ARG;
    }
    tmp.member_count = member_count;
    tmp.quorum = quorum;
    tmp.have_height = have_height == 1u;
    tmp.finalized = finalized == 1u;
    for (size_t i = 0; i < member_count; i++) {
        if (!cr_u8(&r, &tmp.members[i].id) ||
            !cr_bytes(&r, tmp.members[i].pubkey, SN_PUBKEY_LEN)) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (tmp.members[i].pubkey[0] != 0x04u ||
            member_id_exists(tmp.members, i, tmp.members[i].id) ||
            member_pubkey_exists(tmp.members, i, tmp.members[i].pubkey)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    tmp.vote_count = vote_count;
    for (size_t i = 0; i < vote_count; i++) {
        sn_consensus_vote_record_t *rec = &tmp.votes[i];
        if (!cr_u8(&r, &rec->voter_id) ||
            !cr_bytes(&r, rec->block_hash, SN_HASH_LEN) ||
            !cr_u8(&r, &rec->decision) ||
            !cr_bytes(&r, rec->signature, SN_SIG_LEN)) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (rec->decision > SN_VOTE_ACCEPT ||
            memcmp(rec->block_hash, tmp.block_hash, SN_HASH_LEN) != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        const sn_validator_t *member = find_member(&tmp, rec->voter_id);
        if (member == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t j = 0; j < i; j++) {
            if (tmp.votes[j].voter_id == rec->voter_id) {
                return ESP_ERR_INVALID_ARG;
            }
        }
        uint8_t digest[SN_HASH_LEN];
        esp_err_t err = sn_vote_signing_digest(
            tmp.height, tmp.round, rec->block_hash, rec->voter_id,
            rec->decision, digest);
        if (err != ESP_OK) {
            return err;
        }
        if (sn_verify_digest(member->pubkey, digest, rec->signature) !=
            ESP_OK) {
            return ESP_ERR_INVALID_CRC;
        }
    }
    if (!r.ok || r.off != r.len) {
        return ESP_ERR_INVALID_SIZE;
    }
    bool expected_final =
        tmp.have_height && sn_consensus_accept_count(&tmp) >= tmp.quorum;
    if (tmp.finalized != expected_final) {
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(c, &tmp, sizeof(*c));
    return ESP_OK;
}
