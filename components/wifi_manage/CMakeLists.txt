idf_component_register(SRC_DIRS "./"
                       INCLUDE_DIRS "./"
                       REQUIRES esp_wifi esp_http_server nvs_flash json spiffs)

spiffs_create_partition_image(spiffs_data ./spiffs FLASH_IN_PROJECT)