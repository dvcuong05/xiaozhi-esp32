if(D1_RADIO_DIAGNOSTIC)
    idf_component_register(SRCS "radio_diagnostic.c"
                           PRIV_REQUIRES bt nvs_flash esp_wifi esp_event esp_netif)
    return()
endif()

idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
                       PRIV_REQUIRES bt nvs_flash esp_timer esp_wifi esp_event esp_netif esp_http_server
                                     esp_driver_gpio esp_driver_uart esp_driver_i2s)
