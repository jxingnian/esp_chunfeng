/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#ifndef CONFIG_KEY_PRESS_DIALOG_MODE
#include "esp_gmf_afe.h"
#endif  /* CONFIG_KEY_PRESS_DIALOG_MODE */
#include "esp_gmf_oal_sys.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_oal_mem.h"

#include "iot_button.h"
#include "button_adc.h"
#include "esp_coze_chat.h"
#include "audio_processor.h"

// 定义事件组中的录音标志位
#define BUTTON_REC_READING (1 << 0)

// 日志TAG
static char *TAG = "COZE_CHAT_APP";

// 聊天应用结构体，包含会话句柄、唤醒状态、线程、事件组、队列等
struct coze_chat_t {
    esp_coze_chat_handle_t chat;         // Coze聊天句柄
    bool                   wakeuped;     // 唤醒状态标志
    esp_gmf_oal_thread_t   read_thread;  // 音频读取线程
    esp_gmf_oal_thread_t   btn_thread;   // 按键事件线程
    EventGroupHandle_t     data_evt_group; // 事件组，用于同步录音状态
    QueueHandle_t          btn_evt_q;    // 按键事件队列
};

// 全局聊天应用实例
static struct coze_chat_t coze_chat;

/**
 * @brief 音频事件回调函数
 * 
 * 处理Coze聊天过程中的各种事件，如语音开始、结束、客户自定义数据、字幕事件等
 */
static void audio_event_callback(esp_coze_chat_event_t event, char *data, void *ctx)
{
    if (event == ESP_COZE_CHAT_EVENT_CHAT_SPEECH_STARTED) {
        ESP_LOGI(TAG, "chat start"); // 语音对话开始
    } else if (event == ESP_COZE_CHAT_EVENT_CHAT_SPEECH_STOPED) {
        ESP_LOGI(TAG, "chat stop"); // 语音对话结束
    } else if (event == ESP_COZE_CHAT_EVENT_CHAT_CUSTOMER_DATA) {
        // 客户自定义数据，通常为cjson格式
        ESP_LOGI(TAG, "Customer data: %s", data);
    } else if (event == ESP_COZE_CHAT_EVENT_CHAT_SUBTITLE_EVENT) {
        // 字幕事件
        ESP_LOGI(TAG, "Subtitle data: %s", data);
    }
}

/**
 * @brief 音频数据回调函数
 * 
 * 将接收到的音频数据送入播放队列
 */
static void audio_data_callback(char *data, int len, void *ctx)
{
    // audio_playback_feed_data((uint8_t *)data, len);
}

/**
 * @brief 初始化Coze聊天
 * 
 * 配置聊天参数，注册回调，启动聊天会话
 */
static esp_err_t init_coze_chat()
{
    esp_coze_chat_config_t chat_config = ESP_COZE_CHAT_DEFAULT_CONFIG();
    chat_config.enable_subtitle = true; // 启用字幕
    chat_config.bot_id = CONFIG_COZE_BOT_ID; // 机器人ID
    chat_config.access_token = CONFIG_COZE_ACCESS_TOKEN; // 访问令牌
    chat_config.audio_callback = audio_data_callback; // 音频数据回调
    chat_config.event_callback = audio_event_callback; // 事件回调
#ifdef CONFIG_KEY_PRESS_DIALOG_MODE
    chat_config.websocket_buffer_size = 4096; // WebSocket缓冲区大小
    chat_config.mode = ESP_COZE_CHAT_NORMAL_MODE; // 普通模式
#endif /* CONFIG_KEY_PRESS_DIALOG_MODE */

    esp_err_t ret = ESP_OK;
    // 初始化Coze聊天
    ret = esp_coze_chat_init(&chat_config, &coze_chat.chat);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_coze_chat_init failed, err: %d", ret);
        return ESP_FAIL;
    }
    // 启动聊天
    esp_coze_chat_start(coze_chat.chat);
    return ESP_OK;
}

