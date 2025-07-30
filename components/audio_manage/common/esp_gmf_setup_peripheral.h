/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-07-30 11:59:32
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-07-30 12:57:25
 * @FilePath: \esp_chunfeng\components\audio_manage\common\esp_gmf_setup_peripheral.h
 * @Description: GMF音频框架外设设置头文件
 *              提供WiFi、I2C、音频编解码器等外设的初始化和清理函数
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_gmf_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  GMF音频元素音频信息结构体
 *         用于配置音频编解码器的基本参数
 */
typedef struct {
    uint32_t sample_rate;      /*!< 音频采样率 (Hz)，如16000、44100、48000等 */
    uint8_t  channel;          /*!< 音频通道数，1=单声道，2=立体声 */
    uint8_t  bits_per_sample;  /*!< 音频位深度，如16位、24位、32位 */
    uint8_t  port_num;         /*!< I2S端口号，用于指定使用哪个I2S接口 */
} esp_gmf_setup_periph_aud_info;

/**
 * @brief  设置I2C外设
 *         初始化指定端口的I2C接口，用于与音频编解码器芯片通信
 *
 * @param[in]  port  I2C端口号，如0、1等
 */
void esp_gmf_setup_periph_i2c(int port);

/**
 * @brief  清理I2C外设
 *         停止指定端口的I2C接口，释放相关资源
 *
 * @param[in]  port  I2C端口号，与设置时使用的端口号一致
 */
void esp_gmf_teardown_periph_i2c(int port);

#ifdef USE_ESP_GMF_ESP_CODEC_DEV_IO

/**
 * @brief  设置录音和播放编解码器
 *         初始化音频编解码器芯片，配置录音和播放的音频参数
 *         这是音频处理的核心函数，负责硬件音频接口的配置
 *
 * @param[in]   play_info   播放编解码器的音频信息结构体指针
 *                          包含播放时的采样率、通道数、位深度等参数
 * @param[in]   rec_info    录音编解码器的音频信息结构体指针
 *                          包含录音时的采样率、通道数、位深度等参数
 * @param[out]  play_dev    播放编解码器设备句柄指针
 *                          成功初始化后，通过此指针返回设备句柄
 * @param[out]  record_dev  录音编解码器设备句柄指针
 *                          成功初始化后，通过此指针返回设备句柄
 *
 * @return
 *       - ESP_GMF_ERR_OK    成功
 *       - ESP_GMF_ERR_FAIL  设置播放和录音编解码器失败
 *                           可能的原因：硬件连接问题、参数配置错误、驱动初始化失败等
 */
esp_gmf_err_t esp_gmf_setup_periph_codec(esp_gmf_setup_periph_aud_info *play_info, esp_gmf_setup_periph_aud_info *rec_info,
                                         void **play_dev, void **record_dev);

/**
 * @brief  清理播放和录音编解码器
 *         停止音频编解码器，释放相关资源
 *         通常在音频应用关闭或重新配置音频参数时调用
 *
 * @param[in]  play_dev    播放编解码器设备句柄，由esp_gmf_setup_periph_codec返回
 * @param[in]  record_dev  录音编解码器设备句柄，由esp_gmf_setup_periph_codec返回
 */
void esp_gmf_teardown_periph_codec(void *play_dev, void *record_dev);

#endif  /* USE_ESP_GMF_ESP_CODEC_DEV_IO */
#ifdef __cplusplus
}
#endif  /* __cplusplus */
