set(sn_transport_requires sn_protocol)

if(CONFIG_SN_ENABLE_ESPNOW)
    list(APPEND sn_transport_requires
        esp_event
        esp_netif
        esp_wifi
        freertos
    )
endif()

idf_component_register(
    SRCS "sn_transport.c"
    INCLUDE_DIRS "include"
    REQUIRES ${sn_transport_requires}
)
