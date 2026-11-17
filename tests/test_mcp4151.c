#include "mcp4151.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#define CS_PIN 1U // 模拟PB1的片选标识

static SPI_TypeDef instance;
static SPI_HandleTypeDef spi;
static GPIO_TypeDef cs;
static uint16_t frames[32];
static unsigned frame_count, gpio_writes, fail_attempts;
static HAL_StatusTypeDef next_status;

// 返回模拟SPI的当前状态。
HAL_SPI_StateTypeDef HAL_SPI_GetState(const SPI_HandleTypeDef *hspi)
{
    assert(hspi == &spi);
    return hspi->State;
}

// 检查片选只操作配置的GPIO，拉低前必须处于空闲。
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    assert(port == &cs && pin == CS_PIN);
    if (state == GPIO_PIN_RESET) {
        assert(cs.levels == CS_PIN && spi.State == HAL_SPI_STATE_READY);
        port->levels = 0U;
    } else {
        port->levels = CS_PIN;
    }
    ++gpio_writes;
}

// 记录完整命令帧，按剩余失败次数模拟传输结果。
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, const uint8_t *data,
                                   uint16_t size, uint32_t timeout)
{
    assert(hspi == &spi && cs.levels == 0U);
    assert(size == 2U && timeout == 100U);
    assert(frame_count < sizeof(frames) / sizeof(frames[0]));
    frames[frame_count++] = (uint16_t)((uint16_t)data[0] << 8U) | data[1];
    spi.State = HAL_SPI_STATE_BUSY_TX;
    HAL_StatusTypeDef result = HAL_OK;
    if (fail_attempts > 0U) {
        result = next_status;
        --fail_attempts;
    }
    spi.State = HAL_SPI_STATE_READY;
    return result;
}

// 核对最后一帧的编码、句柄记录和片选释放。
static void expect_position(const mcp4151_t *dev, uint16_t expected)
{
    assert(frame_count > 0U && frames[frame_count - 1U] == expected);
    assert(dev->initialized && dev->position == expected && cs.levels == CS_PIN);
}

