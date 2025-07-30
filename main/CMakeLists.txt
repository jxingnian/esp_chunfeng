idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ""
                    REQUIRES wifi_manage
                    )

spiffs_create_partition_image(spiffs_data ./spiffs FLASH_IN_PROJECT)