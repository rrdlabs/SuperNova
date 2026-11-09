#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sn_crypto.h"
#include "sn_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SN_MAX_VALIDATORS 8u
#define SN_CONSENSUS_SNAPSHOT_MAGIC 0x534E3243u /* "SN2C" */
#define SN_CONSENSUS_SNAPSHOT_VERSION 1u

typedef struct {
    uint8_t id;
    uint8_t pubkey[SN_PUBKEY_LEN];
} sn_validator_t;

typedef enum {
    SN_VOTE_REJECT = 0,
    SN_VOTE_ACCEPT = 1,
} sn_vote_decision_t;

typedef struct {
    uint16_t version;
    uint8_t voter_id;
    uint32_t height;
    uint32_t round;
    uint8_t block_hash[SN_HASH_LEN];
    uint8_t decision;
    uint8_t signature[SN_SIG_LEN];
} sn_vote_t;

typedef struct {
    uint8_t voter_id;
    uint8_t block_hash[SN_HASH_LEN];
    uint8_t decision;
    uint8_t signature[SN_SIG_LEN];
} sn_consensus_vote_record_t;

typedef struct {
    sn_validator_t members[SN_MAX_VALIDATORS];
    size_t member_count;
    size_t quorum;
    uint32_t timeout_steps;
    uint32_t height;
    uint32_t round;
    uint8_t block_hash[SN_HASH_LEN];
    bool have_height;
    bool finalized;
    sn_consensus_vote_record_t votes[SN_MAX_VALIDATORS];
    size_t vote_count;
} sn_consensus_t;

esp_err_t sn_consensus_init(sn_consensus_t *c, const sn_validator_t *members,
                            size_t member_count, size_t quorum,
                            uint32_t timeout_steps);
esp_err_t sn_consensus_start_height(sn_consensus_t *c, uint32_t height,
                                    const uint8_t block_hash[SN_HASH_LEN]);
esp_err_t sn_consensus_sign_vote(const sn_signer_t *signer, uint8_t voter_id,
                                 uint32_t height, uint32_t round,
                                 const uint8_t block_hash[SN_HASH_LEN],
                                 sn_vote_decision_t decision, sn_vote_t *out);
esp_err_t sn_consensus_cast_vote(sn_consensus_t *c, const sn_vote_t *vote);
esp_err_t sn_consensus_timeout(sn_consensus_t *c);

bool sn_consensus_is_finalized(const sn_consensus_t *c);
size_t sn_consensus_accept_count(const sn_consensus_t *c);
esp_err_t sn_consensus_proposer_id(const sn_consensus_t *c, uint8_t *out_id);

esp_err_t sn_vote_signing_digest(uint32_t height, uint32_t round,
                                 const uint8_t block_hash[SN_HASH_LEN],
                                 uint8_t voter_id, uint8_t decision,
                                 uint8_t out[SN_HASH_LEN]);
esp_err_t sn_vote_encode(const sn_vote_t *vote, uint8_t *buf, size_t cap,
                         size_t *out_len);
esp_err_t sn_vote_decode(const uint8_t *buf, size_t len, sn_vote_t *out);

esp_err_t sn_consensus_snapshot_encode(const sn_consensus_t *c, uint8_t *buf,
                                       size_t cap, size_t *out_len);
esp_err_t sn_consensus_snapshot_decode(const uint8_t *buf, size_t len,
                                       sn_consensus_t *c);

#ifdef __cplusplus
}
#endif
