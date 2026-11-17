#ifndef AD9833_H
#define AD9833_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AD9833_WAVE_SINE = 0x0000U,     // 正弦波
    AD9833_WAVE_TRIANGLE = 0x0002U, // 三角波
    AD9833_WAVE_SQUARE = 0x0028U    // 方波，频率与设置值相同
} ad9833_waveform_t;

typedef struct {
    GPIO_TypeDef *sclk_port;  // 时钟端口
    uint16_t sclk_pin;        // 时钟引脚
    GPIO_TypeDef *sdata_port; // 数据端口
    uint16_t sdata_pin;       // 数据引脚
    GPIO_TypeDef *cs_port;    // 片选端口
    uint16_t cs_pin;          // 片选引脚
    uint32_t mclk_hz;         // 模块时钟频率，单位Hz
    uint32_t initial_frequency_hz; // 初始输出频率，单位Hz
    ad9833_waveform_t initial_waveform; // 初始输出波形
} ad9833_init_t;

typedef struct {
    GPIO_TypeDef *sclk_port;  // 时钟端口
    uint16_t sclk_pin;        // 时钟引脚
    GPIO_TypeDef *sdata_port; // 数据端口
    uint16_t sdata_pin;       // 数据引脚
    GPIO_TypeDef *cs_port;    // 片选端口
    uint16_t cs_pin;          // 片选引脚
    uint32_t mclk_hz;         // 模块时钟频率，单位Hz
    uint32_t frequency_hz;    // 最近设置成功的频率，单位Hz
    ad9833_waveform_t waveform; // 最近设置成功的波形
    uint16_t control_word;    // 最近发送的控制字副本
    bool initialized;        // 软件初始化完成标志
} ad9833_t;

// 按驱动初始化参数设置接口、频率、波形和零相位，并保持停止。
HAL_StatusTypeDef ad9833_init(ad9833_t *dev, const ad9833_init_t *config);

// 清除复位位，开始输出当前设置的波形。
HAL_StatusTypeDef ad9833_start(ad9833_t *dev);

// 设置复位位，停止交流输出，输出停在中间电平。
HAL_StatusTypeDef ad9833_stop(ad9833_t *dev);

// 设置频率，单位Hz，范围为0至模块时钟的一半，完成后保持正弦波停止状态。
HAL_StatusTypeDef ad9833_set_frequency(ad9833_t *dev, uint32_t frequency_hz);

// 设置正弦波、三角波或方波，保留当前启停状态。
HAL_StatusTypeDef ad9833_set_waveform(ad9833_t *dev,
                                    ad9833_waveform_t waveform);

#ifdef __cplusplus
}
#endif

#endif
