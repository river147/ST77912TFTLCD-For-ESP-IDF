#include <inttypes.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_commands.h"

#include "esp_lcd_st77912.h"

static const char *TAG = "simple_lcd_test";

#define TEST_LCD_HOST               SPI2_HOST
#define TEST_LCD_H_RES              (240)
#define TEST_LCD_V_RES              (240)
#define TEST_LCD_BIT_PER_PIXEL      (16)

#define TEST_PIN_NUM_LCD_CS         (GPIO_NUM_10)
#define TEST_PIN_NUM_LCD_PCLK       (GPIO_NUM_12)
#define TEST_PIN_NUM_LCD_DATA0      (GPIO_NUM_11)
#define TEST_PIN_NUM_LCD_RST        (GPIO_NUM_9)
#define TEST_PIN_NUM_LCD_DC         (GPIO_NUM_14)
#define TEST_PIN_NUM_LCD_BL         (GPIO_NUM_21)

void app_main(void)
{
    ESP_LOGI(TAG, "开始简单LCD测试...");
    
    ESP_LOGI(TAG, "初始化SPI总线");
    spi_bus_config_t buscfg = {
        .sclk_io_num = TEST_PIN_NUM_LCD_PCLK,
        .mosi_io_num = TEST_PIN_NUM_LCD_DATA0,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TEST_LCD_H_RES * TEST_LCD_V_RES * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TEST_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "安装面板IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = TEST_PIN_NUM_LCD_DC,
        .cs_gpio_num = TEST_PIN_NUM_LCD_CS,
        .pclk_hz = 30 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .flags = {
            .dc_low_on_data = 0,
            .octal_mode = 0,
            .sio_mode = 0,
            .lsb_first = 0,
            .cs_high_active = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TEST_LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "安装ST77912 LCD驱动");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TEST_PIN_NUM_LCD_RST,
        .rgb_ele_order = 0,
        .bits_per_pixel = 16,
        .vendor_config = NULL,
        .flags = {
            .reset_active_high = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77912(io_handle, &panel_config, &panel_handle));
    
    ESP_LOGI(TAG, "复位LCD");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "初始化LCD");
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "打开显示");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "初始化背光");
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << TEST_PIN_NUM_LCD_BL,
    };
    gpio_config(&io_conf);
    gpio_set_level(TEST_PIN_NUM_LCD_BL, 1);
    ESP_LOGI(TAG, "背光已打开");

    ESP_LOGI(TAG, "开始循环绘制测试图案");
    
    
    uint16_t *color_buffer = (uint16_t *)heap_caps_malloc(TEST_LCD_H_RES * TEST_LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (color_buffer == NULL) {
        ESP_LOGE(TAG, "无法分配颜色缓冲区");
        return;
    }
    
    int pattern_count = 0;
    
    while (1) {
        pattern_count++;
        ESP_LOGI(TAG, "第%d轮测试图案", pattern_count);
        
        ESP_LOGI(TAG, "绘制全屏红色");
        for (int i = 0; i < TEST_LCD_H_RES * TEST_LCD_V_RES; i++) {
            color_buffer[i] = 0xF800;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, TEST_LCD_H_RES, TEST_LCD_V_RES, color_buffer));
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "绘制全屏绿色");
        for (int i = 0; i < TEST_LCD_H_RES * TEST_LCD_V_RES; i++) {
            color_buffer[i] = 0x07E0;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, TEST_LCD_H_RES, TEST_LCD_V_RES, color_buffer));
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "绘制全屏蓝色");
        for (int i = 0; i < TEST_LCD_H_RES * TEST_LCD_V_RES; i++) {
            color_buffer[i] = 0x001F;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, TEST_LCD_H_RES, TEST_LCD_V_RES, color_buffer));
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "绘制全屏白色");
        for (int i = 0; i < TEST_LCD_H_RES * TEST_LCD_V_RES; i++) {
            color_buffer[i] = 0xFFFF;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, TEST_LCD_H_RES, TEST_LCD_V_RES, color_buffer));
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "绘制彩色条纹");
        int stripe_height = TEST_LCD_V_RES / 8;
        
        // 一次性生成所有条纹的颜色数据
        for (int y = 0; y < TEST_LCD_V_RES; y++) {
            uint16_t color;
            if (y < stripe_height) {
                color = 0xF800;
            } else if (y < stripe_height * 2) {
                color = 0x07E0;
            } else if (y < stripe_height * 3) {
                color = 0x001F;
            } else if (y < stripe_height * 4) {
                color = 0xFFE0;
            } else if (y < stripe_height * 5) {
                color = 0xF81F;
            } else if (y < stripe_height * 6) {
                color = 0x07FF;
            } else if (y < stripe_height * 7) {
                color = 0xFD20;
            } else {
                color = 0xFFFF;
            }
            
            for (int x = 0; x < TEST_LCD_H_RES; x++) {
                color_buffer[y * TEST_LCD_H_RES + x] = color;
            }
        }
        
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, TEST_LCD_H_RES, TEST_LCD_V_RES, color_buffer));
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "绘制渐变彩虹");
        for (int y = 0; y < TEST_LCD_V_RES; y++) {
            float ratio = (float)y / TEST_LCD_V_RES;
            uint16_t color;
            
            if (ratio < 0.17) {
                float t = ratio / 0.17;
                color = (uint16_t)(0xF800 + (uint16_t)(t * 0x0020));
            } else if (ratio < 0.33) {
                float t = (ratio - 0.17) / 0.16;
                color = (uint16_t)(0xFA00 + (uint16_t)(t * 0x05E0));
            } else if (ratio < 0.50) {
                float t = (ratio - 0.33) / 0.17;
                color = (uint16_t)(0xFFE0 - (uint16_t)(t * 0x01E0));
            } else if (ratio < 0.67) {
                float t = (ratio - 0.50) / 0.17;
                color = (uint16_t)(0x07E0 + (uint16_t)(t * 0x001F));
            } else if (ratio < 0.83) {
                float t = (ratio - 0.67) / 0.16;
                color = (uint16_t)(0x07FF - (uint16_t)(t * 0x07E0));
            } else {
                float t = (ratio - 0.83) / 0.17;
                color = (uint16_t)(0x001F + (uint16_t)(t * 0xF7E0));
            }
            
            for (int x = 0; x < TEST_LCD_H_RES; x++) {
                color_buffer[y * TEST_LCD_H_RES + x] = color;
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, TEST_LCD_H_RES, TEST_LCD_V_RES, color_buffer));
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "绘制棋盘格图案");
        int square_size = 20;
        for (int y = 0; y < TEST_LCD_V_RES; y++) {
            for (int x = 0; x < TEST_LCD_H_RES; x++) {
                int square_x = x / square_size;
                int square_y = y / square_size;
                uint16_t color = ((square_x + square_y) % 2 == 0) ? 0x0000 : 0xFFFF;
                color_buffer[y * TEST_LCD_H_RES + x] = color;
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, TEST_LCD_H_RES, TEST_LCD_V_RES, color_buffer));
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "绘制同心圆");
        int center_x = TEST_LCD_H_RES / 2;
        int center_y = TEST_LCD_V_RES / 2;
        for (int y = 0; y < TEST_LCD_V_RES; y++) {
            for (int x = 0; x < TEST_LCD_H_RES; x++) {
                int dx = x - center_x;
                int dy = y - center_y;
                int distance = (int)sqrt(dx * dx + dy * dy);
                
                uint16_t color;
                if (distance < 20) {
                    color = 0xF800;
                } else if (distance < 40) {
                    color = 0x07E0;
                } else if (distance < 60) {
                    color = 0x001F;
                } else if (distance < 80) {
                    color = 0xFFE0;
                } else if (distance < 100) {
                    color = 0xF81F;
                } else {
                    color = 0x0000;
                }
                color_buffer[y * TEST_LCD_H_RES + x] = color;
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, TEST_LCD_H_RES, TEST_LCD_V_RES, color_buffer));
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "绘制螺旋图案");
        center_x = TEST_LCD_H_RES / 2;
        center_y = TEST_LCD_V_RES / 2;
        for (int y = 0; y < TEST_LCD_V_RES; y++) {
            for (int x = 0; x < TEST_LCD_H_RES; x++) {
                int dx = x - center_x;
                int dy = y - center_y;
                float angle = atan2(dy, dx);
                float distance = sqrt(dx * dx + dy * dy);
                
                float spiral = angle + distance * 0.1;
                int color_index = (int)(spiral * 100) % 6;
                
                uint16_t color;
                switch (color_index) {
                    case 0: color = 0xF800; break;
                    case 1: color = 0x07E0; break;
                    case 2: color = 0x001F; break;
                    case 3: color = 0xFFE0; break;
                    case 4: color = 0xF81F; break;
                    case 5: color = 0x07FF; break;
                    default: color = 0x0000; break;
                }
                color_buffer[y * TEST_LCD_H_RES + x] = color;
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, TEST_LCD_H_RES, TEST_LCD_V_RES, color_buffer));
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "绘制波浪图案");
        for (int y = 0; y < TEST_LCD_V_RES; y++) {
            for (int x = 0; x < TEST_LCD_H_RES; x++) {
                float wave1 = sin(x * 0.1) * 20;
                float wave2 = sin(y * 0.1) * 20;
                float combined = wave1 + wave2;
                
                int color_index = (int)(combined + 40) % 6;
                uint16_t color;
                switch (color_index) {
                    case 0: color = 0xF800; break;
                    case 1: color = 0x07E0; break;
                    case 2: color = 0x001F; break;
                    case 3: color = 0xFFE0; break;
                    case 4: color = 0xF81F; break;
                    case 5: color = 0x07FF; break;
                    default: color = 0x0000; break;
                }
                color_buffer[y * TEST_LCD_H_RES + x] = color;
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, TEST_LCD_H_RES, TEST_LCD_V_RES, color_buffer));
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "第%d轮测试完成", pattern_count);
    }
    
    free(color_buffer);
}
