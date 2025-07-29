/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-07-29 14:09:31
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-07-29 16:53:28
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

void app_main(void)
{
    printf("Hello ChunFeng!\n");

    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    
    ESP_ERROR_CHECK(wifi_init_softap());
    

}
