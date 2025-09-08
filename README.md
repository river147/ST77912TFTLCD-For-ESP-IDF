# ST77912 TFT LCD驱动项目

## 📋 项目简介

本项目是ST77912-4WSPI TFT LCD驱动的ESP-IDF组件实现，采用标准ESP-IDF LCD驱动架构，可作为组件被其他ESP-IDF项目引用。

## 📁 项目结构

```
项目根目录/
├── esp_lcd_st77912.c          # ST77912驱动实现文件
├── include/                    # 头文件目录
│   └── esp_lcd_st77912.h      # 驱动头文件
├── CMakeLists.txt              # 组件构建文件
├── idf_component.yml           # 组件依赖管理
├── license.txt                 # 许可证文件
├── test_apps/                  # 测试应用
│   ├── main/                   # 测试主程序
│   │   ├── lcd_test.c          # 测试代码
│   │   ├── CMakeLists.txt      # 测试程序构建文件
│   │   └── idf_component.yml   # 测试程序依赖
│   ├── CMakeLists.txt          # 测试应用构建文件
│   └── sdkconfig.defaults      # 测试配置
└── README.md                   # 项目说明（本文档）
```

## 🔧 硬件规格

- **显示屏**: ST77912 TFT LCD
- **分辨率**: 240×240像素
- **接口**: 4线SPI（4-Wire SPI）
- **颜色深度**: 16位RGB565
- **控制器**: ST77912

## 🔗 硬件要求

- **电源**: 3.3V电源，不要使用5V
- **接口**: 4线SPI（SCLK、MOSI、RES、DC、CS）+ 背光控制（可选）
- **电平**: ESP32的GPIO输出3.3V，与ST77912兼容，无需电平转换
- **连接**: 信号线尽量短，减少干扰，避免与强干扰源靠近

### 常见问题排查

- **屏幕无显示**: 检查电源电压、引脚连接、地线连接
- **显示异常/花屏**: 尝试降低SPI时钟频率（可降至20MHz）
- **背光不亮**: 检查背光控制引脚连接和代码

## 🚀 快速开始

### 作为组件使用

在其他ESP-IDF项目中，通过 `idf_component.yml` 引用：

```yaml
dependencies:
  esp_lcd_st77912:
    version: "^1.0.0"
    git: "https://gitee.com/riverchuan/esp_lcd_st77912.git"
```

### 运行测试

```bash
cd test_apps
$env:IDF_TARGET="esp32s3"
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

测试程序包含10种不同的显示图案：
1. 全屏红色
2. 全屏绿色  
3. 全屏蓝色
4. 全屏白色
5. 彩色条纹
6. 渐变彩虹
7. 棋盘格图案
8. 同心圆
9. 螺旋图案
10. 波浪图案

每种图案显示1秒，循环播放。

## 📋 功能特性

- **标准ESP-IDF LCD框架** - 完全兼容 `esp_lcd` 框架
- **组件化设计** - 支持 `idf_component.yml` 依赖管理
- **标准接口** - 实现 `esp_lcd_panel_ops_t` 接口
- **测试应用** - 包含完整的测试用例

## 🔧 使用方法

### 基本初始化

```c
#include "esp_lcd_st77912.h"

// 配置ST77912驱动
esp_lcd_st77912_config_t config = {
    .io_handle = io_handle,
    .reset_gpio_num = TFT_RES_PIN,
    .dc_gpio_num = TFT_DC_PIN,
    .cs_gpio_num = TFT_CS_PIN,
    .bl_gpio_num = TFT_BL_PIN,
    .width = LCD_WIDTH,
    .height = LCD_HEIGHT,
    // ... 其他配置
};

// 创建驱动实例
esp_lcd_st77912_handle_t handle;
esp_err_t ret = esp_lcd_st77912_new(&config, &handle);
```

## 🎯 版本历史

- **v1.0.0** (2025-08-27)
  - 初始版本
  - 标准ESP-IDF LCD驱动架构
  - 完整的测试应用
  - 组件化设计

## 📄 许可证

本项目采用MIT许可证，详见LICENSE文件。

## 🤝 贡献

欢迎提交Issue和Pull Request来改进项目。



**注意**: 本项目采用标准ESP-IDF LCD驱动架构，与官方组件架构完全兼容！