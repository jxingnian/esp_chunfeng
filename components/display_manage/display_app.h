#pragma once

#include "esp_log.h"
#include "esp_timer.h"          // ESP32定时器
#include "esp_lcd_panel_io.h"           // LCD面板IO接口，提供与LCD通信的底层API
#include "esp_lcd_panel_vendor.h"       // LCD厂商特定功能扩展
#include "esp_lcd_panel_ops.h"          // LCD面板操作接口，包含常用操作函数
#include "lvgl.h"                       // LVGL图形库主头文件
#include "esp_lcd_sh8601.h"             // SH8601 LCD驱动芯片支持
#include "esp_lcd_touch_ft5x06.h"       // FT5x06触摸屏驱动支持
#include "esp_io_expander_tca9554.h"    // TCA9554 IO扩展器驱动
#include "ui.h"                         // 用户界面相关头文件

// 定义LCD和触摸屏所用的主机编号
#define LCD_HOST SPI2_HOST              // LCD使用的SPI主机编号
#define TOUCH_HOST I2C_NUM_1            // 触摸屏使用的I2C主机编号

// 根据LVGL配置的颜色深度，设置LCD像素位深
#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL (24)          // 32位色深时，实际使用24位
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL (16)          // 16位色深
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 以下为LCD硬件相关配置，请根据实际硬件参数进行修改
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1         // 背光开启时的GPIO电平
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  // 背光关闭时的GPIO电平
#define EXAMPLE_PIN_NUM_LCD_CS (GPIO_NUM_12)    // LCD片选引脚
#define EXAMPLE_PIN_NUM_LCD_PCLK (GPIO_NUM_11)  // LCD时钟引脚
#define EXAMPLE_PIN_NUM_LCD_DATA0 (GPIO_NUM_4)  // LCD数据线0
#define EXAMPLE_PIN_NUM_LCD_DATA1 (GPIO_NUM_5)  // LCD数据线1
#define EXAMPLE_PIN_NUM_LCD_DATA2 (GPIO_NUM_6)  // LCD数据线2
#define EXAMPLE_PIN_NUM_LCD_DATA3 (GPIO_NUM_7)  // LCD数据线3
#define EXAMPLE_PIN_NUM_LCD_RST (-1)            // LCD复位引脚，-1表示未连接
#define EXAMPLE_PIN_NUM_BK_LIGHT (-1)           // 背光控制引脚，-1表示未连接

// LCD分辨率配置
#define EXAMPLE_LCD_H_RES 448                   // LCD水平分辨率
#define EXAMPLE_LCD_V_RES 368                   // LCD垂直分辨率

#define EXAMPLE_USE_TOUCH 1                     // 是否启用触摸功能（1启用，0禁用）

#if EXAMPLE_USE_TOUCH
#define EXAMPLE_PIN_NUM_TOUCH_SCL (GPIO_NUM_14) // 触摸屏I2C时钟线
#define EXAMPLE_PIN_NUM_TOUCH_SDA (GPIO_NUM_15) // 触摸屏I2C数据线
#define EXAMPLE_PIN_NUM_TOUCH_RST (-1)          // 触摸屏复位引脚，-1表示未连接
#define EXAMPLE_PIN_NUM_TOUCH_INT (GPIO_NUM_21) // 触摸屏中断引脚

#endif

// LVGL相关缓冲区和任务配置
#define EXAMPLE_LVGL_BUF_HEIGHT (EXAMPLE_LCD_V_RES / 16)   // LVGL绘制缓冲区高度（行数）
#define EXAMPLE_LVGL_TICK_PERIOD_MS 2                     // LVGL定时器周期（毫秒）
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500                // LVGL任务最大延迟（毫秒）
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1                  // LVGL任务最小延迟（毫秒）
#define EXAMPLE_LVGL_TASK_STACK_SIZE (4 * 1024)           // LVGL任务栈大小（字节）
#define EXAMPLE_LVGL_TASK_PRIORITY 2                      // LVGL任务优先级


#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
* @brief  Initialize the Coze chat application
*
* @return
*       - ESP_OK  On success
*       - Other   Appropriate esp_err_t error code on failure
*/
void display_app_init(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
