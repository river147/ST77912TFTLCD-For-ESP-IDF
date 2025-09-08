#include <stdlib.h>
#include <sys/cdefs.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_log.h"

#include "esp_lcd_st77912.h"

#define LCD_OPCODE_WRITE_CMD        (0x02ULL)
#define LCD_OPCODE_READ_CMD         (0x0BULL)
#define LCD_OPCODE_WRITE_COLOR      (0x32ULL)

#define ST77912_CMD_SET             (0xF0)
#define ST77912_PARAM_SET           (0x00)

static const char *TAG = "st77912";

static esp_err_t panel_st77912_del(esp_lcd_panel_t *panel);
static esp_err_t panel_st77912_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_st77912_init(esp_lcd_panel_t *panel);
static esp_err_t panel_st77912_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_st77912_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_st77912_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_st77912_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_st77912_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_st77912_disp_on_off(esp_lcd_panel_t *panel, bool off);

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val;
    uint8_t colmod_val;
    const st77912_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    struct {
        unsigned int use_qspi_interface: 1;
        unsigned int reset_level: 1;
    } flags;
} st77912_panel_t;

esp_err_t esp_lcd_new_panel_st77912(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t ret = ESP_OK;
    st77912_panel_t *st77912 = NULL;
    st77912 = calloc(1, sizeof(st77912_panel_t));
    ESP_GOTO_ON_FALSE(st77912, ESP_ERR_NO_MEM, err, TAG, "no mem for st77912 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        st77912->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        st77912->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color element order");
        break;
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 16: // RGB565
        st77912->colmod_val = 0x55;
        st77912->fb_bits_per_pixel = 16;
        break;
    case 18: // RGB666
        st77912->colmod_val = 0x66;
        // each color component (R/G/B) should occupy the 6 high bits of a byte, which means 3 full bytes are required for a pixel
        st77912->fb_bits_per_pixel = 24;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    st77912->io = io;
    st77912->reset_gpio_num = panel_dev_config->reset_gpio_num;
    st77912->flags.reset_level = panel_dev_config->flags.reset_active_high;
    st77912_vendor_config_t *vendor_config = (st77912_vendor_config_t *)panel_dev_config->vendor_config;
    if (vendor_config) {
        st77912->init_cmds = vendor_config->init_cmds;
        st77912->init_cmds_size = vendor_config->init_cmds_size;
        st77912->flags.use_qspi_interface = vendor_config->flags.use_qspi_interface;
    }
    st77912->base.del = panel_st77912_del;
    st77912->base.reset = panel_st77912_reset;
    st77912->base.init = panel_st77912_init;
    st77912->base.draw_bitmap = panel_st77912_draw_bitmap;
    st77912->base.invert_color = panel_st77912_invert_color;
    st77912->base.set_gap = panel_st77912_set_gap;
    st77912->base.mirror = panel_st77912_mirror;
    st77912->base.swap_xy = panel_st77912_swap_xy;
    st77912->base.disp_on_off = panel_st77912_disp_on_off;
    *ret_panel = &(st77912->base);
    ESP_LOGD(TAG, "new st77912 panel @%p", st77912);

    ESP_LOGI(TAG, "LCD panel create success");
    
    return ESP_OK;

err:
    if (st77912) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(st77912);
    }
    return ret;
}

static esp_err_t tx_param(st77912_panel_t *st77912, esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size)
{
    if (st77912->flags.use_qspi_interface) {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
    }
    return esp_lcd_panel_io_tx_param(io, lcd_cmd, param, param_size);
}

static esp_err_t tx_color(st77912_panel_t *st77912, esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size)
{
    if (st77912->flags.use_qspi_interface) {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_COLOR << 24;
    }
    return esp_lcd_panel_io_tx_color(io, lcd_cmd, param, param_size);
}

static esp_err_t panel_st77912_del(esp_lcd_panel_t *panel)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);

    if (st77912->reset_gpio_num >= 0) {
        gpio_reset_pin(st77912->reset_gpio_num);
    }
    ESP_LOGD(TAG, "del st77912 panel @%p", st77912);
    free(st77912);
    return ESP_OK;
}

