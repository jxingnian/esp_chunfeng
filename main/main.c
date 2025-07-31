/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-07-29 14:09:31
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-07-31 17:11:18
 * @FilePath: \esp_chunfeng\main\main.c
 * @Description: 
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */

#include <stdio.h>
#include "nvs_flash.h"
#include "esp_err.h"
#include <esp_log.h>
#include "wifi_manager.h"
#include "coze_chat_app.h"
#include "display_app.h"

static const char *TAG = "ESP ChunFeng";

static esp_err_t spiffs_filesystem_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs_data",
        .max_files = 5,
        .format_if_mount_failed = false
      };
  
      esp_err_t ret = esp_vfs_spiffs_register(&conf);
  
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    spiffs_filesystem_init();
    
    display_app_init();
    
    ESP_ERROR_CHECK(wifi_init_softap());

    vTaskDelay(3000 / portTICK_PERIOD_MS);

    coze_chat_app_init();
}


