#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Run the deterministic Phase 2 diagnostic suite. Returns ESP_OK only when
 * every test passes. The suite never logs private key material.
 */
esp_err_t sn_app_run(void);

#ifdef __cplusplus
}
#endif