#ifndef CONFIG_KEY_PRESS_DIALOG_MODE
/**
 * @brief 录音事件回调函数（仅语音唤醒模式下）
 * 
 * 处理AFE（音频前端）事件，如唤醒、VAD、命令检测等
 */
static void recorder_event_callback_fn(void *event, void *ctx)
{
    esp_gmf_afe_evt_t *afe_evt = (esp_gmf_afe_evt_t *)event;
    switch (afe_evt->type) {
        case ESP_GMF_AFE_EVT_WAKEUP_START:
            ESP_LOGI(TAG, "wakeup start"); // 唤醒开始
            if (coze_chat.wakeuped) {
                // 如果已唤醒，先取消上一次会话
                esp_coze_chat_send_audio_cancel(coze_chat.chat);
            }
            // 播放唤醒提示音
            audio_prompt_play("file://spiffs/dingding.wav");
            coze_chat.wakeuped = true;
            break;
        case ESP_GMF_AFE_EVT_WAKEUP_END:
            ESP_LOGI(TAG, "wakeup end"); // 唤醒结束
            coze_chat.wakeuped = false;
            break;
        case ESP_GMF_AFE_EVT_VAD_START:
            ESP_LOGI(TAG, "vad start"); // 语音活动检测开始
            break;
        case ESP_GMF_AFE_EVT_VAD_END:
            ESP_LOGI(TAG, "vad end"); // 语音活动检测结束
            break;
        case ESP_GMF_AFE_EVT_VCMD_DECT_TIMEOUT:
            ESP_LOGI(TAG, "vcmd detect timeout"); // 语音命令检测超时
            break;
        default: {
            // 其他事件（如命令检测到），可根据需要补充处理
            // esp_gmf_afe_vcmd_info_t *info = event->event_data;
            // ESP_LOGW(TAG, "Command %d, phrase_id %d, prob %f, str: %s", sevent->type, info->phrase_id, info->prob, info->str);
        }
    }
}
#endif /* CONFIG_KEY_PRESS_DIALOG_MODE */

#if CONFIG_KEY_PRESS_DIALOG_MODE
/**
 * @brief 按键事件回调函数（仅按键对话模式下）
 * 
 * 将按键事件发送到队列，由专用线程处理
 */
static void button_event_cb(void *arg, void *data)
{
    button_event_t button_event = iot_button_get_event(arg);
    xQueueSend(coze_chat.btn_evt_q, &button_event, 0);
}

/**
 * @brief 按键事件处理线程
 * 
 * 处理按键按下/抬起事件，控制录音状态和会话
 */
static void btn_event_task(void *pv)
{
    button_event_t btn_evt;
    while (1) {
        // 阻塞等待按键事件
        if (xQueueReceive(coze_chat.btn_evt_q, &btn_evt, portMAX_DELAY) == pdTRUE) {
            switch (btn_evt) {
                case BUTTON_PRESS_DOWN:
                    // 按下时取消上一次会话，设置录音标志
                    esp_coze_chat_send_audio_cancel(coze_chat.chat);
                    xEventGroupSetBits(coze_chat.data_evt_group, BUTTON_REC_READING);
                    break;
                case BUTTON_PRESS_UP:
                    // 抬起时清除录音标志，发送录音完成
                    xEventGroupClearBits(coze_chat.data_evt_group, BUTTON_REC_READING);
                    esp_coze_chat_send_audio_complete(coze_chat.chat);
                    break;
                default:
                    break;
            }
        }
    }
}
#endif  /* CONFIG_KEY_PRESS_DIALOG_MODE */

/**
 * @brief 音频数据读取线程
 * 
 * 持续从录音设备读取音频数据，并发送到Coze聊天
 * 支持按键对话、语音唤醒、普通模式
 */
