/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if CONFIG_AUDIO_BOARD_WEIXUE_1_8_AMOLED

// I2C
#define ESP_GMF_I2C_SDA_IO_NUM (GPIO_NUM_15)
#define ESP_GMF_I2C_SCL_IO_NUM (GPIO_NUM_14)

// I2S
#define ESP_GMF_I2S_DAC_MCLK_IO_NUM (GPIO_NUM_16)
#define ESP_GMF_I2S_DAC_BCLK_IO_NUM (GPIO_NUM_9)
#define ESP_GMF_I2S_DAC_WS_IO_NUM   (GPIO_NUM_45)
#define ESP_GMF_I2S_DAC_DO_IO_NUM   (GPIO_NUM_8)
#define ESP_GMF_I2S_DAC_DI_IO_NUM   (GPIO_NUM_10)

#define ESP_GMF_I2S_ADC_MCLK_IO_NUM (GPIO_NUM_16)
#define ESP_GMF_I2S_ADC_BCLK_IO_NUM (GPIO_NUM_9)
#define ESP_GMF_I2S_ADC_WS_IO_NUM   (GPIO_NUM_45)
#define ESP_GMF_I2S_ADC_DO_IO_NUM   (GPIO_NUM_8)
#define ESP_GMF_I2S_ADC_DI_IO_NUM   (GPIO_NUM_10)
// PA
#define ESP_GMF_AMP_IO_NUM          (GPIO_NUM_46)

#define CODEC_ES8311_IN_OUT (1)

#endif

#ifdef __cplusplus
}
#endif  /* __cplusplus */
