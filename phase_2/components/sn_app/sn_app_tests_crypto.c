#include <string.h>

#include "sn_app_internal.h"

void sn_app_test_crypto(sn_test_ctx_t *ctx)
{
    const sn_test_key_t *alice = sn_app_test_key(4);
    const sn_test_key_t *bob = sn_app_test_key(5);
    sn_test_begin(ctx, "crypto.test_key.ready", "test keys initialized");
    if (alice != NULL && bob != NULL && alice->pubkey[0] == 0x04u) {
        sn_test_pass(ctx, "ready");
    } else {
        sn_test_fail(ctx, "not_ready");
        return;
    }

    const char *message = "SuperNova Phase 2 crypto self-test";
    uint8_t digest[SN_HASH_LEN];
    esp_err_t err =
        sn_sha256((const uint8_t *)message, strlen(message), digest);
    sn_test_expect_ok(ctx, "crypto.sha256", "sha256 digest", err);

    sn_signer_t signer = sn_app_test_signer(4);
    uint8_t signature[SN_SIG_LEN];
    err = signer.sign_digest(signer.ctx, digest, signature);
    sn_test_expect_ok(ctx, "crypto.sig.sign", "signature produced", err);

    err = sn_verify_digest(alice->pubkey, digest, signature);
    sn_test_expect_ok(ctx, "crypto.sig.accept", "signature accepted", err);

    uint8_t tampered[SN_HASH_LEN];
    memcpy(tampered, digest, sizeof(tampered));
    tampered[0] ^= 0x01u;
    err = sn_verify_digest(alice->pubkey, tampered, signature);
    sn_test_expect_err(ctx, "crypto.sig.reject_tamper", "signature rejected",
                       err, ESP_ERR_INVALID_CRC);

    err = sn_verify_digest(bob->pubkey, digest, signature);
    sn_test_expect_err(ctx, "crypto.sig.reject_key", "signature rejected", err,
                       ESP_ERR_INVALID_CRC);

    uint8_t zero_sig[SN_SIG_LEN];
    memset(zero_sig, 0, sizeof(zero_sig));
    err = sn_verify_digest(alice->pubkey, digest, zero_sig);
    sn_test_expect_err(ctx, "crypto.sig.reject_zero", "signature rejected", err,
                       ESP_ERR_INVALID_CRC);

    uint8_t high_sig[SN_SIG_LEN];
    memset(high_sig, 0xFF, sizeof(high_sig));
    err = sn_verify_digest(alice->pubkey, digest, high_sig);
    sn_test_expect_err(ctx, "crypto.sig.reject_range", "signature rejected",
                       err, ESP_ERR_INVALID_CRC);

    sn_ds_signer_t ds;
    memset(&ds, 0, sizeof(ds));
    err = sn_ds_signer_init(&ds);
    sn_test_expect_err(ctx, "crypto.ds.disabled", "ESP_ERR_NOT_SUPPORTED", err,
                       ESP_ERR_NOT_SUPPORTED);
    sn_test_begin(ctx, "crypto.ds.no_efuse", "adapter remains uninitialized");
    if (!ds.initialized) {
        sn_test_pass(ctx, "no_state_change");
    } else {
        sn_test_fail(ctx, "unexpected_initialization");
    }

    sn_signer_t ds_signer = sn_ds_signer_as_signer(&ds);
    uint8_t ds_pubkey[SN_PUBKEY_LEN];
    err = ds_signer.get_pubkey(ds_signer.ctx, ds_pubkey);
    sn_test_expect_err(ctx, "crypto.ds.get_pubkey", "not supported", err,
                       ESP_ERR_NOT_SUPPORTED);
}
