/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
// #include "driver/sdmmc_host.h"
#include "vfs_fat_internal.h"
// #include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_gmf_oal_mem.h"
#include "driver/i2c_master.h"
#include "driver/i2c.h"
#include "esp_gmf_gpio_config.h"
#include "esp_gmf_setup_peripheral.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#ifdef USE_ESP_GMF_ESP_CODEC_DEV_IO
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_io_i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "driver/i2s_pdm.h"
#include "esp_codec_dev_defaults.h"
#endif  /* USE_ESP_GMF_ESP_CODEC_DEV_IO */

// WiFi相关的事件位定义
#define WIFI_CONNECTED_BIT     BIT0
#define WIFI_FAIL_BIT          BIT1
#define WIFI_RECONNECT_RETRIES 30

#ifdef USE_ESP_GMF_ESP_CODEC_DEV_IO
// I2S和Codec相关全局句柄定义
i2s_chan_handle_t            rx_handle   = NULL; // I2S接收通道句柄
const audio_codec_data_if_t *in_data_if  = NULL; // 输入数据接口
const audio_codec_ctrl_if_t *in_ctrl_if  = NULL; // 输入控制接口
const audio_codec_if_t      *in_codec_if = NULL; // 输入Codec接口

i2s_chan_handle_t            tx_handle    = NULL; // I2S发送通道句柄
const audio_codec_data_if_t *out_data_if  = NULL; // 输出数据接口
const audio_codec_ctrl_if_t *out_ctrl_if  = NULL; // 输出控制接口
const audio_codec_if_t      *out_codec_if = NULL; // 输出Codec接口

const audio_codec_gpio_if_t *gpio_if = NULL;      // Codec GPIO接口
#endif  /* USE_ESP_GMF_ESP_CODEC_DEV_IO */

// I2S通道创建模式枚举
typedef enum {
    I2S_CREATE_MODE_TX_ONLY   = 0, // 仅发送
    I2S_CREATE_MODE_RX_ONLY   = 1, // 仅接收
    I2S_CREATE_MODE_TX_AND_RX = 2, // 发送和接收
} i2s_create_mode_t;

static const char        *TAG = "SETUP_PERIPH"; // 日志TAG
i2c_master_bus_handle_t   i2c_handle  = NULL;   // I2C总线句柄

#ifdef USE_ESP_GMF_ESP_CODEC_DEV_IO
/**
 * @brief 初始化I2S发送通道
 * @param aud_info 音频参数信息
 * @return esp_err_t 错误码
 */
static esp_err_t setup_periph_i2s_tx_init(esp_gmf_setup_periph_aud_info *aud_info)
{
    // 配置I2S标准模式参数
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(aud_info->sample_rate), // 时钟配置
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(aud_info->bits_per_sample, aud_info->channel), // 槽配置
        .gpio_cfg = {
            .mclk = ESP_GMF_I2S_DAC_MCLK_IO_NUM, // 主时钟
            .bclk = ESP_GMF_I2S_DAC_BCLK_IO_NUM, // 位时钟
            .ws = ESP_GMF_I2S_DAC_WS_IO_NUM,     // 字选择
            .dout = ESP_GMF_I2S_DAC_DO_IO_NUM,   // 数据输出
            .din = ESP_GMF_I2S_DAC_DI_IO_NUM,    // 数据输入
        },
    };
    // 初始化I2S发送通道
    return i2s_channel_init_std_mode(tx_handle, &std_cfg);
}

/**
 * @brief 初始化I2S接收通道
 * @param aud_info 音频参数信息
 * @return esp_err_t 错误码
 */
static esp_err_t setup_periph_i2s_rx_init(esp_gmf_setup_periph_aud_info *aud_info)
{
    // 配置I2S标准模式参数
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(aud_info->sample_rate), // 时钟配置
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(aud_info->bits_per_sample, aud_info->channel), // 槽配置
        .gpio_cfg = {
            .mclk = ESP_GMF_I2S_ADC_MCLK_IO_NUM, // 主时钟
            .bclk = ESP_GMF_I2S_ADC_BCLK_IO_NUM, // 位时钟
            .ws = ESP_GMF_I2S_ADC_WS_IO_NUM,     // 字选择
            .dout = ESP_GMF_I2S_ADC_DO_IO_NUM,   // 数据输出
            .din = ESP_GMF_I2S_ADC_DI_IO_NUM,    // 数据输入
        },
    };
    // 初始化I2S接收通道
    return i2s_channel_init_std_mode(rx_handle, &std_cfg);
}

