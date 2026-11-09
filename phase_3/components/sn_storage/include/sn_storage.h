#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SN_STORAGE_MAX_KEY_LEN 32u
#define SN_STORAGE_MAX_VALUE_LEN 8192u
#define SN_STORAGE_MAX_ENTRIES 8u
#define SN_STORAGE_JOURNAL_SLOTS 2u

typedef struct {
    const char *name;
    void *ctx;
    esp_err_t (*get)(void *ctx, const char *key, uint8_t *buf, size_t cap,
                     size_t *out_len);
    esp_err_t (*put)(void *ctx, const char *key, const uint8_t *buf,
                     size_t len);
    esp_err_t (*commit)(void *ctx);
    esp_err_t (*rollback)(void *ctx);
} sn_storage_backend_t;

typedef struct {
    char key[SN_STORAGE_MAX_KEY_LEN];
    uint8_t *value;
    size_t len;
    bool used;
} sn_storage_entry_t;

typedef struct {
    sn_storage_entry_t committed[SN_STORAGE_MAX_ENTRIES];
    sn_storage_entry_t staged[SN_STORAGE_MAX_ENTRIES];
    size_t committed_count;
    size_t staged_count;
} sn_storage_mem_t;

typedef struct {
    uint8_t *value;
    size_t len;
    uint32_t seq;
    uint32_t crc;
    bool valid;
} sn_storage_journal_slot_t;

typedef struct {
    char key[SN_STORAGE_MAX_KEY_LEN];
    sn_storage_journal_slot_t slots[SN_STORAGE_JOURNAL_SLOTS];
    uint8_t active;
    bool used;
    bool has_committed;
} sn_storage_journal_entry_t;

typedef struct {
    char key[SN_STORAGE_MAX_KEY_LEN];
    uint8_t *value;
    size_t len;
    bool used;
} sn_storage_journal_staged_t;

typedef enum {
    SN_STORAGE_CRASH_NONE = 0,
    SN_STORAGE_CRASH_BEFORE_COMMIT = 1,
    SN_STORAGE_CRASH_AFTER_COMMIT = 2,
} sn_storage_crash_t;

typedef struct {
    sn_storage_journal_entry_t entries[SN_STORAGE_MAX_ENTRIES];
    size_t entry_count;
    sn_storage_journal_staged_t staged[SN_STORAGE_MAX_ENTRIES];
    size_t staged_count;
    sn_storage_crash_t crash_mode;
    uint32_t next_seq;
} sn_storage_journal_t;

esp_err_t sn_storage_mem_init(sn_storage_mem_t *m);
void sn_storage_mem_deinit(sn_storage_mem_t *m);
sn_storage_backend_t sn_storage_mem_as_backend(sn_storage_mem_t *m);

esp_err_t sn_storage_journal_init(sn_storage_journal_t *j);
void sn_storage_journal_deinit(sn_storage_journal_t *j);
sn_storage_backend_t sn_storage_journal_as_backend(sn_storage_journal_t *j);
void sn_storage_journal_set_crash(sn_storage_journal_t *j,
                                  sn_storage_crash_t mode);
esp_err_t sn_storage_journal_corrupt_slot(sn_storage_journal_t *j,
                                          const char *key, unsigned slot);

esp_err_t sn_storage_get(const sn_storage_backend_t *backend, const char *key,
                         uint8_t *buf, size_t cap, size_t *out_len);
esp_err_t sn_storage_put(const sn_storage_backend_t *backend, const char *key,
                         const uint8_t *buf, size_t len);
esp_err_t sn_storage_commit(const sn_storage_backend_t *backend);
esp_err_t sn_storage_rollback(const sn_storage_backend_t *backend);

#ifdef __cplusplus
}
#endif
