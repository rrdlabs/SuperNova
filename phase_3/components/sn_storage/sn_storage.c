#include "sn_storage.h"

#include <stdlib.h>
#include <string.h>

static bool key_ok(const char *key)
{
    if (key == NULL) {
        return false;
    }
    size_t n = strlen(key);
    return n > 0 && n < SN_STORAGE_MAX_KEY_LEN;
}

static esp_err_t copy_value(uint8_t **dst, size_t *dst_len,
                            const uint8_t *src, size_t len)
{
    if (len > SN_STORAGE_MAX_VALUE_LEN || (len > 0 && src == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t *copy = NULL;
    if (len > 0) {
        copy = malloc(len);
        if (copy == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(copy, src, len);
    }
    *dst = copy;
    *dst_len = len;
    return ESP_OK;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static uint32_t journal_crc(const char *key, uint32_t seq,
                            const uint8_t *value, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    crc = crc32_update(crc, (const uint8_t *)key, strlen(key));
    uint8_t seqbuf[4] = {
        (uint8_t)(seq >> 24),
        (uint8_t)(seq >> 16),
        (uint8_t)(seq >> 8),
        (uint8_t)seq,
    };
    crc = crc32_update(crc, seqbuf, sizeof(seqbuf));
    if (len > 0) {
        crc = crc32_update(crc, value, len);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------------- */
/* Memory backend                                                            */
/* ------------------------------------------------------------------------- */

static sn_storage_entry_t *mem_find(sn_storage_entry_t *entries, size_t count,
                                    const char *key)
{
    for (size_t i = 0; i < count; i++) {
        if (entries[i].used && strcmp(entries[i].key, key) == 0) {
            return &entries[i];
        }
    }
    return NULL;
}

static void mem_clear_staged(sn_storage_mem_t *m)
{
    for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
        free(m->staged[i].value);
        memset(&m->staged[i], 0, sizeof(m->staged[i]));
    }
    m->staged_count = 0;
}

esp_err_t sn_storage_mem_init(sn_storage_mem_t *m)
{
    if (m == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(m, 0, sizeof(*m));
    return ESP_OK;
}

void sn_storage_mem_deinit(sn_storage_mem_t *m)
{
    if (m == NULL) {
        return;
    }
    for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
        free(m->committed[i].value);
        free(m->staged[i].value);
    }
    memset(m, 0, sizeof(*m));
}

static esp_err_t mem_get(void *ctx, const char *key, uint8_t *buf, size_t cap,
                         size_t *out_len)
{
    sn_storage_mem_t *m = ctx;
    if (m == NULL || !key_ok(key) || (cap > 0 && buf == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    sn_storage_entry_t *entry =
        mem_find(m->committed, SN_STORAGE_MAX_ENTRIES, key);
    if (entry == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (cap < entry->len) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (entry->len > 0) {
        memcpy(buf, entry->value, entry->len);
    }
    if (out_len != NULL) {
        *out_len = entry->len;
    }
    return ESP_OK;
}

static esp_err_t mem_put(void *ctx, const char *key, const uint8_t *buf,
                         size_t len)
{
    sn_storage_mem_t *m = ctx;
    if (m == NULL || !key_ok(key) || len > SN_STORAGE_MAX_VALUE_LEN ||
        (len > 0 && buf == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    sn_storage_entry_t *entry =
        mem_find(m->staged, SN_STORAGE_MAX_ENTRIES, key);
    if (entry == NULL) {
        for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
            if (!m->staged[i].used) {
                entry = &m->staged[i];
                memset(entry, 0, sizeof(*entry));
                memcpy(entry->key, key, strlen(key) + 1u);
                entry->used = true;
                m->staged_count++;
                break;
            }
        }
    }
    if (entry == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t *copy = NULL;
    size_t copy_len = 0;
    esp_err_t err = copy_value(&copy, &copy_len, buf, len);
    if (err != ESP_OK) {
        return err;
    }
    free(entry->value);
    entry->value = copy;
    entry->len = copy_len;
    return ESP_OK;
}

static esp_err_t mem_commit(void *ctx)
{
    sn_storage_mem_t *m = ctx;
    if (m == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t new_keys = 0;
    for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
        if (!m->staged[i].used) {
            continue;
        }
        if (mem_find(m->committed, SN_STORAGE_MAX_ENTRIES,
                     m->staged[i].key) == NULL) {
            new_keys++;
        }
    }
    if (m->committed_count + new_keys > SN_STORAGE_MAX_ENTRIES) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
        sn_storage_entry_t *staged = &m->staged[i];
        if (!staged->used) {
            continue;
        }
        sn_storage_entry_t *entry =
            mem_find(m->committed, SN_STORAGE_MAX_ENTRIES, staged->key);
        if (entry == NULL) {
            entry = &m->committed[m->committed_count++];
            memset(entry, 0, sizeof(*entry));
            memcpy(entry->key, staged->key, strlen(staged->key) + 1u);
            entry->used = true;
        }
        free(entry->value);
        entry->value = staged->value;
        entry->len = staged->len;
        staged->value = NULL;
        memset(staged, 0, sizeof(*staged));
    }
    m->staged_count = 0;
    return ESP_OK;
}

static esp_err_t mem_rollback(void *ctx)
{
    sn_storage_mem_t *m = ctx;
    if (m == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    mem_clear_staged(m);
    return ESP_OK;
}

sn_storage_backend_t sn_storage_mem_as_backend(sn_storage_mem_t *m)
{
    sn_storage_backend_t backend = {
        .name = "memory",
        .ctx = m,
        .get = mem_get,
        .put = mem_put,
        .commit = mem_commit,
        .rollback = mem_rollback,
    };
    return backend;
}

/* ------------------------------------------------------------------------- */
/* Journal backend                                                           */
/* ------------------------------------------------------------------------- */

static sn_storage_journal_entry_t *journal_find(sn_storage_journal_t *j,
                                                const char *key)
{
    for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
        if (j->entries[i].used && strcmp(j->entries[i].key, key) == 0) {
            return &j->entries[i];
        }
    }
    return NULL;
}

static sn_storage_journal_staged_t *journal_find_staged(
    sn_storage_journal_t *j, const char *key)
{
    for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
        if (j->staged[i].used && strcmp(j->staged[i].key, key) == 0) {
            return &j->staged[i];
        }
    }
    return NULL;
}

static void journal_clear_staged(sn_storage_journal_t *j)
{
    for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
        free(j->staged[i].value);
        memset(&j->staged[i], 0, sizeof(j->staged[i]));
    }
    j->staged_count = 0;
}

esp_err_t sn_storage_journal_init(sn_storage_journal_t *j)
{
    if (j == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(j, 0, sizeof(*j));
    j->next_seq = 1;
    return ESP_OK;
}

void sn_storage_journal_deinit(sn_storage_journal_t *j)
{
    if (j == NULL) {
        return;
    }
    for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
        for (size_t s = 0; s < SN_STORAGE_JOURNAL_SLOTS; s++) {
            free(j->entries[i].slots[s].value);
        }
        free(j->staged[i].value);
    }
    memset(j, 0, sizeof(*j));
}

void sn_storage_journal_set_crash(sn_storage_journal_t *j,
                                  sn_storage_crash_t mode)
{
    if (j != NULL) {
        j->crash_mode = mode;
    }
}

static esp_err_t journal_get(void *ctx, const char *key, uint8_t *buf,
                             size_t cap, size_t *out_len)
{
    sn_storage_journal_t *j = ctx;
    if (j == NULL || !key_ok(key) || (cap > 0 && buf == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    sn_storage_journal_entry_t *entry = journal_find(j, key);
    if (entry == NULL || !entry->has_committed) {
        return ESP_ERR_NOT_FOUND;
    }
    const sn_storage_journal_slot_t *slot = &entry->slots[entry->active];
    if (!slot->valid || slot->crc != journal_crc(key, slot->seq, slot->value,
                                                slot->len)) {
        const sn_storage_journal_slot_t *other =
            &entry->slots[entry->active ^ 1u];
        if (!other->valid ||
            other->crc != journal_crc(key, other->seq, other->value,
                                      other->len)) {
            return ESP_ERR_INVALID_CRC;
        }
        slot = other;
    }
    if (cap < slot->len) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (slot->len > 0) {
        memcpy(buf, slot->value, slot->len);
    }
    if (out_len != NULL) {
        *out_len = slot->len;
    }
    return ESP_OK;
}

static esp_err_t journal_put(void *ctx, const char *key, const uint8_t *buf,
                             size_t len)
{
    sn_storage_journal_t *j = ctx;
    if (j == NULL || !key_ok(key) || len > SN_STORAGE_MAX_VALUE_LEN ||
        (len > 0 && buf == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    sn_storage_journal_staged_t *staged = journal_find_staged(j, key);
    if (staged == NULL) {
        for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
            if (!j->staged[i].used) {
                staged = &j->staged[i];
                memset(staged, 0, sizeof(*staged));
                memcpy(staged->key, key, strlen(key) + 1u);
                staged->used = true;
                j->staged_count++;
                break;
            }
        }
    }
    if (staged == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t *copy = NULL;
    size_t copy_len = 0;
    esp_err_t err = copy_value(&copy, &copy_len, buf, len);
    if (err != ESP_OK) {
        return err;
    }
    free(staged->value);
    staged->value = copy;
    staged->len = copy_len;
    return ESP_OK;
}

static esp_err_t journal_commit(void *ctx)
{
    sn_storage_journal_t *j = ctx;
    if (j == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t new_keys = 0;
    for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
        if (j->staged[i].used && journal_find(j, j->staged[i].key) == NULL) {
            new_keys++;
        }
    }
    if (j->entry_count + new_keys > SN_STORAGE_MAX_ENTRIES) {
        return ESP_ERR_NO_MEM;
    }

    sn_storage_journal_entry_t *touched[SN_STORAGE_MAX_ENTRIES];
    uint8_t targets[SN_STORAGE_MAX_ENTRIES];
    size_t touched_count = 0;

    for (size_t i = 0; i < SN_STORAGE_MAX_ENTRIES; i++) {
        sn_storage_journal_staged_t *staged = &j->staged[i];
        if (!staged->used) {
            continue;
        }
        sn_storage_journal_entry_t *entry = journal_find(j, staged->key);
        if (entry == NULL) {
            for (size_t e = 0; e < SN_STORAGE_MAX_ENTRIES; e++) {
                if (!j->entries[e].used) {
                    entry = &j->entries[e];
                    memset(entry, 0, sizeof(*entry));
                    memcpy(entry->key, staged->key,
                           strlen(staged->key) + 1u);
                    entry->used = true;
                    j->entry_count++;
                    break;
                }
            }
        }
        if (entry == NULL) {
            return ESP_ERR_NO_MEM;
        }
        uint8_t target = entry->has_committed ? (entry->active ^ 1u) : 0u;
        sn_storage_journal_slot_t *slot = &entry->slots[target];
        uint8_t *copy = NULL;
        size_t copy_len = 0;
        esp_err_t err = copy_value(&copy, &copy_len, staged->value,
                                   staged->len);
        if (err != ESP_OK) {
            return err;
        }
        free(slot->value);
        slot->value = copy;
        slot->len = copy_len;
        slot->seq = j->next_seq++;
        if (j->next_seq == 0) {
            j->next_seq = 1;
        }
        slot->crc = journal_crc(entry->key, slot->seq, slot->value, slot->len);
        slot->valid = true;
        touched[touched_count] = entry;
        targets[touched_count] = target;
        touched_count++;
    }

    if (j->crash_mode == SN_STORAGE_CRASH_BEFORE_COMMIT) {
        /* The inactive slot was written, but the active index did not move. */
        j->crash_mode = SN_STORAGE_CRASH_NONE;
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < touched_count; i++) {
        touched[i]->active = targets[i];
        touched[i]->has_committed = true;
    }

    if (j->crash_mode == SN_STORAGE_CRASH_AFTER_COMMIT) {
        /* The commit became durable before the simulated power loss. */
        j->crash_mode = SN_STORAGE_CRASH_NONE;
        journal_clear_staged(j);
        return ESP_ERR_INVALID_STATE;
    }

    journal_clear_staged(j);
    return ESP_OK;
}

static esp_err_t journal_rollback(void *ctx)
{
    sn_storage_journal_t *j = ctx;
    if (j == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    journal_clear_staged(j);
    return ESP_OK;
}

esp_err_t sn_storage_journal_corrupt_slot(sn_storage_journal_t *j,
                                          const char *key, unsigned slot)
{
    if (j == NULL || !key_ok(key) || slot >= SN_STORAGE_JOURNAL_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }
    sn_storage_journal_entry_t *entry = journal_find(j, key);
    if (entry == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    sn_storage_journal_slot_t *s = &entry->slots[slot];
    if (s->len > 0 && s->value != NULL) {
        s->value[0] ^= 0xA5u;
    } else {
        s->crc ^= 0xA5A5A5A5u;
    }
    s->valid = false;
    return ESP_OK;
}

sn_storage_backend_t sn_storage_journal_as_backend(sn_storage_journal_t *j)
{
    sn_storage_backend_t backend = {
        .name = "journal",
        .ctx = j,
        .get = journal_get,
        .put = journal_put,
        .commit = journal_commit,
        .rollback = journal_rollback,
    };
    return backend;
}

/* ------------------------------------------------------------------------- */
/* Generic wrappers                                                          */
/* ------------------------------------------------------------------------- */

esp_err_t sn_storage_get(const sn_storage_backend_t *backend, const char *key,
                         uint8_t *buf, size_t cap, size_t *out_len)
{
    if (backend == NULL || backend->get == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return backend->get(backend->ctx, key, buf, cap, out_len);
}

esp_err_t sn_storage_put(const sn_storage_backend_t *backend, const char *key,
                         const uint8_t *buf, size_t len)
{
    if (backend == NULL || backend->put == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return backend->put(backend->ctx, key, buf, len);
}

esp_err_t sn_storage_commit(const sn_storage_backend_t *backend)
{
    if (backend == NULL || backend->commit == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return backend->commit(backend->ctx);
}

esp_err_t sn_storage_rollback(const sn_storage_backend_t *backend)
{
    if (backend == NULL || backend->rollback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return backend->rollback(backend->ctx);
}
