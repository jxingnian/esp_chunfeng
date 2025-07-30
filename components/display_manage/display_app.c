/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-07-30 13:45:50
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-07-30 15:24:54
 * @FilePath: \esp_chunfeng\components\display_manage\display_app.c
 * @Description: 
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "display_app.h"

// 日志TAG，用于ESP_LOG系列宏输出日志
static const char *TAG = "DISPLAY_APP";

i2c_master_bus_handle_t i2c_bus_handle = NULL;
esp_lcd_touch_handle_t tp = NULL;               // 触摸屏控制句柄

// LCD初始化命令序列，具体命令请参考SH8601芯片手册
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},                  // 退出睡眠模式，延时120ms
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},              // 设置垂直滚动区域
    {0x35, (uint8_t[]){0x00}, 1, 0},                    // 设置撕裂效应
    {0x53, (uint8_t[]){0x20}, 1, 10},                   // 写入控制显示
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},  // 设置列地址
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},  // 设置页地址
    {0x51, (uint8_t[]){0x00}, 1, 10},                   // 写入显示亮度
    {0x29, (uint8_t[]){0x00}, 0, 10},                   // 开启显示
    {0x51, (uint8_t[]){0xFF}, 1, 0},                    // 设置最大亮度
};

/**
 * @brief LCD面板刷新完成通知回调
 * 
 * 该回调在LCD面板完成一次刷新后被调用，通知LVGL刷新已完成。
 * 
 * @param panel_io LCD面板IO句柄
 * @param edata 事件数据（未使用）
 * @param user_ctx 用户上下文（此处为LVGL显示驱动指针）
 * @return true 继续处理事件
 * @return false 停止处理事件
 */
static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx; // 获取LVGL显示驱动
    lv_disp_flush_ready(disp_driver);                       // 通知LVGL刷新完成
    return false;                                           // 不再继续处理事件
}

/**
 * @brief LVGL刷新回调函数
 * 
 * 该函数由LVGL在需要刷新屏幕某一区域时调用，将LVGL缓冲区的数据写入LCD。
 * 
 * @param drv LVGL显示驱动
 * @param area 刷新区域
 * @param color_map 区域对应的颜色数据
 */
static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data; // 获取LCD面板句柄
    const int offsetx1 = area->x1; // 刷新区域左上角X坐标
    const int offsetx2 = area->x2; // 刷新区域右下角X坐标
    const int offsety1 = area->y1; // 刷新区域左上角Y坐标
    const int offsety2 = area->y2; // 刷新区域右下角Y坐标

#if LCD_BIT_PER_PIXEL == 24
    // 24位色彩需要特殊处理RGB格式
    uint8_t *to = (uint8_t *)color_map; // 指向颜色数据的指针
    uint8_t temp = 0;
    uint16_t pixel_num = (offsetx2 - offsetx1 + 1) * (offsety2 - offsety1 + 1); // 计算像素数量

    // 处理第一个像素（特殊格式）
    temp = color_map[0].ch.blue;
    *to++ = color_map[0].ch.red;
    *to++ = color_map[0].ch.green;
    *to++ = temp;
    // 处理剩余像素
    for (int i = 1; i < pixel_num; i++)
    {
        *to++ = color_map[i].ch.red;
        *to++ = color_map[i].ch.green;
        *to++ = color_map[i].ch.blue;
    }
#endif

    // 将缓冲区内容复制到LCD指定区域
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

/**
 * @brief LVGL显示驱动更新回调
 * 
 * 当LVGL旋转屏幕时，调用此回调以同步LCD的显示方向和镜像设置。
 * 
 * @param drv LVGL显示驱动
 */
static void example_lvgl_update_cb(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data; // 获取LCD面板句柄

    switch (drv->rotated)
    {
    case LV_DISP_ROT_NONE:
        // 无旋转，正常显示
        esp_lcd_panel_swap_xy(panel_handle, false);      // 不交换XY
        esp_lcd_panel_mirror(panel_handle, true, false); // 水平镜像
        break;
    case LV_DISP_ROT_90:
        // 顺时针旋转90度
        esp_lcd_panel_swap_xy(panel_handle, true);       // 交换XY
        esp_lcd_panel_mirror(panel_handle, true, true);  // 水平+垂直镜像
        break;
    case LV_DISP_ROT_180:
        // 旋转180度
        esp_lcd_panel_swap_xy(panel_handle, false);      // 不交换XY
        esp_lcd_panel_mirror(panel_handle, false, true); // 垂直镜像
        break;
    case LV_DISP_ROT_270:
        // 旋转270度
        esp_lcd_panel_swap_xy(panel_handle, true);       // 交换XY
        esp_lcd_panel_mirror(panel_handle, false, false);// 不镜像
        break;
    }
}

