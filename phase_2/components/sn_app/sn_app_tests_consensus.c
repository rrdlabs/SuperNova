#include <string.h>

#include "sn_app_internal.h"

static esp_err_t make_consensus(sn_consensus_t *c, size_t quorum)
{
    sn_validator_t members[SN_TEST_VALIDATOR_COUNT];
    memset(members, 0, sizeof(members));
    for (size_t i = 0; i < SN_TEST_VALIDATOR_COUNT; i++) {
        const sn_test_key_t *key = sn_app_test_key(i);
        if (key == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        members[i].id = (uint8_t)i;
        memcpy(members[i].pubkey, key->pubkey, SN_PUBKEY_LEN);
    }
    return sn_consensus_init(c, members, SN_TEST_VALIDATOR_COUNT, quorum, 10);
}

static esp_err_t sign_for(const sn_consensus_t *c, size_t key_index,
                          sn_vote_decision_t decision, sn_vote_t *vote)
{
    sn_signer_t signer = sn_app_test_signer(key_index);
    if (signer.sign_digest == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return sn_consensus_sign_vote(&signer, (uint8_t)key_index, c->height,
                                  c->round, c->block_hash, decision, vote);
}

static void fill_hash(uint8_t hash[SN_HASH_LEN], uint8_t seed)
{
    for (size_t i = 0; i < SN_HASH_LEN; i++) {
        hash[i] = (uint8_t)(seed + i);
    }
}

static void test_finality_and_quorum(sn_test_ctx_t *ctx)
{
    sn_consensus_t c;
    uint8_t hash[SN_HASH_LEN];
    fill_hash(hash, 0x10);
    esp_err_t err = make_consensus(&c, 3);
    sn_test_expect_ok(ctx, "consensus.init", "4 validators/quorum 3", err);
    err = sn_consensus_start_height(&c, 1, hash);
    sn_test_expect_ok(ctx, "consensus.start", "height started", err);

    for (size_t i = 0; i < 2; i++) {
        sn_vote_t vote;
        err = sign_for(&c, i, SN_VOTE_ACCEPT, &vote);
        if (err == ESP_OK) {
            err = sn_consensus_cast_vote(&c, &vote);
        }
        sn_test_expect_ok(ctx, "consensus.vote.cast", "vote accepted", err);
    }
    sn_test_expect_true(ctx, "consensus.no_quorum", "pending, not final",
                        !sn_consensus_is_finalized(&c) &&
                            sn_consensus_accept_count(&c) == 2,
                        "pending/2");

    sn_vote_t third;
    err = sign_for(&c, 2, SN_VOTE_ACCEPT, &third);
    if (err == ESP_OK) {
        err = sn_consensus_cast_vote(&c, &third);
    }
    sn_test_expect_ok(ctx, "consensus.finality.vote", "third vote accepted",
                      err);
    sn_test_expect_true(ctx, "consensus.finality", "finalized at quorum",
                        sn_consensus_is_finalized(&c) &&
                            sn_consensus_accept_count(&c) == 3,
                        "final/3");

    uint8_t proposer = 0;
    err = sn_consensus_proposer_id(&c, &proposer);
    sn_test_expect_true(ctx, "consensus.proposer", "height 1 round 0 proposer",
                        err == ESP_OK && proposer == 0, "validator 0");
}

static void test_duplicate_and_equivocation(sn_test_ctx_t *ctx)
{
    sn_consensus_t c;
    uint8_t hash[SN_HASH_LEN];
    fill_hash(hash, 0x30);
    (void)make_consensus(&c, 3);
    (void)sn_consensus_start_height(&c, 1, hash);

    sn_vote_t vote;
    esp_err_t err = sign_for(&c, 0, SN_VOTE_ACCEPT, &vote);
    if (err == ESP_OK) {
        err = sn_consensus_cast_vote(&c, &vote);
    }
    sn_test_expect_ok(ctx, "consensus.duplicate.first", "first vote accepted",
                      err);
    err = sn_consensus_cast_vote(&c, &vote);
    sn_test_expect_err(ctx, "consensus.duplicate_vote", "second vote rejected",
                       err, ESP_ERR_INVALID_STATE);
    sn_test_expect_true(ctx, "consensus.duplicate.count",
                        "not double counted",
                        sn_consensus_accept_count(&c) == 1, "1");

    uint8_t other_hash[SN_HASH_LEN];
    fill_hash(other_hash, 0x70);
    sn_vote_t equivocation;
    sn_signer_t signer = sn_app_test_signer(0);
    err = sn_consensus_sign_vote(&signer, 0, c.height, c.round, other_hash,
                                 SN_VOTE_ACCEPT, &equivocation);
    if (err == ESP_OK) {
        err = sn_consensus_cast_vote(&c, &equivocation);
    }
    sn_test_expect_err(ctx, "consensus.equivocation", "conflicting vote rejected",
                       err, ESP_ERR_INVALID_ARG);
    sn_test_expect_true(ctx, "consensus.equivocation.count",
                        "vote count unchanged",
                        sn_consensus_accept_count(&c) == 1, "1");

    sn_vote_t bad;
    err = sign_for(&c, 1, SN_VOTE_ACCEPT, &bad);
    if (err == ESP_OK) {
        bad.signature[0] ^= 0xFFu;
        err = sn_consensus_cast_vote(&c, &bad);
    }
    sn_test_expect_err(ctx, "consensus.bad_signature", "bad signature rejected",
                       err, ESP_ERR_INVALID_CRC);
}

static void test_partition(sn_test_ctx_t *ctx)
{
    sn_consensus_t a;
    sn_consensus_t b;
    uint8_t hash_a[SN_HASH_LEN];
    uint8_t hash_b[SN_HASH_LEN];
    fill_hash(hash_a, 0x90);
    fill_hash(hash_b, 0xB0);
    (void)make_consensus(&a, 3);
    (void)make_consensus(&b, 3);
    (void)sn_consensus_start_height(&a, 1, hash_a);
    (void)sn_consensus_start_height(&b, 1, hash_b);
    for (size_t i = 0; i < 2; i++) {
        sn_vote_t vote_a;
        sn_vote_t vote_b;
        (void)sign_for(&a, i, SN_VOTE_ACCEPT, &vote_a);
        (void)sn_consensus_cast_vote(&a, &vote_a);
        (void)sign_for(&b, i, SN_VOTE_ACCEPT, &vote_b);
        (void)sn_consensus_cast_vote(&b, &vote_b);
    }
    sn_test_expect_true(ctx, "consensus.partition", "neither side finalizes",
                        !sn_consensus_is_finalized(&a) &&
                            !sn_consensus_is_finalized(&b) &&
                            sn_consensus_accept_count(&a) == 2 &&
                            sn_consensus_accept_count(&b) == 2,
                        "pending/pending");
}

static void test_restore(sn_test_ctx_t *ctx)
{
    sn_consensus_t c;
    uint8_t hash[SN_HASH_LEN];
    fill_hash(hash, 0xD0);
    (void)make_consensus(&c, 3);
    (void)sn_consensus_start_height(&c, 1, hash);
    for (size_t i = 0; i < 2; i++) {
        sn_vote_t vote;
        (void)sign_for(&c, i, SN_VOTE_ACCEPT, &vote);
        (void)sn_consensus_cast_vote(&c, &vote);
    }
    uint8_t snapshot[2048];
    size_t snapshot_len = 0;
    esp_err_t err =
        sn_consensus_snapshot_encode(&c, snapshot, sizeof(snapshot),
                                     &snapshot_len);
    sn_test_expect_ok(ctx, "consensus.restore.encode", "snapshot encoded", err);
    sn_consensus_t restored;
    memset(&restored, 0, sizeof(restored));
    err = sn_consensus_snapshot_decode(snapshot, snapshot_len, &restored);
    sn_test_expect_ok(ctx, "consensus.restore.decode", "snapshot decoded", err);
    sn_test_expect_true(ctx, "consensus.restore.pending",
                        "pending votes restored",
                        !sn_consensus_is_finalized(&restored) &&
                            sn_consensus_accept_count(&restored) == 2,
                        "pending/2");
    sn_vote_t third;
    err = sign_for(&restored, 2, SN_VOTE_ACCEPT, &third);
    if (err == ESP_OK) {
        err = sn_consensus_cast_vote(&restored, &third);
    }
    sn_test_expect_ok(ctx, "consensus.restore.third", "third vote accepted",
                      err);
    sn_test_expect_true(ctx, "consensus.restore.finality",
                        "restored state finalizes",
                        sn_consensus_is_finalized(&restored),
                        "final");
}

static void test_timeout(sn_test_ctx_t *ctx)
{
    sn_consensus_t c;
    uint8_t hash[SN_HASH_LEN];
    fill_hash(hash, 0xE0);
    (void)make_consensus(&c, 3);
    (void)sn_consensus_start_height(&c, 1, hash);
    for (size_t i = 0; i < 2; i++) {
        sn_vote_t vote;
        (void)sign_for(&c, i, SN_VOTE_ACCEPT, &vote);
        (void)sn_consensus_cast_vote(&c, &vote);
    }
    uint8_t proposer_before = 0xFF;
    (void)sn_consensus_proposer_id(&c, &proposer_before);
    esp_err_t err = sn_consensus_timeout(&c);
    sn_test_expect_ok(ctx, "consensus.timeout", "round advanced", err);
    uint8_t proposer_after = 0xFF;
    (void)sn_consensus_proposer_id(&c, &proposer_after);
    sn_test_expect_true(ctx, "consensus.timeout.state",
                        "votes cleared, proposer rotated",
                        c.round == 1 && c.vote_count == 0 &&
                            !sn_consensus_is_finalized(&c) &&
                            proposer_before == 0 && proposer_after == 1,
                        "round=1/votes=0/proposer=1");

    sn_vote_t stale;
    sn_signer_t signer = sn_app_test_signer(0);
    err = sn_consensus_sign_vote(&signer, 0, 1, 0, hash, SN_VOTE_ACCEPT,
                                 &stale);
    if (err == ESP_OK) {
        err = sn_consensus_cast_vote(&c, &stale);
    }
    sn_test_expect_err(ctx, "consensus.timeout.stale", "old round rejected",
                       err, ESP_ERR_INVALID_ARG);

    for (size_t i = 0; i < 3; i++) {
        sn_vote_t vote;
        err = sign_for(&c, i, SN_VOTE_ACCEPT, &vote);
        if (err == ESP_OK) {
            err = sn_consensus_cast_vote(&c, &vote);
        }
        sn_test_expect_ok(ctx, "consensus.timeout.vote", "new round vote",
                          err);
    }
    sn_test_expect_true(ctx, "consensus.timeout.finality",
                        "new round finalizes",
                        sn_consensus_is_finalized(&c), "final");
}

static void test_snapshot_finalized(sn_test_ctx_t *ctx)
{
    sn_consensus_t c;
    uint8_t hash[SN_HASH_LEN];
    fill_hash(hash, 0x42);
    (void)make_consensus(&c, 3);
    (void)sn_consensus_start_height(&c, 1, hash);
    for (size_t i = 0; i < 3; i++) {
        sn_vote_t vote;
        (void)sign_for(&c, i, SN_VOTE_ACCEPT, &vote);
        (void)sn_consensus_cast_vote(&c, &vote);
    }
    uint8_t snapshot[2048];
    size_t snapshot_len = 0;
    esp_err_t err =
        sn_consensus_snapshot_encode(&c, snapshot, sizeof(snapshot),
                                     &snapshot_len);
    sn_consensus_t restored;
    memset(&restored, 0, sizeof(restored));
    if (err == ESP_OK) {
        err = sn_consensus_snapshot_decode(snapshot, snapshot_len, &restored);
    }
    sn_test_expect_ok(ctx, "consensus.snapshot", "finalized state restored",
                      err);
    uint8_t proposer_a = 0;
    uint8_t proposer_b = 0;
    (void)sn_consensus_proposer_id(&c, &proposer_a);
    (void)sn_consensus_proposer_id(&restored, &proposer_b);
    sn_test_expect_true(
        ctx, "consensus.snapshot.match", "restored state matches",
        sn_consensus_is_finalized(&restored) &&
            restored.height == c.height && restored.round == c.round &&
            restored.vote_count == c.vote_count &&
            sn_consensus_accept_count(&restored) ==
                sn_consensus_accept_count(&c) &&
            proposer_a == proposer_b &&
            memcmp(restored.block_hash, c.block_hash, SN_HASH_LEN) == 0,
        "match");
}

static void test_vote_wire(sn_test_ctx_t *ctx)
{
    sn_consensus_t c;
    uint8_t hash[SN_HASH_LEN];
    fill_hash(hash, 0x55);
    (void)make_consensus(&c, 3);
    (void)sn_consensus_start_height(&c, 1, hash);
    sn_vote_t vote;
    esp_err_t err = sign_for(&c, 0, SN_VOTE_ACCEPT, &vote);
    uint8_t wire[256];
    size_t wire_len = 0;
    if (err == ESP_OK) {
        err = sn_vote_encode(&vote, wire, sizeof(wire), &wire_len);
    }
    sn_test_expect_ok(ctx, "consensus.vote.wire", "vote encoded", err);
    sn_vote_t decoded;
    err = sn_vote_decode(wire, wire_len, &decoded);
    sn_test_expect_ok(ctx, "consensus.vote.wire.decode", "vote decoded", err);
    err = sn_consensus_cast_vote(&c, &decoded);
    sn_test_expect_ok(ctx, "consensus.vote.wire.cast", "decoded vote accepted",
                      err);
}

void sn_app_test_consensus(sn_test_ctx_t *ctx)
{
    test_finality_and_quorum(ctx);
    test_duplicate_and_equivocation(ctx);
    test_partition(ctx);
    test_restore(ctx);
    test_timeout(ctx);
    test_snapshot_finalized(ctx);
    test_vote_wire(ctx);
}
