/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-07-29 14:55:50
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-07-29 16:03:40
 * @FilePath: \esp_chunfeng\components\wifi_manage\http_server.h
 * @Description: 
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */

#pragma once

#include "esp_err.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_spiffs.h>
#include <esp_system.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "http_server.h"
#include <sys/stat.h>
#include "nvs_flash.h"
#include "lwip/ip4_addr.h"
#include "wifi_manager.h"

#define FILE_PATH_MAX (128 + 128)
#define CHUNK_SIZE    (4096)

#ifdef __cplusplus
extern "C" {
#endif

// 启动Web服务器
esp_err_t start_webserver(void);

// 停止Web服务器
esp_err_t stop_webserver(void);

#ifdef __cplusplus
}
#endif