/**
 * @brief LVGL坐标舍入回调函数
 * 
 * LVGL在刷新区域时会调用此函数，将区域坐标对齐到2的倍数，提升DMA效率。
 * 
 * @param disp_drv LVGL显示驱动
 * @param area 刷新区域
 */
void example_lvgl_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // 起始坐标向下舍入到偶数
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    // 结束坐标向上舍入到奇数
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

#if EXAMPLE_USE_TOUCH
/**
 * @brief LVGL触摸输入回调函数
 * 
 * LVGL通过此回调获取触摸屏的输入数据。
 * 
 * @param drv LVGL输入设备驱动
 * @param data LVGL输入数据结构体
 */
static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)drv->user_data; // 获取触摸屏句柄
    assert(tp);

    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t tp_cnt = 0;
    /* 从触摸控制器读取数据到内存 */
    esp_lcd_touch_read_data(tp);
    /* 获取触摸点坐标 */
    bool tp_pressed = esp_lcd_touch_get_coordinates(tp, &tp_x, &tp_y, NULL, &tp_cnt, 1);
    if (tp_pressed && tp_cnt > 0)
    {
        data->point.x = tp_x;                  // 设置X坐标
        data->point.y = tp_y;                  // 设置Y坐标
        data->state = LV_INDEV_STATE_PRESSED;  // 设置为按下状态
        ESP_LOGD(TAG, "Touch position: %d,%d", tp_x, tp_y);
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED; // 设置为释放状态
    }
}
#endif

/**
 * @brief LVGL定时器回调，增加LVGL内部时基
 * 
 * @param arg 用户参数（未使用）
 */
static void example_increase_lvgl_tick(void *arg)
{
    /* 通知LVGL已过去EXAMPLE_LVGL_TICK_PERIOD_MS毫秒 */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

/**
 * @brief LVGL任务处理函数
 * 
 * 该任务循环调用LVGL的事件处理函数，确保LVGL正常运行。
 * 
 * @param arg 用户参数（未使用）
 */
static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    while (1)
    {
        // // 获取互斥锁，保证LVGL线程安全
        // if (example_lvgl_lock(-1))
        // {
            task_delay_ms = lv_timer_handler(); // 处理LVGL任务，返回下次调用延迟
            // example_lvgl_unlock();              // 释放互斥锁
        // }
        // 限制延迟范围
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS)
        {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        }
        else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS)
        {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms)); // 延迟一段时间后继续
    }
}

/**
 * @brief 显示应用初始化函数
 * 
 * 初始化I2C、SPI、LCD、触摸屏、LVGL等所有相关外设和软件资源。
 */
void display_app_init(void)
{
    // LVGL绘制缓冲区和显示驱动结构体（静态分配，生命周期为全局）
    static lv_disp_draw_buf_t disp_buf; // LVGL绘制缓冲区结构体
    static lv_disp_drv_t disp_drv;      // LVGL显示驱动结构体

    // 1. 初始化I2C总线（用于触摸屏和IO扩展器）
    ESP_LOGI(TAG, "Initialize I2C bus");
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,           // 默认时钟源
        .i2c_port = TOUCH_HOST,                      // I2C端口号
        .sda_io_num = EXAMPLE_PIN_NUM_TOUCH_SDA,     // SDA引脚
        .scl_io_num = EXAMPLE_PIN_NUM_TOUCH_SCL,     // SCL引脚
        .glitch_ignore_cnt = 7,                      // 抖动滤波
        .flags.enable_internal_pullup = true,        // 启用内部上拉
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle)); // 创建I2C主机

    // 2. 初始化IO扩展器（TCA9554，用于控制外部IO）
    esp_io_expander_handle_t io_expander = NULL;
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(
        i2c_bus_handle, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander));

    // 配置IO扩展器的引脚方向为输出，并初始化电平为0
    esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0, 0);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_1, 0);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_2, 0);
    vTaskDelay(pdMS_TO_TICKS(200)); // 延时200ms，确保IO稳定
    // 设置IO扩展器引脚为高电平
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_0, 1);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_1, 1);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_2, 1);
    // i2c_handle0 = i2c_bus_handle; // 记录I2C句柄

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    // 3. 配置背光控制引脚为输出，并关闭背光
    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif

    // 4. 初始化SPI总线（用于LCD）
    ESP_LOGI(TAG, "Initialize SPI bus");
    // 配置QSPI模式的SPI总线
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        EXAMPLE_PIN_NUM_LCD_PCLK,
        EXAMPLE_PIN_NUM_LCD_DATA0,
        EXAMPLE_PIN_NUM_LCD_DATA1,
        EXAMPLE_PIN_NUM_LCD_DATA2,
        EXAMPLE_PIN_NUM_LCD_DATA3,
        EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * LCD_BIT_PER_PIXEL / 8
    );
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO)); // 初始化SPI总线

    // 5. 安装LCD面板IO驱动
    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    // 配置LCD面板IO（QSPI模式），并指定刷新完成回调
    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(
        EXAMPLE_PIN_NUM_LCD_CS,
        example_notify_lvgl_flush_ready,
        &disp_drv
    );
    // SH8601厂商特定配置
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds, // 初始化命令序列
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]), // 命令数量
        .flags = {
            .use_qspi_interface = 1, // 启用QSPI接口
        },
    };
    // 创建LCD面板IO句柄
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    // 6. 安装LCD面板驱动
    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,         // 复位引脚
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,        // RGB顺序
        .bits_per_pixel = LCD_BIT_PER_PIXEL,               // 像素位深
        .vendor_config = &vendor_config,                   // 厂商特定配置
    };
    ESP_LOGI(TAG, "Install SH8601 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle)); // 创建LCD面板句柄
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));     // 复位LCD
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));      // 初始化LCD
    // 可选：在打开显示或背光前刷新预定义图案
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true)); // 打开LCD显示

