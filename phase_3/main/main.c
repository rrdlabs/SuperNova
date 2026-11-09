#include <stdio.h>

#include "esp_err.h"
#include "sn_app.h"

void app_main(void)
{
    printf("SN2|event=BOOT|suite=phase3|radios=disabled|sim_ms=0\n");
    fflush(stdout);

    esp_err_t err = sn_app_run();

    printf("SN2|event=APP_EXIT|result=%s|sim_ms=0\n",
           err == ESP_OK ? "PASS" : "FAIL");
    fflush(stdout);
}
