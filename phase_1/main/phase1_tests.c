#include "phase1_tests.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"

static const char *TAG = "phase1_tests";

enum {
    SHA256_BENCHMARK_ITERATIONS = 1000,
    SHA256_WARMUP_ITERATIONS = 32,
    SHA256_INPUT_SIZE = 1024,
    RNG_SAMPLE_SIZE = 64,
};

static const uint8_t s_sha256_abc_expected[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
};

static uint8_t s_benchmark_input[SHA256_INPUT_SIZE];
static uint64_t s_sha256_latencies_us[SHA256_BENCHMARK_ITERATIONS];

static int compare_u64(const void *left, const void *right)
{
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static bool run_sha256_known_answer_test(void)
{
    static const uint8_t input[] = "abc";
    uint8_t digest[sizeof(s_sha256_abc_expected)];

    const int result = mbedtls_sha256(input, sizeof(input) - 1, digest, 0);
    const bool passed = result == 0 &&
                        memcmp(digest, s_sha256_abc_expected, sizeof(digest)) == 0;

    ESP_LOGI(TAG, "SHA-256 known-answer test: %s",
             passed ? "PASS" : "FAIL");
    if (!passed) {
        ESP_LOGE(TAG, "SHA-256 returned %d or did not match the published test vector",
                 result);
    }
    return passed;
}

static void run_rng_api_smoke_test(void)
{
    uint8_t sample[RNG_SAMPLE_SIZE];

    /*
     * This project does not start Wi-Fi, Bluetooth, ADC, or I2S. Keep the
     * temporary entropy source bounded and disable it before adding any of
     * those subsystems.
     */
    bootloader_random_enable();
    const int64_t start_us = esp_timer_get_time();
    esp_fill_random(sample, sizeof(sample));
    const int64_t elapsed_us = esp_timer_get_time() - start_us;
    bootloader_random_disable();

    ESP_LOGI(TAG,
             "RNG API exercised: bytes=%u elapsed_us=%" PRId64
             " (sample omitted; entropy quality not assessed)",
             (unsigned)sizeof(sample), elapsed_us);
}

static bool run_sha256_benchmark(void)
{
    uint8_t digest[32];
    uint64_t total_us = 0;

    for (size_t i = 0; i < SHA256_WARMUP_ITERATIONS; ++i) {
        if (mbedtls_sha256(s_benchmark_input, sizeof(s_benchmark_input),
                           digest, 0) != 0) {
            ESP_LOGE(TAG, "SHA-256 warm-up failed at iteration %u", (unsigned)i);
            return false;
        }
    }

    for (size_t i = 0; i < SHA256_BENCHMARK_ITERATIONS; ++i) {
        const int64_t start_us = esp_timer_get_time();
        const int result = mbedtls_sha256(s_benchmark_input,
                                          sizeof(s_benchmark_input), digest, 0);
        const int64_t elapsed_us = esp_timer_get_time() - start_us;

        if (result != 0 || elapsed_us < 0) {
            ESP_LOGE(TAG, "SHA-256 benchmark failed at iteration %u (err=%d)",
                     (unsigned)i, result);
            return false;
        }
        s_sha256_latencies_us[i] = (uint64_t)elapsed_us;
        total_us += (uint64_t)elapsed_us;
    }

    qsort(s_sha256_latencies_us, SHA256_BENCHMARK_ITERATIONS,
          sizeof(s_sha256_latencies_us[0]), compare_u64);

    const uint64_t median_us =
        (s_sha256_latencies_us[SHA256_BENCHMARK_ITERATIONS / 2 - 1] +
         s_sha256_latencies_us[SHA256_BENCHMARK_ITERATIONS / 2]) / 2;
    const size_t p95_index =
        (SHA256_BENCHMARK_ITERATIONS * 95 + 99) / 100 - 1;
    const size_t p99_index =
        (SHA256_BENCHMARK_ITERATIONS * 99 + 99) / 100 - 1;

    ESP_LOGI(TAG,
             "SHA-256 1KiB benchmark: n=%u mean_us=%" PRIu64
             " median_us=%" PRIu64 " p95_us=%" PRIu64 " p99_us=%" PRIu64,
             SHA256_BENCHMARK_ITERATIONS,
             total_us / SHA256_BENCHMARK_ITERATIONS,
             median_us,
             s_sha256_latencies_us[p95_index],
             s_sha256_latencies_us[p99_index]);
    return true;
}

void phase1_run_tests(void)
{
    const bool hash_vector_passed = run_sha256_known_answer_test();
    run_rng_api_smoke_test();
    const bool benchmark_passed = run_sha256_benchmark();

    ESP_LOGI(TAG, "Deterministic checks: %s",
             hash_vector_passed && benchmark_passed ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "Free heap after tests: %" PRIu32 " bytes",
             esp_get_free_heap_size());
}
