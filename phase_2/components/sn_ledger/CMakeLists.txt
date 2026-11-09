idf_component_register(
    SRCS "sn_ledger.c"
    INCLUDE_DIRS "include"
    REQUIRES sn_protocol sn_crypto
)