static void audio_data_read_task(void *pv)
{
#if defined CONFIG_KEY_PRESS_DIALOG_MODE
    // 按键对话模式下，每次读取640字节
    uint8_t *data = esp_gmf_oal_calloc(1, 640);
#else
    // 其他模式下，每次读取4096*3字节
    uint8_t *data = esp_gmf_oal_calloc(1, 4096 * 3);
#endif
    int ret = 0;
    while (true) {
#if defined CONFIG_KEY_PRESS_DIALOG_MODE
        // 等待录音标志位被设置（即按键按下）
        xEventGroupWaitBits(coze_chat.data_evt_group, BUTTON_REC_READING, pdFALSE, pdFALSE, portMAX_DELAY);
        // 读取音频数据
        // ret = audio_recorder_read_data(data, 640);
        // 发送音频数据到Coze
        esp_coze_chat_send_audio_data(coze_chat.chat, (char *)data, ret);

#elif defined CONFIG_VOICE_WAKEUP_MODE
        // 语音唤醒模式下读取数据
        // ret = audio_recorder_read_data(data, 4096 * 3);
        if (coze_chat.wakeuped) {
            // 唤醒状态下才发送数据
            esp_coze_chat_send_audio_data(coze_chat.chat, (char *)data, ret);
        }
#else
        // 普通模式下直接读取并发送
        ret = audio_recorder_read_data(data, 4096 * 3);
        esp_coze_chat_send_audio_data(coze_chat.chat, (char *)data, ret);
#endif  /* CONFIG_KEY_PRESS_DIALOG_MODE */
    }
}

/**
 * @brief 打开音频通路
 * 
 * 初始化音频管理、录音、播放等模块
 */
static void audio_pipe_open()
{
    audio_manager_init(); // 初始化音频管理

#if CONFIG_KEY_PRESS_DIALOG_MODE
    // 按键对话模式下打开录音（无事件回调）
    audio_recorder_open(NULL, NULL);
#else
    // 语音唤醒/普通模式下打开提示音和录音
    audio_prompt_open();
    audio_recorder_open(recorder_event_callback_fn, NULL);
#endif /* CONFIG_KEY_PRESS_DIALOG_MODE */
    audio_playback_open(); // 打开音频播放
    audio_playback_run();  // 启动音频播放
}

/**
 * @brief 聊天应用初始化入口
 * 
 * 初始化日志、按键、Coze聊天、音频通路、线程等
 */
esp_err_t coze_chat_app_init(void)
{
    esp_log_level_set("*", ESP_LOG_INFO); // 设置全局日志等级
    coze_chat.wakeuped = false;           // 初始化唤醒状态

#if CONFIG_KEY_PRESS_DIALOG_MODE
    /** ESP32-S3-Korvo2 板载ADC按键初始化 */
    button_handle_t btn = NULL;
    const button_config_t btn_cfg = {0};
    button_adc_config_t btn_adc_cfg = {
        .unit_id = ADC_UNIT_1,      // ADC单元
        .adc_channel = 4,           // ADC通道
        .button_index = 0,          // 按键索引
        .min = 2310,                // ADC最小值
        .max = 2510                 // ADC最大值
    };
    // 创建ADC按键设备
    iot_button_new_adc_device(&btn_cfg, &btn_adc_cfg, &btn);
    // 注册按下/抬起回调
    ESP_ERROR_CHECK(iot_button_register_cb(btn, BUTTON_PRESS_DOWN, NULL, button_event_cb, NULL));
    ESP_ERROR_CHECK(iot_button_register_cb(btn, BUTTON_PRESS_UP, NULL, button_event_cb, NULL));
    // 创建事件组和队列
    coze_chat.data_evt_group = xEventGroupCreate();
    coze_chat.btn_evt_q = xQueueCreate(2, sizeof(button_event_t));
    // 创建按键事件处理线程
    esp_gmf_oal_thread_create(&coze_chat.btn_thread, "btn_event_task", btn_event_task, (void *)NULL, 3096, 12, true, 1);

#endif  /* CONFIG_KEY_PRESS_DIALOG_MODE */

    // 初始化Coze聊天
    init_coze_chat();

    // 打开音频通路
    audio_pipe_open();

    // 创建音频数据读取线程
    esp_gmf_oal_thread_create(&coze_chat.read_thread, "audio_data_read_task", audio_data_read_task, (void *)NULL, 3096, 12, true, 1);

    return ESP_OK;
}