#if EXAMPLE_USE_TOUCH
    // 7. 初始化触摸屏
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    // 配置I2C触摸屏IO
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    tp_io_config.scl_speed_hz = 16 * 1000; // 设置I2C时钟频率为16kHz
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_handle, &tp_io_config, &tp_io_handle));

    // 配置触摸屏参数
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,                // 触摸屏X轴最大值
        .y_max = EXAMPLE_LCD_V_RES,                // 触摸屏Y轴最大值
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST, // 复位引脚
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT, // 中断引脚
        .levels = {
            .reset = 0,                            // 复位电平
            .interrupt = 0,                        // 中断电平
        },
        .flags = {
            .swap_xy = 0,                          // 是否交换XY
            .mirror_x = 0,                         // 是否镜像X
            .mirror_y = 0,                         // 是否镜像Y
        },
    };

    ESP_LOGI(TAG, "Initialize touch controller");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp)); // 创建触摸屏句柄
#endif

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    // 8. 打开LCD背光
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
#endif

    // 9. 初始化LVGL图形库
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // 分配LVGL绘制缓冲区
    // lv_color_t *buf1 = (lv_color_t *)malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t));
    // assert(buf1);
    static lv_color_t buf1[EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT];  // 单缓冲区
    // lv_color_t *buf2 = (lv_color_t *)malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t));
    // assert(buf2);
    // 初始化LVGL绘制缓冲区
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT);

    // 10. 注册LVGL显示驱动
    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);                   // 初始化显示驱动结构体
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;          // 设置水平分辨率
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;          // 设置垂直分辨率
    disp_drv.flush_cb = example_lvgl_flush_cb;     // 刷新回调
    disp_drv.rounder_cb = example_lvgl_rounder_cb; // 坐标舍入回调
    disp_drv.drv_update_cb = example_lvgl_update_cb; // 驱动更新回调
    disp_drv.draw_buf = &disp_buf;                 // 绘制缓冲区
    disp_drv.user_data = panel_handle;             // 用户数据（LCD面板句柄）
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv); // 注册显示驱动

    // 11. 安装LVGL定时器（周期性调用lv_tick_inc）
    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,   // 定时器回调
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer)); // 创建定时器
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000)); // 启动定时器

#if EXAMPLE_USE_TOUCH
    // 12. 注册LVGL触摸输入设备
    static lv_indev_drv_t indev_drv;              // 输入设备驱动结构体
    lv_indev_drv_init(&indev_drv);                // 初始化输入设备驱动
    indev_drv.type = LV_INDEV_TYPE_POINTER;       // 类型为指针（触摸）
    indev_drv.disp = disp;                        // 关联显示驱动
    indev_drv.read_cb = example_lvgl_touch_cb;    // 读取回调
    indev_drv.user_data = tp;                     // 用户数据（触摸屏句柄）
    lv_indev_drv_register(&indev_drv);            // 注册输入设备驱动
#endif

    // 13. 初始化UI事件
    ui_init();

    // 14. 创建LVGL任务，负责LVGL主循环
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);
}