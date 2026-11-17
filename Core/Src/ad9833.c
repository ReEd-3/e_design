#include "ad9833.h"
#include <stddef.h>

#define AD9833_B28          0x2000U // 连续两次写入完整的28位频率字
#define AD9833_RESET        0x0100U // 复位位，置位后停止交流输出
#define AD9833_FREQ0        0x4000U // 频率寄存器0的写入地址
#define AD9833_PHASE0       0xC000U // 相位寄存器0的写入地址
#define AD9833_FREQ_MASK    0x3FFFU // 提取频率字中的14位数据
#define AD9833_WAVE_MASK    0x002AU // 波形选择所使用的控制位
#define AD9833_BIT_DELAY_MS 1U      // 每个时钟半周期的等待时间，单位ms

// 片选拉低后逐位发送16位字，最高位先发，数据在时钟下降沿前稳定。
static void ad9833_write_word(ad9833_t *dev, uint16_t word)
{
    HAL_GPIO_WritePin(dev->sclk_port, dev->sclk_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);

    for (unsigned i = 0U; i < 16U; ++i) {
        HAL_GPIO_WritePin(dev->sdata_port, dev->sdata_pin,
                          (word & 0x8000U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_Delay(AD9833_BIT_DELAY_MS);
        HAL_GPIO_WritePin(dev->sclk_port, dev->sclk_pin, GPIO_PIN_RESET);
        HAL_Delay(AD9833_BIT_DELAY_MS);
        HAL_GPIO_WritePin(dev->sclk_port, dev->sclk_pin, GPIO_PIN_SET);
        word <<= 1U;
    }

    HAL_Delay(AD9833_BIT_DELAY_MS);
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
    HAL_Delay(AD9833_BIT_DELAY_MS);
}

// 按配置初始化接口、频率和波形，保持零相位及停止状态。
HAL_StatusTypeDef ad9833_init(ad9833_t *dev, const ad9833_init_t *config)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }
    dev->initialized = false;
    if (config == NULL || config->sclk_port == NULL || config->sdata_port == NULL ||
        config->cs_port == NULL ||
        config->sclk_pin == 0U || (config->sclk_pin & (config->sclk_pin - 1U)) != 0U ||
        config->sdata_pin == 0U || (config->sdata_pin & (config->sdata_pin - 1U)) != 0U ||
        config->cs_pin == 0U || (config->cs_pin & (config->cs_pin - 1U)) != 0U ||
        config->mclk_hz == 0U || config->mclk_hz > 25000000U ||
        config->initial_frequency_hz > config->mclk_hz / 2U ||
        (config->initial_waveform != AD9833_WAVE_SINE &&
         config->initial_waveform != AD9833_WAVE_TRIANGLE &&
         config->initial_waveform != AD9833_WAVE_SQUARE)) {
        return HAL_ERROR;
    }
    if ((config->sclk_port == config->sdata_port && config->sclk_pin == config->sdata_pin) ||
        (config->sclk_port == config->cs_port && config->sclk_pin == config->cs_pin) ||
        (config->sdata_port == config->cs_port && config->sdata_pin == config->cs_pin)) {
        return HAL_ERROR;
    }

    dev->sclk_port = config->sclk_port;
    dev->sclk_pin = config->sclk_pin;
    dev->sdata_port = config->sdata_port;
    dev->sdata_pin = config->sdata_pin;
    dev->cs_port = config->cs_port;
    dev->cs_pin = config->cs_pin;
    dev->mclk_hz = config->mclk_hz;
    dev->control_word = 0U;

    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(dev->sclk_port, dev->sclk_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(dev->sdata_port, dev->sdata_pin, GPIO_PIN_RESET);
    HAL_Delay(10U);

    const uint16_t words[] = {
        AD9833_RESET,              // 清除原有模式并保持复位
        AD9833_B28 | AD9833_RESET,  // 允许完整频率写入并保持复位
        AD9833_FREQ0,              // 频率低14位置零
        AD9833_FREQ0,              // 频率高14位置零
        AD9833_PHASE0              // 相位置零
    };

    for (unsigned i = 0U; i < sizeof(words) / sizeof(words[0]); ++i) {
        ad9833_write_word(dev, words[i]);
    }
    dev->control_word = AD9833_B28 | AD9833_RESET;
    dev->frequency_hz = 0U;
    dev->waveform = AD9833_WAVE_SINE;
    dev->initialized = true;
    HAL_StatusTypeDef status = ad9833_set_frequency(dev, config->initial_frequency_hz);
    if (status == HAL_OK) {
        status = ad9833_set_waveform(dev, config->initial_waveform);
    }
    if (status != HAL_OK) {
        dev->initialized = false;
    }
    return status;
}

// 清除复位位，开始输出，保留频率和波形设置。
HAL_StatusTypeDef ad9833_start(ad9833_t *dev)
{
    if (dev == NULL || !dev->initialized) {
        return HAL_ERROR;
    }
    uint16_t word = dev->control_word & ~AD9833_RESET;
    ad9833_write_word(dev, word);
    dev->control_word = word;
    return HAL_OK;
}

// 设置复位位，停止交流输出，保留频率和波形设置。
HAL_StatusTypeDef ad9833_stop(ad9833_t *dev)
{
    if (dev == NULL || !dev->initialized) {
        return HAL_ERROR;
    }
    uint16_t word = dev->control_word | AD9833_RESET;
    ad9833_write_word(dev, word);
    dev->control_word = word;
    return HAL_OK;
}

// 先写0x2100，再写频率低14位和高14位，完成后保持正弦波停止状态。
HAL_StatusTypeDef ad9833_set_frequency(ad9833_t *dev, uint32_t frequency_hz)
{
    if (dev == NULL || !dev->initialized || frequency_hz > dev->mclk_hz / 2U) {
        return HAL_ERROR;
    }
    uint32_t tuning_word = (uint32_t)((((uint64_t)frequency_hz << 28U) +
                                       dev->mclk_hz / 2U) / dev->mclk_hz);
    uint16_t low = AD9833_FREQ0 | (tuning_word & AD9833_FREQ_MASK);
    uint16_t high = AD9833_FREQ0 | ((tuning_word >> 14U) & AD9833_FREQ_MASK);

    ad9833_write_word(dev, AD9833_B28 | AD9833_RESET);
    dev->control_word = AD9833_B28 | AD9833_RESET;
    ad9833_write_word(dev, low);
    ad9833_write_word(dev, high);
    dev->frequency_hz = frequency_hz;
    dev->waveform = AD9833_WAVE_SINE;
    return HAL_OK;
}

// 修改波形选择位，支持正弦波、三角波和方波，保留启停状态。
HAL_StatusTypeDef ad9833_set_waveform(ad9833_t *dev,
                                    ad9833_waveform_t waveform)
{
    if (dev == NULL || !dev->initialized ||
        (waveform != AD9833_WAVE_SINE && waveform != AD9833_WAVE_TRIANGLE &&
         waveform != AD9833_WAVE_SQUARE)) {
        return HAL_ERROR;
    }
    uint16_t word = (dev->control_word & ~AD9833_WAVE_MASK) | (uint16_t)waveform;
    ad9833_write_word(dev, word);
    dev->control_word = word;
    dev->waveform = waveform;
    return HAL_OK;
}