/**
 * @brief 创建I2S通道（发送/接收/双工）
 * @param mode 创建模式
 * @param aud_info 音频参数信息
 * @return esp_err_t 错误码
 */
static esp_err_t setup_periph_create_i2s(i2s_create_mode_t mode, esp_gmf_setup_periph_aud_info *aud_info)
{
    // 配置I2S通道参数
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(aud_info->port_num, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // 自动清除FIFO
    esp_err_t ret = ESP_OK;
    if (mode == I2S_CREATE_MODE_TX_ONLY) {
        // 仅创建发送通道
        ret = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return ret;}, "Failed to new I2S tx handle");
        ret = setup_periph_i2s_tx_init(aud_info);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return ret;}, "Failed to initialize I2S tx");
    } else if (mode == I2S_CREATE_MODE_RX_ONLY) {
        // 仅创建接收通道
        ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return ret;}, "Failed to new I2S rx handle");
        ret = setup_periph_i2s_rx_init(aud_info);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return ret;}, "Failed to initialize I2S rx");
    } else {
        // 同时创建发送和接收通道
        ret = i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return ret;}, "Failed to new I2S tx and rx handle");
        ret = setup_periph_i2s_tx_init(aud_info);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return ret;}, "Failed to initialize I2S tx");
        ret = setup_periph_i2s_rx_init(aud_info);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return ret;}, "Failed to initialize I2S rx");
    }
    return ret;
}

/**
 * @brief 创建I2S数据接口
 * @param tx_hd 发送句柄
 * @param rx_hd 接收句柄
 * @return const audio_codec_data_if_t* 数据接口指针
 */
static const audio_codec_data_if_t *setup_periph_new_i2s_data(void *tx_hd, void *rx_hd)
{
    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = rx_hd,
        .tx_handle = tx_hd,
    };
    return audio_codec_new_i2s_data(&i2s_cfg);
}

/**
 * @brief 创建播放用的Codec（输出）
 */
static void setup_periph_new_play_codec()
{
    // 配置I2C控制接口
    audio_codec_i2c_cfg_t i2c_ctrl_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR, .port = 0, .bus_handle = i2c_handle};
    out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_ctrl_cfg);
    gpio_if = audio_codec_new_gpio();
    // 配置ES8311输出Codec
    es8311_codec_cfg_t es8311_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC, // 仅DAC模式
        .ctrl_if = out_ctrl_if,
        .gpio_if = gpio_if,
        .pa_pin = ESP_GMF_AMP_IO_NUM, // 功放引脚
        .use_mclk = false,
    };
    out_codec_if = es8311_codec_new(&es8311_cfg);
}

/**
 * @brief 创建录音用的Codec（输入）
 */
static void setup_periph_new_record_codec()
{
#if CODEC_ES8311_IN_OUT
    // ES8311输入输出一体
    audio_codec_i2c_cfg_t i2c_ctrl_cfg = {.addr = ES8311_CODEC_DEFAULT_ADDR, .port = 0, .bus_handle = i2c_handle};
    in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_ctrl_cfg);
    gpio_if = audio_codec_new_gpio();
    es8311_codec_cfg_t es8311_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH, // 输入输出双工
        .ctrl_if = in_ctrl_if,
        .gpio_if = gpio_if,
        .pa_pin = ESP_GMF_AMP_IO_NUM,
        .use_mclk = false,
    };
    in_codec_if = es8311_codec_new(&es8311_cfg);
#elif CODEC_ES7210_IN_ES8311_OUT
    // ES7210输入，ES8311输出
    audio_codec_i2c_cfg_t i2c_ctrl_cfg = {.addr = ES7210_CODEC_DEFAULT_ADDR, .port = 0, .bus_handle = i2c_handle};
    in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_ctrl_cfg);
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = in_ctrl_if,
        .mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC2 | ES7120_SEL_MIC3, // 选择麦克风
    };
    in_codec_if = es7210_codec_new(&es7210_cfg);
#endif  /* defined CONFIG_IDF_TARGET_ESP32P4 */
}

