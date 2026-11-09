/*
 * Phase 3 empirical benchmark.
 *
 * The measured workload is the hardened transaction verification and UTXO
 * processing path: canonical decode, canonical txid/signing-digest hashing,
 * ECDSA P-256 signature verification, atomic UTXO state transition, and the
 * resulting state-root hash. Each iteration starts from a fresh genesis
 * ledger so the same signed transaction is valid every time.
 *
 * Only the core call is inside the cycle window. Fixture setup, ledger reset,
 * UART output, and the periodic yield are outside the measured region.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sn_app_internal.h"

#ifndef SN_BENCHMARK_ITERATIONS
#define SN_BENCHMARK_ITERATIONS 1000
#endif

#define BENCHMARK_WARMUP 16
#define BENCHMARK_YIELD_MASK 0x1Fu

static uint32_t cycle_delta(uint32_t start, uint32_t end)
{
    if (end >= start) {
        return end - start;
    }
    /* 32-bit CCOUNT rollover. */
    return (UINT32_MAX - start) + end + 1u;
}

static esp_err_t reset_bench_ledger(sn_ledger_t *ledger,
                                    const sn_genesis_output_t *genesis)
{
    uint8_t tip[SN_HASH_LEN] = {0};
    return sn_ledger_genesis(ledger, genesis, 1, 0, tip);
}

void run_empirical_benchmark(void)
{
    printf("SN3|event=BENCH_START|workload=utxo_verify_and_process|"
           "iterations=%d|cpu_mhz=%d\n",
           SN_BENCHMARK_ITERATIONS, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    fflush(stdout);

    const sn_test_key_t *key = sn_app_test_key(0);
    if (key == NULL) {
        printf("SN3|event=BENCH_ERROR|reason=key_unavailable\n");
        fflush(stdout);
        return;
    }

    sn_test_input_t input;
    sn_test_output_t output = {.value = 1000, .key_index = 0};
    sn_tx_t tx;
    esp_err_t err = sn_test_genesis_input(&input, 0, 0);
    if (err == ESP_OK) {
        err = sn_test_build_tx(&tx, &input, 1, &output, 1);
    }
    if (err == ESP_OK) {
        err = sn_test_sign_tx(&tx);
    }
    if (err != ESP_OK) {
        printf("SN3|event=BENCH_ERROR|reason=fixture_build|code=%s\n",
               esp_err_to_name(err));
        fflush(stdout);
        return;
    }

    sn_genesis_output_t genesis = {.value = 1000};
    memcpy(genesis.owner_pubkey, key->pubkey, SN_PUBKEY_LEN);

    sn_ledger_t ledger;
    sn_ledger_report_t report;
    for (int i = 0; i < BENCHMARK_WARMUP; i++) {
        err = reset_bench_ledger(&ledger, &genesis);
        if (err == ESP_OK) {
            err = sn_ledger_apply_tx(&ledger, &tx, &report);
        }
        if (err != ESP_OK) {
            printf("SN3|event=BENCH_ERROR|reason=warmup|code=%s\n",
                   esp_err_to_name(err));
            fflush(stdout);
            return;
        }
    }

    uint64_t total_cycles = 0;
    uint32_t min_cycles = UINT32_MAX;
    uint32_t max_cycles = 0;
    uint32_t failure_count = 0;
    uint32_t first_failure_cycles = 0;

    for (int i = 0; i < SN_BENCHMARK_ITERATIONS; i++) {
        /* Reset is deliberately outside the measured cycle window. */
        err = reset_bench_ledger(&ledger, &genesis);
        if (err != ESP_OK) {
            failure_count++;
            if (failure_count == 1) {
                printf("SN3|event=BENCH_FAIL|iter=%d|phase=reset|code=%s\n",
                       i, esp_err_to_name(err));
            }
            continue;
        }

        uint32_t start_ccount = (uint32_t)esp_cpu_get_cycle_count();
        esp_err_t apply_err = sn_ledger_apply_tx(&ledger, &tx, &report);
        uint32_t end_ccount = (uint32_t)esp_cpu_get_cycle_count();
        uint32_t delta_cycles = cycle_delta(start_ccount, end_ccount);

        if (apply_err != ESP_OK) {
            failure_count++;
            if (failure_count == 1) {
                first_failure_cycles = delta_cycles;
                printf("SN3|event=BENCH_FAIL|iter=%d|phase=apply|code=%s|"
                       "cycles=%lu\n",
                       i, esp_err_to_name(apply_err),
                       (unsigned long)delta_cycles);
            }
            continue;
        }

        total_cycles += delta_cycles;
        if (delta_cycles < min_cycles) {
            min_cycles = delta_cycles;
        }
        if (delta_cycles > max_cycles) {
            max_cycles = delta_cycles;
        }

        printf("---RAW_SAMPLE::ITER=%d,CYCLES=%lu---\n", i,
               (unsigned long)delta_cycles);
        fflush(stdout);

        /* Keep the idle task alive during long emulator runs; not measured. */
        if ((i & BENCHMARK_YIELD_MASK) == 0) {
            vTaskDelay(1);
        }
    }

    uint64_t measured = (uint64_t)SN_BENCHMARK_ITERATIONS - failure_count;
    if (measured == 0) {
        printf("SN3|event=BENCH_ERROR|reason=all_failed|first_cycles=%lu\n",
               (unsigned long)first_failure_cycles);
        fflush(stdout);
        return;
    }

    uint64_t avg_cycles = total_cycles / measured;
    double avg_us =
        (double)avg_cycles / (double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    double avg_ms = avg_us / 1000.0;

    printf("\n================ METRIC SUMMARY LOG ================\n");
    printf("SN3|event=BENCH_SUMMARY|workload=utxo_verify_and_process|"
           "iterations=%d|failures=%u|avg_cycles=%" PRIu64
           "|avg_us=%.3f|avg_ms=%.6f|min_cycles=%lu|max_cycles=%lu|"
           "cpu_mhz=%d\n",
           SN_BENCHMARK_ITERATIONS, (unsigned)failure_count, avg_cycles,
           avg_us, avg_ms, (unsigned long)min_cycles,
           (unsigned long)max_cycles, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    printf("Target Workload : canonical UTXO verification and processing\n");
    printf("Total Runs      : %d | Failures: %u\n", SN_BENCHMARK_ITERATIONS,
           (unsigned)failure_count);
    printf("Avg Latency     : %" PRIu64 " cycles (~%.4f ms)\n", avg_cycles,
           avg_ms);
    printf("Min / Max       : %lu / %lu cycles\n",
           (unsigned long)min_cycles, (unsigned long)max_cycles);
    printf("====================================================\n\n");
    fflush(stdout);
}
