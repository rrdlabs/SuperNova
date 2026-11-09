#include <inttypes.h>

#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
#include "phase1_tests.h"

static const char *TAG = "phase1";

void app_main(void)
{
    esp_chip_info_t chip_info = {0};
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "SuperNova Phase 1 bench firmware");
    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip model enum=%d revision=%d cores=%d features=0x%" PRIx32,
             chip_info.model, chip_info.revision, chip_info.cores, chip_info.features);
    ESP_LOGI(TAG, "Free heap before tests: %" PRIu32 " bytes",
             esp_get_free_heap_size());

    phase1_run_tests();
}
