/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-07-29 14:55:50
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-07-29 15:46:00
 * @FilePath: \esp_chunfeng\components\wifi_manage\wifi_manager.h
 * @Description: 
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
 
#pragma once

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "http_server.h"

// WiFi相关宏定义
#define ESP_WIFI_CHANNEL   1     // WiFi信道
#define EXAMPLE_MAX_STA_CONN       4     // 最大连接数
#define ESP_AP_SSID      CONFIG_ESP_AP_SSID        // AP名称
#define ESP_AP_PASS      CONFIG_ESP_AP_PASSWORD    // AP密码
#define MAX_RETRY_COUNT 5
#define DEFAULT_SCAN_LIST_SIZE 10  // 默认扫描列表大小

#ifdef __cplusplus
extern "C" {
#endif

// WiFi初始化函数
esp_err_t wifi_init_softap(void);
// wifi连接次数重置
esp_err_t wifi_reset_connection_retry(void);
// WiFi扫描函数
esp_err_t wifi_scan_networks(wifi_ap_record_t **ap_records, uint16_t *ap_count);

#ifdef __cplusplus
}
#endif
