file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c ${CMAKE_SOURCE_DIR}/src/*.cpp)
idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES espressif__esp_websocket_client m5stack__m5gfx esp_http_client espressif__cjson esp_driver_i2c esp_wifi esp_netif nvs_flash esp_http_server esp_lcd
                                     esp_driver_gpio esp_driver_spi esp_mm esp_hw_support app_update
                                     esp_timer lwip esp_system)
