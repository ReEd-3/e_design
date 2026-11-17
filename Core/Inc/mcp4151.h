#ifndef MCP4151_H
#define MCP4151_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MCP4151_MAX_POSITION 256U // 滑动端最大位置，共257个有效位置

typedef struct {
    SPI_HandleTypeDef *hspi;  // 已由MX初始化的SPI句柄
    GPIO_TypeDef *cs_port;    // 片选端口
    uint16_t cs_pin;          // 片选引脚
    uint16_t initial_position; // 初始滑动端位置
} mcp4151_init_t;

typedef struct {
    SPI_HandleTypeDef *hspi; // SPI句柄
    GPIO_TypeDef *cs_port;   // 片选端口
    uint16_t cs_pin;         // 片选引脚
    uint16_t position;       // 最近发送成功的位置，范围0～256
    bool initialized;       // 软件初始化完成标志
} mcp4151_t;

// 按初始化参数绑定接口并写入初始位置，使用前须完成MX配置。
HAL_StatusTypeDef mcp4151_init(mcp4151_t *dev, const mcp4151_init_t *config);

// 写入滑动端位置，超时或错误自动重发一次，仍失败则须重新初始化。
HAL_StatusTypeDef mcp4151_set_position(mcp4151_t *dev, uint16_t position);

#ifdef __cplusplus
}
#endif

#endif
