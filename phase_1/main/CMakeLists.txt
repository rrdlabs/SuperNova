idf_component_register(
    SRCS "phase_1.c" "phase1_tests.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bootloader_support esp_hw_support esp_timer esp_system log mbedtls
)
