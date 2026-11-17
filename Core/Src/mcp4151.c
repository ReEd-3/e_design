#include "mcp4151.h"
#include <stddef.h>

#define MCP4151_WRITE_WIPER0 0x0000U // 地址0的滑动端位置写命令
#define MCP4151_TIMEOUT_MS  100U    // 单次阻塞发送的超时时间，单位ms
#define MCP4151_RETRY_COUNT 1U      // 发送超时或错误后的重发次数

// 片选覆盖两个字节，高字节先发，返回前释放片选。
static HAL_StatusTypeDef mcp4151_write_word(mcp4151_t *dev, uint16_t word)
{
    if (HAL_SPI_GetState(dev->hspi) != HAL_SPI_STATE_READY) {
        return HAL_BUSY;
    }
    uint8_t data[2] = {(uint8_t)(word >> 8U), (uint8_t)word};
    HAL_StatusTypeDef status = HAL_ERROR;

    // 芯片丢弃不足16时钟的残帧，超时或错误可整帧重发。
    for (uint32_t attempt = 0U; attempt <= MCP4151_RETRY_COUNT; ++attempt) {
        HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
        status = HAL_SPI_Transmit(dev->hspi, data, 2U, MCP4151_TIMEOUT_MS);
        HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
        if (status != HAL_TIMEOUT && status != HAL_ERROR) {
            break;
        }
    }
    if (status != HAL_OK && status != HAL_BUSY) {
        dev->initialized = false;
    }
    return status;
}

// 校验模式0接口并写入初始位置，发送成功后标记初始化完成。
HAL_StatusTypeDef mcp4151_init(mcp4151_t *dev, const mcp4151_init_t *config)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }
    dev->initialized = false;
    if (config == NULL || config->hspi == NULL || config->hspi->Instance == NULL ||
        config->cs_port == NULL || config->cs_pin == 0U ||
        (config->cs_pin & (config->cs_pin - 1U)) != 0U ||
        config->initial_position > MCP4151_MAX_POSITION) {
        return HAL_ERROR;
    }
    const SPI_InitTypeDef *spi = &config->hspi->Init;
    if (spi->Mode != SPI_MODE_MASTER || spi->Direction != SPI_DIRECTION_2LINES ||
        spi->DataSize != SPI_DATASIZE_8BIT || spi->FirstBit != SPI_FIRSTBIT_MSB ||
        spi->CLKPolarity != SPI_POLARITY_LOW || spi->CLKPhase != SPI_PHASE_1EDGE ||
        spi->NSS != SPI_NSS_SOFT || spi->TIMode != SPI_TIMODE_DISABLE ||
        spi->CRCCalculation != SPI_CRCCALCULATION_DISABLE) {
        return HAL_ERROR;
    }
    if (HAL_SPI_GetState(config->hspi) != HAL_SPI_STATE_READY) {
        return HAL_BUSY;
    }

    dev->hspi = config->hspi;
    dev->cs_port = config->cs_port;
    dev->cs_pin = config->cs_pin;
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);

    HAL_StatusTypeDef status = mcp4151_write_word(dev,
        MCP4151_WRITE_WIPER0 | config->initial_position);
    if (status == HAL_OK) {
        dev->position = config->initial_position;
        dev->initialized = true;
    }
    return status;
}

// 写入完整位置命令，非法参数或发送失败时不更新位置记录。
HAL_StatusTypeDef mcp4151_set_position(mcp4151_t *dev, uint16_t position)
{
    if (dev == NULL || !dev->initialized || position > MCP4151_MAX_POSITION) {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef status = mcp4151_write_word(dev, MCP4151_WRITE_WIPER0 | position);
    if (status == HAL_OK) {
        dev->position = position;
    }
    return status;
}