// 验证初始化结构体、位置边界、总线忙及发送失败后的状态。
int main(void)
{
    mcp4151_t dev = {0};
    spi.Instance = &instance;
    spi.State = HAL_SPI_STATE_READY;
    spi.Init = (SPI_InitTypeDef){
        .Mode = SPI_MODE_MASTER, .Direction = SPI_DIRECTION_2LINES,
        .DataSize = SPI_DATASIZE_8BIT, .FirstBit = SPI_FIRSTBIT_MSB,
        .CLKPolarity = SPI_POLARITY_LOW, .CLKPhase = SPI_PHASE_1EDGE,
        .NSS = SPI_NSS_SOFT, .TIMode = SPI_TIMODE_DISABLE,
        .CRCCalculation = SPI_CRCCALCULATION_DISABLE
    };
    cs.levels = CS_PIN;
    mcp4151_init_t config = {
        .hspi = &spi, .cs_port = &cs, .cs_pin = CS_PIN, .initial_position = 32U
    };

    assert(mcp4151_set_position(NULL, 0U) == HAL_ERROR);
    assert(mcp4151_set_position(&dev, 0U) == HAL_ERROR);
    assert(mcp4151_init(NULL, &config) == HAL_ERROR);
    assert(mcp4151_init(&dev, NULL) == HAL_ERROR);
    config.hspi = NULL;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    config.hspi = &spi;
    spi.Instance = NULL;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    spi.Instance = &instance;
    config.cs_port = NULL;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    config.cs_port = &cs;
    config.cs_pin = 0U;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    config.cs_pin = 3U;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    config.cs_pin = CS_PIN;
    config.initial_position = 257U;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    config.initial_position = 32U;

    spi.Init.Mode = 0U;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    spi.Init.Mode = SPI_MODE_MASTER;
    spi.Init.DataSize = 16U;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    spi.Init.DataSize = SPI_DATASIZE_8BIT;
    spi.Init.FirstBit = 1U;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    spi.Init.CLKPolarity = 1U;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    spi.Init.CLKPhase = 1U;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    spi.Init.CRCCalculation = 1U;
    assert(mcp4151_init(&dev, &config) == HAL_ERROR);
    spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    spi.State = HAL_SPI_STATE_BUSY_TX;
    assert(mcp4151_init(&dev, &config) == HAL_BUSY);
    assert(frame_count == 0U && gpio_writes == 0U && !dev.initialized);
    spi.State = HAL_SPI_STATE_READY;

    assert(mcp4151_init(&dev, &config) == HAL_OK);
    expect_position(&dev, 32U);
    assert(dev.hspi == &spi && dev.cs_port == &cs && dev.cs_pin == CS_PIN);
    config.initial_position = 256U;
    assert(dev.position == 32U);
    const uint16_t positions[] = {0U, 128U, 255U, 256U};
    for (unsigned i = 0U; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        assert(mcp4151_set_position(&dev, positions[i]) == HAL_OK);
        expect_position(&dev, positions[i]);
    }

    unsigned before = frame_count;
    unsigned writes_before = gpio_writes;
    assert(mcp4151_set_position(&dev, 257U) == HAL_ERROR);
    assert(mcp4151_set_position(&dev, 65535U) == HAL_ERROR);
    spi.State = HAL_SPI_STATE_BUSY_TX;
    assert(mcp4151_set_position(&dev, 127U) == HAL_BUSY);
    assert(frame_count == before && gpio_writes == writes_before);
    assert(dev.initialized && dev.position == 256U && cs.levels == CS_PIN);
    spi.State = HAL_SPI_STATE_READY;
    // HAL返回忙时不重发，也不改变句柄状态。
    before = frame_count;
    next_status = HAL_BUSY;
    fail_attempts = 1U;
    assert(mcp4151_set_position(&dev, 127U) == HAL_BUSY);
    assert(frame_count == before + 1U);
    assert(dev.initialized && dev.position == 256U && cs.levels == CS_PIN);

    // 首次超时后自动重发成功，位置更新且保持可用。
    before = frame_count;
    next_status = HAL_TIMEOUT;
    fail_attempts = 1U;
    assert(mcp4151_set_position(&dev, 127U) == HAL_OK);
    expect_position(&dev, 127U);
    assert(frame_count == before + 2U);

    // 连续两次超时返回失败，但仍允许后续再次发送。
    before = frame_count;
    next_status = HAL_TIMEOUT;
    fail_attempts = 2U;
    assert(mcp4151_set_position(&dev, 0U) == HAL_TIMEOUT);
    assert(frame_count == before + 2U);
    assert(dev.initialized && dev.position == 127U && cs.levels == CS_PIN);

    next_status = HAL_OK;
    assert(mcp4151_set_position(&dev, 0U) == HAL_OK);
    expect_position(&dev, 0U);
    config.initial_position = 32U;
    next_status = HAL_TIMEOUT;
    fail_attempts = 2U;
    assert(mcp4151_init(&dev, &config) == HAL_TIMEOUT);
    assert(!dev.initialized && dev.position == 0U && cs.levels == CS_PIN);

    // 初始化首帧失败可由重发救回。
    next_status = HAL_ERROR;
    fail_attempts = 1U;
    assert(mcp4151_init(&dev, &config) == HAL_OK);
    expect_position(&dev, 32U);

    config.initial_position = 256U;
    assert(mcp4151_init(&dev, &config) == HAL_OK);
    expect_position(&dev, 256U);
    next_status = HAL_ERROR;
    fail_attempts = 2U;
    assert(mcp4151_set_position(&dev, 0U) == HAL_ERROR);
    assert(dev.initialized && dev.position == 256U && cs.levels == CS_PIN);
    config.initial_position = 0U;
    assert(mcp4151_init(&dev, &config) == HAL_OK);
    expect_position(&dev, 0U);

    puts("MCP4151初始化、0～256编码、片选、重发、忙状态和错误处理测试通过。");
    return 0;
}
