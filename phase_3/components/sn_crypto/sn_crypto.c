#include "sn_crypto.h"

#include <stdlib.h>
#include <string.h>

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

/*
 * Deterministic, non-cryptographic RNG used only for ECP blinding and for
 * deriving the public half of fixed test scalars. It is not a key source and
 * must never be used for production key generation.
 */
static int test_blinding_rng(void *ctx, unsigned char *out, size_t len)
{
    uint32_t state = 0x534E3255u; /* "SN2U" */
    if (ctx != NULL) {
        state = *(const uint32_t *)ctx;
    }
    for (size_t i = 0; i < len; i++) {
        state = state * 1664525u + 1013904223u;
        out[i] = (uint8_t)(state >> 24);
    }
    if (ctx != NULL) {
        *(uint32_t *)ctx = state;
    }
    return 0;
}

esp_err_t sn_sha256(const uint8_t *data, size_t len, uint8_t out[SN_HASH_LEN])
{
    sn_iovec_t part = {.data = data, .len = len};
    return sn_sha256_many(&part, 1, out);
}

esp_err_t sn_sha256_many(const sn_iovec_t *parts, size_t count,
                         uint8_t out[SN_HASH_LEN])
{
    if (out == NULL || (count > 0 && parts == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    int ret = mbedtls_sha256_starts(&ctx, 0);
    if (ret != 0) {
        mbedtls_sha256_free(&ctx);
        return ESP_FAIL;
    }
    for (size_t i = 0; i < count; i++) {
        if (parts[i].len > 0 && parts[i].data == NULL) {
            mbedtls_sha256_free(&ctx);
            return ESP_ERR_INVALID_ARG;
        }
        ret = mbedtls_sha256_update(&ctx, parts[i].data, parts[i].len);
        if (ret != 0) {
            mbedtls_sha256_free(&ctx);
            return ESP_FAIL;
        }
    }
    ret = mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t sn_verify_digest(const uint8_t pubkey[SN_PUBKEY_LEN],
                           const uint8_t digest[SN_HASH_LEN],
                           const uint8_t signature[SN_SIG_LEN])
{
    if (pubkey == NULL || digest == NULL || signature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    esp_err_t result = ESP_ERR_INVALID_CRC;
    int ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) {
        goto cleanup;
    }
    ret = mbedtls_ecp_point_read_binary(&grp, &q, pubkey, SN_PUBKEY_LEN);
    if (ret != 0) {
        goto cleanup;
    }
    ret = mbedtls_ecp_check_pubkey(&grp, &q);
    if (ret != 0) {
        goto cleanup;
    }
    ret = mbedtls_mpi_read_binary(&r, signature, SN_SIG_LEN / 2u);
    if (ret != 0) {
        goto cleanup;
    }
    ret = mbedtls_mpi_read_binary(&s, signature + SN_SIG_LEN / 2u,
                                  SN_SIG_LEN / 2u);
    if (ret != 0) {
        goto cleanup;
    }
    ret = mbedtls_ecdsa_verify(&grp, digest, SN_HASH_LEN, &q, &r, &s);
    if (ret == 0) {
        result = ESP_OK;
    }

cleanup:
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return result;
}

typedef struct {
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point q;
    uint32_t rng_state;
    bool ready;
} soft_signer_impl_t;

static void soft_signer_impl_free(soft_signer_impl_t *impl)
{
    if (impl == NULL) {
        return;
    }
    mbedtls_ecp_group_free(&impl->grp);
    mbedtls_mpi_free(&impl->d);
    mbedtls_ecp_point_free(&impl->q);
    memset(impl, 0, sizeof(*impl));
    free(impl);
}

esp_err_t sn_soft_signer_init(sn_soft_signer_t *signer,
                              const uint8_t scalar[SN_HASH_LEN])
{
    if (signer == NULL || scalar == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    signer->impl = NULL;
    soft_signer_impl_t *impl = calloc(1, sizeof(*impl));
    if (impl == NULL) {
        return ESP_ERR_NO_MEM;
    }
    mbedtls_ecp_group_init(&impl->grp);
    mbedtls_mpi_init(&impl->d);
    mbedtls_ecp_point_init(&impl->q);
    impl->rng_state = 0x534E3255u;
    int ret = mbedtls_ecp_group_load(&impl->grp, MBEDTLS_ECP_DP_SECP256R1);
    if (ret == 0) {
        ret = mbedtls_mpi_read_binary(&impl->d, scalar, SN_HASH_LEN);
    }
    if (ret == 0) {
        ret = mbedtls_ecp_check_privkey(&impl->grp, &impl->d);
    }
    if (ret == 0) {
        ret = mbedtls_ecp_mul(&impl->grp, &impl->q, &impl->d, &impl->grp.G,
                              test_blinding_rng, &impl->rng_state);
    }
    if (ret == 0) {
        ret = mbedtls_ecp_check_pubkey(&impl->grp, &impl->q);
    }
    if (ret != 0) {
        soft_signer_impl_free(impl);
        return ESP_ERR_INVALID_ARG;
    }
    impl->ready = true;
    signer->impl = impl;
    return ESP_OK;
}

esp_err_t sn_soft_signer_get_pubkey(sn_soft_signer_t *signer,
                                    uint8_t out[SN_PUBKEY_LEN])
{
    if (signer == NULL || signer->impl == NULL || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    soft_signer_impl_t *impl = signer->impl;
    if (!impl->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t olen = 0;
    int ret = mbedtls_ecp_point_write_binary(
        &impl->grp, &impl->q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, out,
        SN_PUBKEY_LEN);
    if (ret != 0 || olen != SN_PUBKEY_LEN) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t sn_soft_signer_sign(sn_soft_signer_t *signer,
                              const uint8_t digest[SN_HASH_LEN],
                              uint8_t out_sig[SN_SIG_LEN])
{
    if (signer == NULL || signer->impl == NULL || digest == NULL ||
        out_sig == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    soft_signer_impl_t *impl = signer->impl;
    if (!impl->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    int ret = mbedtls_ecdsa_sign_det_ext(
        &impl->grp, &r, &s, &impl->d, digest, SN_HASH_LEN, MBEDTLS_MD_SHA256,
        test_blinding_rng, &impl->rng_state);
    if (ret != 0) {
        mbedtls_mpi_free(&r);
        mbedtls_mpi_free(&s);
        return ESP_ERR_INVALID_STATE;
    }
    ret = mbedtls_mpi_write_binary(&r, out_sig, SN_SIG_LEN / 2u);
    if (ret == 0) {
        ret = mbedtls_mpi_write_binary(&s, out_sig + SN_SIG_LEN / 2u,
                                       SN_SIG_LEN / 2u);
    }
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return ret == 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void sn_soft_signer_deinit(sn_soft_signer_t *signer)
{
    if (signer == NULL || signer->impl == NULL) {
        return;
    }
    soft_signer_impl_free(signer->impl);
    signer->impl = NULL;
}

static esp_err_t soft_get_pubkey_cb(void *ctx, uint8_t out[SN_PUBKEY_LEN])
{
    return sn_soft_signer_get_pubkey((sn_soft_signer_t *)ctx, out);
}

static esp_err_t soft_sign_cb(void *ctx, const uint8_t digest[SN_HASH_LEN],
                              uint8_t out_sig[SN_SIG_LEN])
{
    return sn_soft_signer_sign((sn_soft_signer_t *)ctx, digest, out_sig);
}

sn_signer_t sn_soft_signer_as_signer(sn_soft_signer_t *signer)
{
    sn_signer_t out = {
        .name = "soft-testkey-ecdsa-p256",
        .ctx = signer,
        .get_pubkey = soft_get_pubkey_cb,
        .sign_digest = soft_sign_cb,
    };
    return out;
}

esp_err_t sn_ds_signer_init(sn_ds_signer_t *signer)
{
    if (signer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    signer->initialized = false;
    /*
     * Deliberate boundary: the ESP32-S3 DS peripheral is RSA-based and needs
     * provisioned, eFuse-backed key material. No eFuse operation is performed
     * here. A future implementation requires a separately reviewed
     * provisioning and use plan.
     */
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t ds_get_pubkey_cb(void *ctx, uint8_t out[SN_PUBKEY_LEN])
{
    (void)ctx;
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t ds_sign_cb(void *ctx, const uint8_t digest[SN_HASH_LEN],
                            uint8_t out_sig[SN_SIG_LEN])
{
    (void)ctx;
    (void)digest;
    (void)out_sig;
    return ESP_ERR_NOT_SUPPORTED;
}

sn_signer_t sn_ds_signer_as_signer(sn_ds_signer_t *signer)
{
    sn_signer_t out = {
        .name = "esp32s3-ds-disabled-boundary",
        .ctx = signer,
        .get_pubkey = ds_get_pubkey_cb,
        .sign_digest = ds_sign_cb,
    };
    return out;
}