/**
 * @brief 创建Codec设备（输入或输出）
 * @param dev_type 设备类型（输入/输出）
 * @param aud_info 音频参数信息
 * @return esp_codec_dev_handle_t 设备句柄
 */
static esp_codec_dev_handle_t setup_periph_create_codec_dev(esp_codec_dev_type_t dev_type, esp_gmf_setup_periph_aud_info *aud_info)
{
    // 配置采样信息
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = aud_info->sample_rate,
        .channel = aud_info->channel,
        .bits_per_sample = aud_info->bits_per_sample,
    };
    esp_codec_dev_cfg_t dev_cfg = {0};
    esp_codec_dev_handle_t codec_dev = NULL;
    if (dev_type == ESP_CODEC_DEV_TYPE_OUT) {
        // 创建输出Codec设备
        dev_cfg.codec_if = out_codec_if;
        dev_cfg.data_if = out_data_if;
        dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_OUT;
        codec_dev = esp_codec_dev_new(&dev_cfg);
        esp_codec_dev_set_out_vol(codec_dev, 80.0); // 设置输出音量
        esp_codec_dev_open(codec_dev, &fs);         // 打开设备
    } else {
        // 创建输入Codec设备
        dev_cfg.codec_if = in_codec_if;
        dev_cfg.data_if = in_data_if;
        dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
        codec_dev = esp_codec_dev_new(&dev_cfg);
        esp_codec_dev_set_in_gain(codec_dev, 30.0); // 设置输入增益
        esp_codec_dev_open(codec_dev, &fs);         // 打开设备
    }
    return codec_dev;
}

/**
 * @brief 初始化播放Codec设备
 * @param aud_info 音频参数信息
 * @param play_dev 播放设备句柄指针
 */
static void setup_periph_play_codec(esp_gmf_setup_periph_aud_info *aud_info, void **play_dev)
{
    out_data_if = setup_periph_new_i2s_data(tx_handle, NULL); // 创建I2S数据接口（仅发送）
    setup_periph_new_play_codec();                            // 创建播放Codec
    *play_dev = setup_periph_create_codec_dev(ESP_CODEC_DEV_TYPE_OUT, aud_info); // 创建Codec设备
}

/**
 * @brief 初始化录音Codec设备
 * @param aud_info 音频参数信息
 * @param record_dev 录音设备句柄指针
 */
static void setup_periph_record_codec(esp_gmf_setup_periph_aud_info *aud_info, void **record_dev)
{
    in_data_if = setup_periph_new_i2s_data(NULL, rx_handle); // 创建I2S数据接口（仅接收）
    setup_periph_new_record_codec();                         // 创建录音Codec
    *record_dev = setup_periph_create_codec_dev(ESP_CODEC_DEV_TYPE_IN, aud_info); // 创建Codec设备
}

/**
 * @brief 释放播放Codec相关资源
 * @param play_dev 播放设备句柄
 */
void teardown_periph_play_codec(void *play_dev)
{
    esp_codec_dev_close(play_dev);                // 关闭设备
    esp_codec_dev_delete(play_dev);               // 删除设备
    audio_codec_delete_codec_if(out_codec_if);    // 删除Codec接口
    audio_codec_delete_ctrl_if(out_ctrl_if);      // 删除控制接口
    audio_codec_delete_gpio_if(gpio_if);          // 删除GPIO接口
    audio_codec_delete_data_if(out_data_if);      // 删除数据接口
    i2s_channel_disable(tx_handle);               // 禁用I2S通道
    i2s_del_channel(tx_handle);                   // 删除I2S通道
    tx_handle = NULL;
}

/**
 * @brief 释放录音Codec相关资源
 * @param record_dev 录音设备句柄
 */
void teardown_periph_record_codec(void *record_dev)
{
    esp_codec_dev_close(record_dev);              // 关闭设备
    esp_codec_dev_delete(record_dev);             // 删除设备
    audio_codec_delete_codec_if(in_codec_if);     // 删除Codec接口
    audio_codec_delete_ctrl_if(in_ctrl_if);       // 删除控制接口
    audio_codec_delete_data_if(in_data_if);       // 删除数据接口
    i2s_channel_disable(rx_handle);               // 禁用I2S通道
    i2s_del_channel(rx_handle);                   // 删除I2S通道
    rx_handle = NULL;
}
#endif  /* USE_ESP_GMF_ESP_CODEC_DEV_IO */

