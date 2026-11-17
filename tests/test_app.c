#include "app.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static GPIO_TypeDef gpio_a, gpio_b;
static SPI_TypeDef spi_instance;
static SPI_HandleTypeDef pot_spi;
static char calls[128];
static unsigned call_count, fail_on_call;
static uint32_t last_frequency;
static uint16_t last_position;

// 记录部件调用顺序，并模拟指定步骤超时。
static HAL_StatusTypeDef record_call(char operation)
{
    assert(call_count + 1U < sizeof(calls));
    calls[call_count++] = operation;
    calls[call_count] = '\0';
    return call_count == fail_on_call ? HAL_TIMEOUT : HAL_OK;
}

// 模拟驱动接收完整初始化参数并自行设置频率和波形。
HAL_StatusTypeDef ad9833_init(ad9833_t *dev, const ad9833_init_t *config)
{
    assert(dev != NULL && config != NULL);
    assert(config->sclk_port == &gpio_a && config->sclk_pin == 0x20U);
    assert(config->sdata_port == &gpio_a && config->sdata_pin == 0x80U);
    assert(config->cs_port == &gpio_b && config->cs_pin == 1U);
    assert(config->mclk_hz == 25000000U);
    HAL_StatusTypeDef status = record_call('I');
    dev->initialized = false;
    if (status != HAL_OK) {
        return status;
    }
    if (config->initial_frequency_hz > config->mclk_hz / 2U ||
        (config->initial_waveform != AD9833_WAVE_SINE &&
         config->initial_waveform != AD9833_WAVE_TRIANGLE &&
         config->initial_waveform != AD9833_WAVE_SQUARE)) {
        return HAL_ERROR;
    }
    dev->mclk_hz = config->mclk_hz;
    dev->frequency_hz = config->initial_frequency_hz;
    dev->waveform = config->initial_waveform;
    dev->control_word = 0x2100U | config->initial_waveform;
    dev->initialized = true;
    return HAL_OK;
}

// 模拟MCP4151初始化。
HAL_StatusTypeDef mcp4151_init(mcp4151_t *dev, const mcp4151_init_t *config)
{
    assert(dev != NULL && config != NULL);
    assert(config->hspi == &pot_spi && config->cs_port == &gpio_b);
    assert(config->cs_pin == 2U && config->initial_position <= MCP4151_MAX_POSITION);
    HAL_StatusTypeDef status = record_call('P');
    dev->initialized = status == HAL_OK;
    if (status == HAL_OK) {
        dev->hspi = config->hspi;
        dev->cs_port = config->cs_port;
        dev->cs_pin = config->cs_pin;
        dev->position = config->initial_position;
    }
    return status;
}

// 模拟MCP4151位置发送，失败时不更新位置。
HAL_StatusTypeDef mcp4151_set_position(mcp4151_t *dev, uint16_t position)
{
    assert(dev != NULL && dev->initialized);
    HAL_StatusTypeDef status = record_call('R');
    if (status == HAL_OK) {
        last_position = position;
        dev->position = position;
    }
    return status;
}

// 模拟开始输出。
HAL_StatusTypeDef ad9833_start(ad9833_t *dev)
{
    assert(dev != NULL && dev->initialized);
    HAL_StatusTypeDef status = record_call('S');
    if (status == HAL_OK) {
        dev->control_word &= (uint16_t)~0x0100U;
    }
    return status;
}

// 模拟停止输出。
HAL_StatusTypeDef ad9833_stop(ad9833_t *dev)
{
    assert(dev != NULL && dev->initialized);
    HAL_StatusTypeDef status = record_call('T');
    if (status == HAL_OK) {
        dev->control_word |= 0x0100U;
    }
    return status;
}

