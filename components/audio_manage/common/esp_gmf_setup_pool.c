/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * 本文件主要用于注册和管理 ESP GMF (Espressif General Media Framework) 的各种音频处理组件、IO 设备、编解码器和音频特效到资源池中。
 * 详细注释已添加，便于理解每一步的作用和流程。
 */

#include <string.h>
#include "esp_log.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_oal_mem.h"

#include "esp_gmf_io_file.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_io_embed_flash.h"
#include "esp_gmf_copier.h"
#include "esp_gmf_setup_pool.h"

#ifdef USE_ESP_GMF_ESP_CODEC_DEV_IO
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_io_i2s_pdm.h"
#include "driver/i2s_pdm.h"
#endif  /* USE_ESP_GMF_ESP_CODEC_DEV_IO */

#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_sonic.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_fade.h"
#include "esp_gmf_mixer.h"
#include "esp_gmf_interleave.h"
#include "esp_gmf_deinterleave.h"
#include "esp_gmf_audio_dec.h"
#include "esp_audio_simple_dec_default.h"

#include "esp_audio_enc_default.h"
#include "esp_gmf_audio_enc.h"

#include "esp_audio_dec_default.h"
#include "esp_audio_dec_reg.h"
#include "esp_http_client.h"
#include "esp_gmf_gpio_config.h"
#include "esp_gmf_audio_helper.h"
#include "driver/i2c_master.h"

static const char *TAG = "ESP_GMF_SETUP_POOL";

// 默认音频采样率、位宽、通道数
#define SETUP_AUDIO_SAMPLE_RATE 16000
#define SETUP_AUDIO_BITS        16
#define SETUP_AUDIO_CHANNELS    1

// HTTP Content-Type 对应的音频格式
static const char *header_type[] = {
    "audio/aac",
    "audio/opus",
    "audio/wav",
};

#ifdef USE_ESP_GMF_ESP_CODEC_DEV_IO
i2s_chan_handle_t pdm_tx_chan = NULL; // PDM TX 通道句柄
#endif  /* USE_ESP_GMF_ESP_CODEC_DEV_IO */

/**
 * @brief HTTP 流事件处理函数
 * 负责处理 HTTP 流的不同事件，包括请求前设置 header、数据写入、请求结束等
 */