static esp_err_t panel_st77912_reset(esp_lcd_panel_t *panel)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    esp_lcd_panel_io_handle_t io = st77912->io;

    if (st77912->reset_gpio_num >= 0) {
        gpio_set_level(st77912->reset_gpio_num, st77912->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(st77912->reset_gpio_num, !st77912->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        ESP_RETURN_ON_ERROR(tx_param(st77912, io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    
    return ESP_OK;
}

static const st77912_lcd_init_cmd_t vendor_specific_init_default[] = {
    {0x01, NULL, 0, 120},
    {0xF0, (uint8_t []){0x01}, 1, 0}, {0xF1, (uint8_t []){0x01}, 1, 0},
    {0x7A, (uint8_t []){0x83}, 1, 0}, {0xB0, (uint8_t []){0x5E}, 1, 0},
    {0xB1, (uint8_t []){0x55}, 1, 0}, {0xB2, (uint8_t []){0x24}, 1, 0},
    {0xB4, (uint8_t []){0xA7}, 1, 0}, {0xB5, (uint8_t []){0x54}, 1, 0},
    {0xB6, (uint8_t []){0x8B}, 1, 0}, {0xB7, (uint8_t []){0x50}, 1, 0},
    {0xBA, (uint8_t []){0x00}, 1, 0}, {0xBB, (uint8_t []){0x08}, 1, 0},
    {0xBC, (uint8_t []){0x08}, 1, 0}, {0xBD, (uint8_t []){0x00}, 1, 0},
    {0xC0, (uint8_t []){0x80}, 1, 0}, {0xC1, (uint8_t []){0x08}, 1, 0},
    {0xC2, (uint8_t []){0x54}, 1, 0}, {0xC3, (uint8_t []){0x80}, 1, 0},
    {0xC4, (uint8_t []){0x08}, 1, 0}, {0xC5, (uint8_t []){0x54}, 1, 0},
    {0xC6, (uint8_t []){0xA9}, 1, 0}, {0xC7, (uint8_t []){0x41}, 1, 0},
    {0xC8, (uint8_t []){0x51}, 1, 0}, {0xC9, (uint8_t []){0xA9}, 1, 0},
    {0xCA, (uint8_t []){0x41}, 1, 0}, {0xCB, (uint8_t []){0x51}, 1, 0},
    {0xD0, (uint8_t []){0x80}, 1, 0}, {0xD1, (uint8_t []){0xF0}, 1, 0},
    {0xD2, (uint8_t []){0xF0}, 1, 0}, {0xF5, (uint8_t []){0x00, 0xA5}, 2, 0},
    {0xDD, (uint8_t []){0x36}, 1, 0}, {0xDE, (uint8_t []){0x36}, 1, 0},
    {0xF0, (uint8_t []){0x02}, 1, 0}, {0xF1, (uint8_t []){0x01}, 1, 0},
    {0xE0, (uint8_t []){0xF0,0x16,0x1C,0x0A,0x0A,0x06,0x3E,0x33,0x53,0x07,0x14,0x13,0x31,0x35}, 14, 0},
    {0xE1, (uint8_t []){0xF0,0x16,0x1C,0x0A,0x0A,0x06,0x3E,0x33,0x53,0x07,0x14,0x13,0x31,0x35}, 14, 0},
    {0xF0, (uint8_t []){0x10}, 1, 0}, {0xF3, (uint8_t []){0x10}, 1, 0},
    {0xE0, (uint8_t []){0x0B}, 1, 0}, {0xE1, (uint8_t []){0x00}, 1, 0},
    {0xE2, (uint8_t []){0x00}, 1, 0}, {0xE3, (uint8_t []){0x00}, 1, 0},
    {0xE4, (uint8_t []){0xE0}, 1, 0}, {0xE5, (uint8_t []){0x06}, 1, 0},
    {0xE6, (uint8_t []){0x21}, 1, 0}, {0xE7, (uint8_t []){0x80}, 1, 0},
    {0xE8, (uint8_t []){0x0A}, 1, 0}, {0xE9, (uint8_t []){0x00}, 1, 0},
    {0xEA, (uint8_t []){0x04}, 1, 0}, {0xEB, (uint8_t []){0x00}, 1, 0},
    {0xEC, (uint8_t []){0x00}, 1, 0}, {0xED, (uint8_t []){0x24}, 1, 0},
    {0xEE, (uint8_t []){0x00}, 1, 0}, {0xEF, (uint8_t []){0x00}, 1, 0},
    {0xF8, (uint8_t []){0xFF}, 1, 0}, {0xF9, (uint8_t []){0x00}, 1, 0},
    {0xFA, (uint8_t []){0x00}, 1, 0}, {0xFB, (uint8_t []){0x30}, 1, 0},
    {0xFC, (uint8_t []){0x00}, 1, 0}, {0xFD, (uint8_t []){0x00}, 1, 0},
    {0xFE, (uint8_t []){0x00}, 1, 0}, {0xFF, (uint8_t []){0x00}, 1, 0},
    {0x60, (uint8_t []){0x40}, 1, 0}, {0x61, (uint8_t []){0x08}, 1, 0},
    {0x62, (uint8_t []){0x00}, 1, 0}, {0x63, (uint8_t []){0x41}, 1, 0},
    {0x64, (uint8_t []){0xED}, 1, 0}, {0x65, (uint8_t []){0x00}, 1, 0},
    {0x66, (uint8_t []){0x40}, 1, 0}, {0x67, (uint8_t []){0x00}, 1, 0},
    {0x68, (uint8_t []){0x00}, 1, 0}, {0x69, (uint8_t []){0x40}, 1, 0},
    {0x6A, (uint8_t []){0x00}, 1, 0}, {0x6B, (uint8_t []){0x00}, 1, 0},
    {0x70, (uint8_t []){0x40}, 1, 0}, {0x71, (uint8_t []){0x07}, 1, 0},
    {0x72, (uint8_t []){0x00}, 1, 0}, {0x73, (uint8_t []){0x41}, 1, 0},
    {0x74, (uint8_t []){0xEC}, 1, 0}, {0x75, (uint8_t []){0x00}, 1, 0},
    {0x76, (uint8_t []){0x40}, 1, 0}, {0x77, (uint8_t []){0x00}, 1, 0},
    {0x78, (uint8_t []){0x00}, 1, 0}, {0x79, (uint8_t []){0x40}, 1, 0},
    {0x7A, (uint8_t []){0x00}, 1, 0}, {0x7B, (uint8_t []){0x00}, 1, 0},
    {0x80, (uint8_t []){0x48}, 1, 0}, {0x81, (uint8_t []){0x00}, 1, 0},
    {0x82, (uint8_t []){0x0A}, 1, 0}, {0x83, (uint8_t []){0x01}, 1, 0},
    {0x84, (uint8_t []){0xEA}, 1, 0}, {0x85, (uint8_t []){0x00}, 1, 0},
    {0x86, (uint8_t []){0x00}, 1, 0}, {0x87, (uint8_t []){0x00}, 1, 0},
    {0x88, (uint8_t []){0x48}, 1, 0}, {0x89, (uint8_t []){0x00}, 1, 0},
    {0x8A, (uint8_t []){0x0C}, 1, 0}, {0x8B, (uint8_t []){0x01}, 1, 0},
    {0x8C, (uint8_t []){0xEC}, 1, 0}, {0x8D, (uint8_t []){0x00}, 1, 0},
    {0x8E, (uint8_t []){0x00}, 1, 0}, {0x8F, (uint8_t []){0x00}, 1, 0},
    {0x90, (uint8_t []){0x48}, 1, 0}, {0x91, (uint8_t []){0x00}, 1, 0},
    {0x92, (uint8_t []){0x0E}, 1, 0}, {0x93, (uint8_t []){0x01}, 1, 0},
    {0x94, (uint8_t []){0xEE}, 1, 0}, {0x95, (uint8_t []){0x00}, 1, 0},
    {0x96, (uint8_t []){0x00}, 1, 0}, {0x97, (uint8_t []){0x00}, 1, 0},
    {0x98, (uint8_t []){0x48}, 1, 0}, {0x99, (uint8_t []){0x00}, 1, 0},
    {0x9A, (uint8_t []){0x10}, 1, 0}, {0x9B, (uint8_t []){0x01}, 1, 0},
    {0x9C, (uint8_t []){0xF0}, 1, 0}, {0x9D, (uint8_t []){0x00}, 1, 0},
    {0x9E, (uint8_t []){0x00}, 1, 0}, {0x9F, (uint8_t []){0x00}, 1, 0},
    {0xA0, (uint8_t []){0x48}, 1, 0}, {0xA1, (uint8_t []){0x00}, 1, 0},
    {0xA2, (uint8_t []){0x09}, 1, 0}, {0xA3, (uint8_t []){0x01}, 1, 0},
    {0xA4, (uint8_t []){0xE9}, 1, 0}, {0xA5, (uint8_t []){0x00}, 1, 0},
    {0xA6, (uint8_t []){0x00}, 1, 0}, {0xA7, (uint8_t []){0x00}, 1, 0},
    {0xA8, (uint8_t []){0x48}, 1, 0}, {0xA9, (uint8_t []){0x00}, 1, 0},
    {0xAA, (uint8_t []){0x0B}, 1, 0}, {0xAB, (uint8_t []){0x01}, 1, 0},
    {0xAC, (uint8_t []){0xEB}, 1, 0}, {0xAD, (uint8_t []){0x00}, 1, 0},
    {0xAE, (uint8_t []){0x00}, 1, 0}, {0xAF, (uint8_t []){0x00}, 1, 0},
    {0xB0, (uint8_t []){0x48}, 1, 0}, {0xB1, (uint8_t []){0x00}, 1, 0},
    {0xB2, (uint8_t []){0x0D}, 1, 0}, {0xB3, (uint8_t []){0x01}, 1, 0},
    {0xB4, (uint8_t []){0xED}, 1, 0}, {0xB5, (uint8_t []){0x00}, 1, 0},
    {0xB6, (uint8_t []){0x00}, 1, 0}, {0xB7, (uint8_t []){0x00}, 1, 0},
    {0xB8, (uint8_t []){0x48}, 1, 0}, {0xB9, (uint8_t []){0x00}, 1, 0},
    {0xBA, (uint8_t []){0x0F}, 1, 0}, {0xBB, (uint8_t []){0x01}, 1, 0},
    {0xBC, (uint8_t []){0xEF}, 1, 0}, {0xBD, (uint8_t []){0x00}, 1, 0},
    {0xBE, (uint8_t []){0x00}, 1, 0}, {0xBF, (uint8_t []){0x00}, 1, 0},
    {0xC0, (uint8_t []){0x88}, 1, 0}, {0xC1, (uint8_t []){0x99}, 1, 0},
    {0xC2, (uint8_t []){0x01}, 1, 0}, {0xC3, (uint8_t []){0xAA}, 1, 0},
    {0xC4, (uint8_t []){0xBB}, 1, 0}, {0xC5, (uint8_t []){0x74}, 1, 0},
    {0xC6, (uint8_t []){0x65}, 1, 0}, {0xC7, (uint8_t []){0x56}, 1, 0},
    {0xC8, (uint8_t []){0x47}, 1, 0}, {0xC9, (uint8_t []){0x10}, 1, 0},
    {0xD0, (uint8_t []){0x88}, 1, 0}, {0xD1, (uint8_t []){0x99}, 1, 0},
    {0xD2, (uint8_t []){0x01}, 1, 0}, {0xD3, (uint8_t []){0xAA}, 1, 0},
    {0xD4, (uint8_t []){0xBB}, 1, 0}, {0xD5, (uint8_t []){0x74}, 1, 0},
    {0xD6, (uint8_t []){0x65}, 1, 0}, {0xD7, (uint8_t []){0x56}, 1, 0},
    {0xD8, (uint8_t []){0x47}, 1, 0}, {0xD9, (uint8_t []){0x10}, 1, 0},
    {0x2A, (uint8_t []){0x00, 0x00, 0x00, 0xEF}, 4, 0},
    {0x2B, (uint8_t []){0x00, 0x00, 0x00, 0xEF}, 4, 0},
    {0x21, (uint8_t []){0x00}, 1, 0},
    {0x11, (uint8_t []){0x00}, 1, 120},
};

static esp_err_t panel_st77912_init(esp_lcd_panel_t *panel)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    esp_lcd_panel_io_handle_t io = st77912->io;
    const st77912_lcd_init_cmd_t *init_cmds = NULL;
    uint16_t init_cmds_size = 0;
    bool is_user_set = true;
    bool is_cmd_overwritten = false;

    ESP_RETURN_ON_ERROR(tx_param(st77912, io, LCD_CMD_MADCTL, (uint8_t[]) {
        st77912->madctl_val,
    }, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(tx_param(st77912, io, LCD_CMD_COLMOD, (uint8_t[]) {
        st77912->colmod_val,
    }, 1), TAG, "send command failed");

    if (st77912->init_cmds) {
        init_cmds = st77912->init_cmds;
        init_cmds_size = st77912->init_cmds_size;
    } else {
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(st77912_lcd_init_cmd_t);
    }

    for (int i = 0; i < init_cmds_size; i++) {
        if (is_user_set && (init_cmds[i].data_bytes > 0)) {
            switch (init_cmds[i].cmd) {
            case LCD_CMD_MADCTL:
                is_cmd_overwritten = true;
                st77912->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
                break;
            case LCD_CMD_COLMOD:
                is_cmd_overwritten = true;
                st77912->colmod_val = ((uint8_t *)init_cmds[i].data)[0];
                break;
            default:
                is_cmd_overwritten = false;
                break;
            }

            if (is_cmd_overwritten) {
                is_cmd_overwritten = false;
                ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence", init_cmds[i].cmd);
            }
        }

        ESP_RETURN_ON_ERROR(tx_param(st77912, io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));

        if ((init_cmds[i].cmd == ST77912_CMD_SET)) {
            is_user_set = ((uint8_t *)init_cmds[i].data)[0] == ST77912_PARAM_SET ? true : false;
        }
    }
    ESP_LOGD(TAG, "send init commands success");

    return ESP_OK;
}

static esp_err_t panel_st77912_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");
    esp_lcd_panel_io_handle_t io = st77912->io;

    x_start += st77912->x_gap;
    x_end += st77912->x_gap;
    y_start += st77912->y_gap;
    y_end += st77912->y_gap;

    ESP_RETURN_ON_ERROR(tx_param(st77912, io, LCD_CMD_CASET, (uint8_t[]) {
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF,
    }, 4), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(tx_param(st77912, io, LCD_CMD_RASET, (uint8_t[]) {
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF,
    }, 4), TAG, "send command failed");
    size_t len = (x_end - x_start) * (y_end - y_start) * st77912->fb_bits_per_pixel / 8;
    tx_color(st77912, io, LCD_CMD_RAMWR, color_data, len);
    
    return ESP_OK;
}

static esp_err_t panel_st77912_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    esp_lcd_panel_io_handle_t io = st77912->io;
    int command = 0;
    if (invert_color_data) {
        command = LCD_CMD_INVON;
    } else {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(tx_param(st77912, io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_st77912_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    esp_lcd_panel_io_handle_t io = st77912->io;
    esp_err_t ret = ESP_OK;

    if (mirror_x) {
        st77912->madctl_val |= BIT(6);
    } else {
        st77912->madctl_val &= ~BIT(6);
    }
    if (mirror_y) {
        st77912->madctl_val |= BIT(7);
    } else {
        st77912->madctl_val &= ~BIT(7);
    }
    ESP_RETURN_ON_ERROR(tx_param(st77912, io, LCD_CMD_MADCTL, (uint8_t[]) {
        st77912->madctl_val
    }, 1), TAG, "send command failed");
    return ret;
}

static esp_err_t panel_st77912_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    esp_lcd_panel_io_handle_t io = st77912->io;
    if (swap_axes) {
        st77912->madctl_val |= LCD_CMD_MV_BIT;
    } else {
        st77912->madctl_val &= ~LCD_CMD_MV_BIT;
    }
    ESP_RETURN_ON_ERROR(tx_param(st77912, io, LCD_CMD_MADCTL, (uint8_t[]) {
        st77912->madctl_val
    }, 1), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_st77912_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    st77912->x_gap = x_gap;
    st77912->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_st77912_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    st77912_panel_t *st77912 = __containerof(panel, st77912_panel_t, base);
    esp_lcd_panel_io_handle_t io = st77912->io;
    int command = 0;

    if (on_off) {
        command = LCD_CMD_DISPON;
    } else {
        command = LCD_CMD_DISPOFF;
    }
    ESP_RETURN_ON_ERROR(tx_param(st77912, io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}