// 模拟设置频率后恢复正弦并停止的现有驱动行为。
HAL_StatusTypeDef ad9833_set_frequency(ad9833_t *dev, uint32_t frequency_hz)
{
    assert(dev != NULL && dev->initialized && frequency_hz <= dev->mclk_hz / 2U);
    HAL_StatusTypeDef status = record_call('F');
    if (status == HAL_OK) {
        last_frequency = frequency_hz;
        dev->control_word = 0x2100U;
        dev->frequency_hz = frequency_hz;
        dev->waveform = AD9833_WAVE_SINE;
    }
    return status;
}

// 模拟设置波形并保留复位状态。
HAL_StatusTypeDef ad9833_set_waveform(ad9833_t *dev, ad9833_waveform_t waveform)
{
    assert(dev != NULL && dev->initialized);
    HAL_StatusTypeDef status = record_call('W');
    if (status == HAL_OK) {
        dev->control_word = (dev->control_word & (uint16_t)~0x002AU) | waveform;
        dev->waveform = waveform;
    }
    return status;
}

// 检查部件调用顺序，并清空本次记录。
static void expect_calls(const char *expected)
{
    assert(strcmp(calls, expected) == 0);
    call_count = fail_on_call = 0U;
    calls[0] = '\0';
}

// 验证初始化参数、换频恢复、重复请求和部件故障处理。
int main(void)
{
    ad9833_t dds = {0};
    mcp4151_t pot = {0};
    global_t global = {0};
    pot_spi.Instance = &spi_instance;
    pot_spi.State = HAL_SPI_STATE_READY;
    app_init_t config = {
        .dds_src = &dds,
        .pot_src = &pot,
        .pot_init = {
            .hspi = &pot_spi, .cs_port = &gpio_b,
            .cs_pin = 2U, .initial_position = 128U
        },
        .dds_init = {
            .sclk_port = &gpio_a, .sclk_pin = 0x20U,
            .sdata_port = &gpio_a, .sdata_pin = 0x80U,
            .cs_port = &gpio_b, .cs_pin = 1U,
            .mclk_hz = 25000000U, .initial_frequency_hz = 100000U,
            .initial_waveform = AD9833_WAVE_TRIANGLE
        }
    };

    assert(app_init(NULL, &config) == HAL_ERROR);
    assert(app_init(&global, NULL) == HAL_ERROR);
    assert(app_start(NULL) == HAL_ERROR);
    assert(app_stop(&global) == HAL_ERROR);
    assert(app_set_dds_frequency(&global, 1000U) == HAL_ERROR);
    assert(app_set_dds_waveform(&global, AD9833_WAVE_SINE) == HAL_ERROR);
    config.dds_init.initial_frequency_hz = 12500001U;
    assert(app_init(&global, &config) == HAL_ERROR);
    expect_calls("I");
    assert(global.state == APP_ERROR && !dds.initialized);
    config.dds_init.initial_frequency_hz = 100000U;
    config.dds_init.initial_waveform = (ad9833_waveform_t)3;
    assert(app_init(&global, &config) == HAL_ERROR);
    expect_calls("I");
    config.dds_init.initial_waveform = AD9833_WAVE_TRIANGLE;
    expect_calls("");

    assert(app_init(&global, &config) == HAL_OK);
    expect_calls("IP");
    assert(global.state == APP_IDLE && global.last_error == HAL_OK);
    assert(global.dds_src == &dds && dds.frequency_hz == 100000U);
    assert(global.pot_src == &pot && pot.initialized && pot.position == 128U);
    assert(dds.waveform == AD9833_WAVE_TRIANGLE && dds.control_word == 0x2102U);

    config.dds_init.initial_frequency_hz = 200000U;
    config.dds_init.initial_waveform = AD9833_WAVE_SINE;
    assert(dds.frequency_hz == 100000U && dds.waveform == AD9833_WAVE_TRIANGLE);
    assert(app_set_dds_frequency(&global, 200000U) == HAL_OK);
    expect_calls("FW");
    assert(global.state == APP_IDLE && dds.control_word == 0x2102U);
    assert(last_frequency == 200000U && dds.frequency_hz == last_frequency);

    assert(app_start(&global) == HAL_OK);
    assert(app_start(&global) == HAL_OK);
    expect_calls("S");
    assert(app_init(&global, &config) == HAL_BUSY);
    assert(global.state == APP_RUNNING);
    expect_calls("");
    assert(app_set_dds_frequency(&global, 1000U) == HAL_OK);
    expect_calls("FWS");
    assert(global.state == APP_RUNNING && dds.control_word == 0x2002U);
    assert(app_set_dds_frequency(&global, 1000U) == HAL_OK);
    expect_calls("");
    assert(app_set_dds_waveform(&global, AD9833_WAVE_SQUARE) == HAL_OK);
    expect_calls("W");
    assert(dds.control_word == 0x2028U && global.state == APP_RUNNING);
    assert(app_set_dds_frequency(&global, 12500001U) == HAL_ERROR);
    assert(app_set_dds_waveform(&global, (ad9833_waveform_t)3) == HAL_ERROR);
    expect_calls("");
    assert(dds.frequency_hz == 1000U && dds.control_word == 0x2028U);
    assert(app_stop(&global) == HAL_OK);
    assert(app_stop(&global) == HAL_OK);
    expect_calls("T");
    assert(global.state == APP_IDLE && dds.control_word == 0x2128U);

    assert(app_start(&global) == HAL_OK);
    expect_calls("S");
    fail_on_call = 2U;
    assert(app_set_dds_frequency(&global, 10000U) == HAL_TIMEOUT);
    expect_calls("FWT");
    assert(global.state == APP_ERROR && global.last_error == HAL_TIMEOUT);
    assert(dds.frequency_hz == 10000U && (dds.control_word & 0x0100U));
    assert(dds.waveform == AD9833_WAVE_SINE);
    assert(app_start(&global) == HAL_ERROR);
    assert(app_stop(&global) == HAL_ERROR);
    assert(global.last_error == HAL_TIMEOUT);
    expect_calls("");

    assert(app_init(&global, &config) == HAL_OK);
    expect_calls("IP");
    assert(global.state == APP_IDLE && dds.frequency_hz == 200000U);
    assert(dds.waveform == AD9833_WAVE_SINE && global.last_error == HAL_OK);
    fail_on_call = 1U;
    assert(app_start(&global) == HAL_TIMEOUT);
    expect_calls("ST");
    assert(global.state == APP_ERROR && global.last_error == HAL_TIMEOUT);

    fail_on_call = 1U;
    assert(app_init(&global, &config) == HAL_TIMEOUT);
    expect_calls("I");
    assert(global.state == APP_ERROR && !dds.initialized);
    assert(app_init(&global, &config) == HAL_OK);
    expect_calls("IP");
    assert(app_start(&global) == HAL_OK);
    expect_calls("S");
    fail_on_call = 1U;
    assert(app_set_dds_frequency(&global, 1000U) == HAL_TIMEOUT);
    expect_calls("FT");
    assert(global.state == APP_ERROR && global.last_error == HAL_TIMEOUT);
    assert(dds.frequency_hz == 200000U && dds.waveform == AD9833_WAVE_SINE);

    assert(app_init(&global, &config) == HAL_OK);
    expect_calls("IP");
    assert(app_set_mcp4151_position(&global, 64U) == HAL_OK);
    expect_calls("R");
    assert(last_position == 64U && pot.position == 64U);
    fail_on_call = 1U;
    assert(app_set_mcp4151_position(&global, 192U) == HAL_TIMEOUT);
    expect_calls("R");
    assert(global.state == APP_IDLE && pot.position == 64U);
    assert(app_set_mcp4151_position(&global, 192U) == HAL_OK);
    expect_calls("R");
    assert(last_position == 192U && pot.position == 192U);

    puts("APP初始化参数、状态管理、换频恢复、电位器写入及故障处理测试通过。");
    return 0;
}