static esp_err_t _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    esp_http_client_handle_t http = (esp_http_client_handle_t)msg->http_client;
    char len_buf[16];
    static int total_write = 0; // 记录总写入字节数

    if (msg->event_id == HTTP_STREAM_PRE_REQUEST) {
        // 1. 请求前设置 HTTP header
        ESP_LOGE(TAG, "[ + ] HTTP client HTTP_STREAM_PRE_REQUEST, length=%d, format:%s", msg->buffer_len, (char *)msg->user_data);
        esp_http_client_set_method(http, HTTP_METHOD_POST); // 设置为 POST 方法
        char dat[10] = {0};
        // 设置采样率 header
        snprintf(dat, sizeof(dat), "%d", SETUP_AUDIO_SAMPLE_RATE);
        esp_http_client_set_header(http, "x-audio-sample-rates", dat);
        // 根据 URI 判断音频格式，设置 Content-Type
        esp_audio_type_t fmt = 0;
        esp_gmf_audio_helper_get_audio_type_by_uri((char *)msg->user_data, &fmt);
        if (fmt == ESP_AUDIO_TYPE_AAC) {
            esp_http_client_set_header(http, "Content-Type", header_type[0]);
        } else if (fmt == ESP_AUDIO_TYPE_OPUS) {
            esp_http_client_set_header(http, "Content-Type", header_type[1]);
        } else {
            esp_http_client_set_header(http, "Content-Type", header_type[2]);
        }
        // 设置位宽 header
        memset(dat, 0, sizeof(dat));
        snprintf(dat, sizeof(dat), "%d", SETUP_AUDIO_BITS);
        esp_http_client_set_header(http, "x-audio-bits", dat);
        // 设置通道数 header
        memset(dat, 0, sizeof(dat));
        snprintf(dat, sizeof(dat), "%d", SETUP_AUDIO_CHANNELS);
        esp_http_client_set_header(http, "x-audio-channel", dat);
        total_write = 0; // 初始化写入计数
        return ESP_OK;
    }

    if (msg->event_id == HTTP_STREAM_ON_REQUEST) {
        // 2. 数据写入阶段，采用 chunked 方式
        int wlen = sprintf(len_buf, "%x\r\n", msg->buffer_len); // 写 chunk 长度
        if (esp_http_client_write(http, len_buf, wlen) <= 0) {
            return ESP_FAIL;
        }
        if (esp_http_client_write(http, msg->buffer, msg->buffer_len) <= 0) { // 写 chunk 数据
            return ESP_FAIL;
        }
        if (esp_http_client_write(http, "\r\n", 2) <= 0) { // chunk 结束
            return ESP_FAIL;
        }
        total_write += msg->buffer_len;
        printf("\033[A\33[2K\rTotal bytes written: %d\n", total_write); // 打印总写入字节数
        return msg->buffer_len;
    }

    if (msg->event_id == HTTP_STREAM_POST_REQUEST) {
        // 3. 数据写入结束，写入 chunked 结束标记
        ESP_LOGE(TAG, "[ + ] HTTP client HTTP_STREAM_POST_REQUEST, write end chunked marker");
        if (esp_http_client_write(http, "0\r\n\r\n", 5) <= 0) {
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    if (msg->event_id == HTTP_STREAM_FINISH_REQUEST) {
        // 4. 请求完成，读取服务器响应
        ESP_LOGE(TAG, "[ + ] HTTP client HTTP_STREAM_FINISH_REQUEST");
        char *buf = calloc(1, 64);
        assert(buf);
        int read_len = esp_http_client_read(http, buf, 64);
        if (read_len <= 0) {
            free(buf);
            return ESP_FAIL;
        }
        buf[read_len] = 0; // 字符串结尾
        ESP_LOGI(TAG, "Got HTTP Response = %s", (char *)buf);
        free(buf);
        total_write = 0;
        return ESP_OK;
    }
    return ESP_OK;
}

/**
 * @brief 注册 I2S PDM TX 通道到资源池
 * @param pool      资源池句柄
 * @param sample_rate 采样率
 * @param bits         位宽
 * @param channel      通道数
 */
void pool_register_i2s_pdm_tx(esp_gmf_pool_handle_t pool, uint32_t sample_rate, uint8_t bits, uint8_t channel)
{
#ifdef USE_ESP_GMF_ESP_CODEC_DEV_IO
    // 步骤1：配置 I2S 通道，仅分配 TX 通道
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    tx_chan_cfg.auto_clear = true;
    tx_chan_cfg.dma_desc_num = 10;
    tx_chan_cfg.dma_frame_num = 900;
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &pdm_tx_chan, NULL));

    // 步骤2：配置 PDM TX 模式并初始化 TX 通道
    i2s_pdm_tx_config_t pdm_tx_cfg = {
#if CONFIG_EXAMPLE_PDM_TX_DAC
        .clk_cfg = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(sample_rate), // DAC 时钟配置
        .slot_cfg = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(bits, channel), // DAC 槽配置
#else
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(sample_rate), // 普通 PDM 时钟配置
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(bits, channel), // 普通 PDM 槽配置
#endif  /* CONFIG_EXAMPLE_PDM_TX_DAC */
        .gpio_cfg = {
            .clk = ESP_GMF_I2S_DAC_BCLK_IO_NUM, // 时钟引脚
            .dout = ESP_GMF_I2S_DAC_DO_IO_NUM,  // 数据输出引脚
            .invert_flags = {
                .clk_inv = false, // 时钟不反相
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_pdm_tx_mode(pdm_tx_chan, &pdm_tx_cfg));

    // 步骤3：初始化 GMF PDM IO，并注册到资源池
    i2s_pdm_io_cfg_t pdm_cfg = ESP_GMF_IO_I2S_PDM_CFG_DEFAULT();
    pdm_cfg.dir = ESP_GMF_IO_DIR_WRITER; // 作为写端
    pdm_cfg.pdm_chan = pdm_tx_chan;
    esp_gmf_io_handle_t pdm = NULL;
    esp_gmf_io_i2s_pdm_init(&pdm_cfg, &pdm);
    esp_gmf_pool_register_io(pool, pdm, NULL);
#endif  /* USE_ESP_GMF_ESP_CODEC_DEV_IO */
}

/**
 * @brief 注销 I2S PDM TX 通道
 */
void pool_unregister_i2s_pdm_tx(void)
{
#ifdef USE_ESP_GMF_ESP_CODEC_DEV_IO
    i2s_del_channel(pdm_tx_chan);
#endif  /* USE_ESP_GMF_ESP_CODEC_DEV_IO */
}

/**
 * @brief 注册编解码器设备 IO 到资源池
 * @param pool      资源池句柄
 * @param play_dev  播放设备指针
 * @param record_dev 录音设备指针
 */
void pool_register_codec_dev_io(esp_gmf_pool_handle_t pool, void *play_dev, void *record_dev)
{
#ifdef USE_ESP_GMF_ESP_CODEC_DEV_IO
    // 注册播放设备
    if (play_dev != NULL) {
        codec_dev_io_cfg_t tx_codec_dev_cfg = ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT();
        tx_codec_dev_cfg.dir = ESP_GMF_IO_DIR_WRITER;
        tx_codec_dev_cfg.dev = play_dev;
        tx_codec_dev_cfg.name = "codec_dev_tx";
        esp_gmf_io_handle_t tx_dev = NULL;
        esp_gmf_io_codec_dev_init(&tx_codec_dev_cfg, &tx_dev);
        esp_gmf_pool_register_io(pool, tx_dev, NULL);
    }
    // 注册录音设备
    if (record_dev != NULL) {
        codec_dev_io_cfg_t rx_codec_dev_cfg = ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT();
        rx_codec_dev_cfg.dir = ESP_GMF_IO_DIR_READER;
        rx_codec_dev_cfg.dev = record_dev;
        rx_codec_dev_cfg.name = "codec_dev_rx";
        esp_gmf_io_handle_t rx_dev = NULL;
        esp_gmf_io_codec_dev_init(&rx_codec_dev_cfg, &rx_dev);
        esp_gmf_pool_register_io(pool, rx_dev, NULL);
    }
#endif  /* USE_ESP_GMF_ESP_CODEC_DEV_IO */
}

/**
 * @brief 注册常用 IO 设备到资源池
 * 包括 HTTP、文件、嵌入式 flash、拷贝器等
 */
void pool_register_io(esp_gmf_pool_handle_t pool)
{
    // 注册 HTTP 写端
    http_io_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    esp_gmf_io_handle_t http = NULL;
    http_cfg.dir = ESP_GMF_IO_DIR_WRITER;
    http_cfg.event_handle = _http_stream_event_handle; // 绑定事件处理
    esp_gmf_io_http_init(&http_cfg, &http);
    esp_gmf_pool_register_io(pool, http, NULL);

    // 注册 HTTP 读端
    http_cfg.dir = ESP_GMF_IO_DIR_READER;
    http_cfg.event_handle = NULL;
    http = NULL;
    esp_gmf_io_http_init(&http_cfg, &http);
    esp_gmf_pool_register_io(pool, http, NULL);

    // 注册文件读端
    file_io_cfg_t fs_cfg = FILE_IO_CFG_DEFAULT();
    fs_cfg.dir = ESP_GMF_IO_DIR_READER;
    esp_gmf_io_handle_t fs = NULL;
    esp_gmf_io_file_init(&fs_cfg, &fs);
    esp_gmf_pool_register_io(pool, fs, NULL);

    // 注册文件写端
    fs_cfg.dir = ESP_GMF_IO_DIR_WRITER;
    esp_gmf_io_file_init(&fs_cfg, &fs);
    esp_gmf_pool_register_io(pool, fs, NULL);

    // 注册拷贝器（数据复制元素）
    esp_gmf_copier_cfg_t copier_cfg = {
        .copy_num = 1,
    };
    esp_gmf_element_handle_t copier = NULL;
    esp_gmf_copier_init(&copier_cfg, &copier);
    esp_gmf_pool_register_element(pool, copier, NULL);

    // 注册嵌入式 flash IO
    embed_flash_io_cfg_t flash_cfg = EMBED_FLASH_CFG_DEFAULT();
    esp_gmf_io_handle_t flash = NULL;
    esp_gmf_io_embed_flash_init(&flash_cfg, &flash);
    esp_gmf_pool_register_io(pool, flash, NULL);
}

/**
 * @brief 注册音频编解码器到资源池
 * 包括 AAC 编码器、默认解码器等
 */
void pool_register_audio_codecs(esp_gmf_pool_handle_t pool)
{
    // 注册默认编码器
    esp_audio_enc_register_default();
    esp_audio_enc_config_t es_enc_cfg = DEFAULT_ESP_GMF_AUDIO_ENC_CONFIG();
    esp_aac_enc_config_t aac_enc_cfg = ESP_AAC_ENC_CONFIG_DEFAULT();
    // 配置 AAC 编码参数
    aac_enc_cfg.sample_rate = 16000;
    aac_enc_cfg.channel = 1;
    aac_enc_cfg.bits_per_sample = 16;
    aac_enc_cfg.bitrate = 64000;
    aac_enc_cfg.adts_used = true;
    es_enc_cfg.type = ESP_AUDIO_TYPE_AAC;
    es_enc_cfg.cfg = &aac_enc_cfg;
    es_enc_cfg.cfg_sz = sizeof(esp_aac_enc_config_t);
    esp_gmf_element_handle_t enc_handle = NULL;
    esp_gmf_audio_enc_init(&es_enc_cfg, &enc_handle);
    esp_gmf_pool_register_element(pool, enc_handle, NULL);

    // 注册默认解码器
    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();
    esp_audio_simple_dec_cfg_t es_dec_cfg = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    esp_gmf_element_handle_t es_hd = NULL;
    esp_gmf_audio_dec_init(&es_dec_cfg, &es_hd);
    esp_gmf_pool_register_element(pool, es_hd, NULL);
}

/**
 * @brief 注销音频编解码器
 */
void pool_unregister_audio_codecs()
{
    esp_audio_enc_unregister_default();
    esp_audio_dec_unregister_default();
    esp_audio_simple_dec_unregister_default();
}

/**
 * @brief 注册音频特效处理元素到资源池
 * 包括 ALC、EQ、通道转换、位宽转换、采样率转换、淡入淡出、变速、交错/去交错、混音等
 */
void pool_register_audio_effects(esp_gmf_pool_handle_t pool)
{
    // 自动电平控制（ALC）
    esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
    esp_gmf_element_handle_t alc_hd = NULL;
    esp_gmf_alc_init(&alc_cfg, &alc_hd);
    esp_gmf_pool_register_element(pool, alc_hd, NULL);

    // 均衡器（EQ）
    esp_ae_eq_cfg_t eq_cfg = DEFAULT_ESP_GMF_EQ_CONFIG();
    esp_gmf_element_handle_t eq_hd = NULL;
    esp_gmf_eq_init(&eq_cfg, &eq_hd);
    esp_gmf_pool_register_element(pool, eq_hd, NULL);

    // 通道转换
    esp_ae_ch_cvt_cfg_t ch_cvt_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    esp_gmf_element_handle_t ch_hd = NULL;
    ch_cvt_cfg.dest_ch = 2; // 转换为双通道
    esp_gmf_ch_cvt_init(&ch_cvt_cfg, &ch_hd);
    esp_gmf_pool_register_element(pool, ch_hd, NULL);

    // 位宽转换
    esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    bit_cvt_cfg.dest_bits = 16; // 转换为 16 位
    esp_gmf_element_handle_t bit_hd = NULL;
    esp_gmf_bit_cvt_init(&bit_cvt_cfg, &bit_hd);
    esp_gmf_pool_register_element(pool, bit_hd, NULL);

    // 采样率转换
    esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    rate_cvt_cfg.dest_rate = 48000; // 转换为 48kHz
    esp_gmf_element_handle_t rate_hd = NULL;
    esp_gmf_rate_cvt_init(&rate_cvt_cfg, &rate_hd);
    esp_gmf_pool_register_element(pool, rate_hd, NULL);

    // 淡入淡出
    esp_ae_fade_cfg_t fade_cfg = DEFAULT_ESP_GMF_FADE_CONFIG();
    esp_gmf_element_handle_t fade_hd = NULL;
    esp_gmf_fade_init(&fade_cfg, &fade_hd);
    esp_gmf_pool_register_element(pool, fade_hd, NULL);

    // 变速（Sonic）
    esp_ae_sonic_cfg_t sonic_cfg = DEFAULT_ESP_GMF_SONIC_CONFIG();
    esp_gmf_element_handle_t sonic_hd = NULL;
    esp_gmf_sonic_init(&sonic_cfg, &sonic_hd);
    esp_gmf_pool_register_element(pool, sonic_hd, NULL);

    // 去交错
    esp_gmf_deinterleave_cfg deinterleave_cfg = DEFAULT_ESP_GMF_DEINTERLEAVE_CONFIG();
    esp_gmf_element_handle_t deinterleave_hd = NULL;
    esp_gmf_deinterleave_init(&deinterleave_cfg, &deinterleave_hd);
    esp_gmf_pool_register_element(pool, deinterleave_hd, NULL);

    // 交错
    esp_gmf_interleave_cfg interleave_cfg = DEFAULT_ESP_GMF_INTERLEAVE_CONFIG();
    esp_gmf_element_handle_t interleave_hd = NULL;
    esp_gmf_interleave_init(&interleave_cfg, &interleave_hd);
    esp_gmf_pool_register_element(pool, interleave_hd, NULL);

    // 混音器
    esp_ae_mixer_cfg_t mixer_cfg = {0};
    mixer_cfg.sample_rate = 48000;
    mixer_cfg.channel = 2;
    mixer_cfg.bits_per_sample = 16;
    mixer_cfg.src_num = 2; // 两路输入
    esp_ae_mixer_info_t source_info[2];
    esp_ae_mixer_info_t mixer_info1 = {
        .weight1 = 1,
        .weight2 = 0.5,
        .transit_time = 500,
    };
    esp_ae_mixer_info_t mixer_info2 = {
        .weight1 = 0.0,
        .weight2 = 0.5,
        .transit_time = 500,
    };
    source_info[0] = mixer_info1;
    source_info[1] = mixer_info2;
    mixer_cfg.src_info = source_info;
    esp_gmf_element_handle_t mixer_hd = NULL;
    esp_gmf_mixer_init(&mixer_cfg, &mixer_hd);
    esp_gmf_pool_register_element(pool, mixer_hd, NULL);
}

/**
 * @brief 一键注册所有常用组件到资源池
 * @param pool      资源池句柄
 * @param play_dev  播放设备指针
 * @param codec_dev 编解码器设备指针
 */
void pool_register_all(esp_gmf_pool_handle_t pool, void *play_dev, void *codec_dev)
{
    pool_register_audio_codecs(pool);         // 注册音频编解码器
    pool_register_audio_effects(pool);        // 注册音频特效
    pool_register_io(pool);                   // 注册 IO 设备
    pool_register_codec_dev_io(pool, play_dev, codec_dev); // 注册编解码器 IO
}
