#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SN_HASH_LEN
#define SN_HASH_LEN 32u
#endif
#ifndef SN_PUBKEY_LEN
#define SN_PUBKEY_LEN 65u
#endif
#ifndef SN_SIG_LEN
#define SN_SIG_LEN 64u
#endif

typedef struct {
    const uint8_t *data;
    size_t len;
} sn_iovec_t;

esp_err_t sn_sha256(const uint8_t *data, size_t len, uint8_t out[SN_HASH_LEN]);
esp_err_t sn_sha256_many(const sn_iovec_t *parts, size_t count,
                         uint8_t out[SN_HASH_LEN]);

/*
 * Signing-provider interface. The software test-key signer is the baseline.
 * A future hardware-backed adapter must implement this interface without
 * changing the protocol or ledger code.
 */
typedef struct {
    const char *name;
    void *ctx;
    esp_err_t (*get_pubkey)(void *ctx, uint8_t out[SN_PUBKEY_LEN]);
    esp_err_t (*sign_digest)(void *ctx, const uint8_t digest[SN_HASH_LEN],
                             uint8_t out_sig[SN_SIG_LEN]);
} sn_signer_t;

esp_err_t sn_verify_digest(const uint8_t pubkey[SN_PUBKEY_LEN],
                           const uint8_t digest[SN_HASH_LEN],
                           const uint8_t signature[SN_SIG_LEN]);

/* Software test-key baseline: deterministic ECDSA P-256/SHA-256. */
typedef struct {
    void *impl;
} sn_soft_signer_t;

esp_err_t sn_soft_signer_init(sn_soft_signer_t *signer,
                              const uint8_t scalar[SN_HASH_LEN]);
esp_err_t sn_soft_signer_get_pubkey(sn_soft_signer_t *signer,
                                    uint8_t out[SN_PUBKEY_LEN]);
esp_err_t sn_soft_signer_sign(sn_soft_signer_t *signer,
                              const uint8_t digest[SN_HASH_LEN],
                              uint8_t out_sig[SN_SIG_LEN]);
void sn_soft_signer_deinit(sn_soft_signer_t *signer);
sn_signer_t sn_soft_signer_as_signer(sn_soft_signer_t *signer);

/*
 * Disabled/unprovisioned ESP32-S3 Digital Signature adapter boundary.
 * This performs no eFuse read, write, burn, program, or protect operation.
 * The DS peripheral is RSA-based and is not used for the ECDSA test path.
 */
typedef struct {
    bool initialized;
} sn_ds_signer_t;

esp_err_t sn_ds_signer_init(sn_ds_signer_t *signer);
sn_signer_t sn_ds_signer_as_signer(sn_ds_signer_t *signer);

#ifdef __cplusplus
}
#endif