/**
 * @brief 初始化I2C主机总线
 * @param port I2C端口号（未使用，保留）
 */
void esp_gmf_setup_periph_i2c(int port)
{
    i2c_master_bus_config_t i2c_config = {
        .i2c_port = 0, // I2C端口号
        .sda_io_num = ESP_GMF_I2C_SDA_IO_NUM, // SDA引脚
        .scl_io_num = ESP_GMF_I2C_SCL_IO_NUM, // SCL引脚
        .clk_source = I2C_CLK_SRC_DEFAULT,    // 时钟源
        .flags.enable_internal_pullup = true, // 使能内部上拉
        .glitch_ignore_cnt = 7,               // 抖动滤波
    };
    i2c_new_master_bus(&i2c_config, &i2c_handle); // 创建I2C主机总线
}

/**
 * @brief 释放I2C主机总线
 * @param port I2C端口号（未使用，保留）
 */
void esp_gmf_teardown_periph_i2c(int port)
{
    if (i2c_handle != NULL) {
        i2c_del_master_bus(i2c_handle); // 删除I2C主机总线
        i2c_handle = NULL;
    }
}

#ifdef USE_ESP_GMF_ESP_CODEC_DEV_IO
/**
 * @brief 初始化Codec设备（播放和录音）
 * @param play_info 播放音频参数
 * @param rec_info 录音音频参数
 * @param play_dev 播放设备句柄指针
 * @param record_dev 录音设备句柄指针
 * @return esp_gmf_err_t 错误码
 */
esp_gmf_err_t esp_gmf_setup_periph_codec(esp_gmf_setup_periph_aud_info *play_info, esp_gmf_setup_periph_aud_info *rec_info,
                                         void **play_dev, void **record_dev)
{
    if ((play_dev != NULL) && (record_dev != NULL)) {
        // 同时需要播放和录音
        if (play_info->port_num == rec_info->port_num) {
            // 播放和录音共用同一个I2S端口
            ESP_GMF_RET_ON_NOT_OK(TAG, setup_periph_create_i2s(I2S_CREATE_MODE_TX_AND_RX, play_info), { return ESP_GMF_ERR_FAIL;}, "Failed to create I2S tx and rx");
        } else {
            // 播放和录音使用不同I2S端口
            ESP_GMF_RET_ON_NOT_OK(TAG, setup_periph_create_i2s(I2S_CREATE_MODE_TX_ONLY, play_info), { return ESP_GMF_ERR_FAIL;}, "Failed to create I2S tx");
            ESP_GMF_RET_ON_NOT_OK(TAG, setup_periph_create_i2s(I2S_CREATE_MODE_RX_ONLY, rec_info), { return ESP_GMF_ERR_FAIL;}, "Failed to create I2S rx");
        }
        setup_periph_play_codec(play_info, play_dev);         // 初始化播放Codec
        setup_periph_record_codec(rec_info, record_dev);      // 初始化录音Codec
    } else if (play_dev != NULL) {
        // 仅需要播放
        ESP_GMF_RET_ON_NOT_OK(TAG, setup_periph_create_i2s(I2S_CREATE_MODE_TX_ONLY, play_info), { return ESP_GMF_ERR_FAIL;}, "Failed to create I2S tx");
        setup_periph_play_codec(play_info, play_dev);
    } else if (record_dev != NULL) {
        // 仅需要录音
        ESP_GMF_RET_ON_NOT_OK(TAG, setup_periph_create_i2s(I2S_CREATE_MODE_RX_ONLY, rec_info), { return ESP_GMF_ERR_FAIL;}, "Failed to create I2S rx");
        setup_periph_record_codec(rec_info, record_dev);
    } else {
        // 无需播放和录音
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

/**
 * @brief 释放Codec设备（播放和录音）
 * @param play_dev 播放设备句柄
 * @param record_dev 录音设备句柄
 */
void esp_gmf_teardown_periph_codec(void *play_dev, void *record_dev)
{
    if (play_dev != NULL) {
        teardown_periph_play_codec(play_dev);   // 释放播放相关资源
    }
    if (record_dev != NULL) {
        teardown_periph_record_codec(record_dev); // 释放录音相关资源
    }
}
#endif  /* USE_ESP_GMF_ESP_CODEC_DEV_IO */
