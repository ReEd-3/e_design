#include "ad9833.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#define CLOCK_PIN 0x0020U // 模拟PA5时钟
#define DATA_PIN  0x0080U // 模拟PA7数据
#define CS_PIN    0x0001U // 模拟PB0片选

static GPIO_TypeDef gpio_a, gpio_b;
static uint16_t words[128];
static unsigned word_count, bit_count, rising_count, startup_delays;
static uint16_t received;
static uint32_t elapsed_ms, data_changed_ms, clock_changed_ms;
static bool selected;

// 累计等待时间，确认上电等待发生在片选高电平下。
void HAL_Delay(uint32_t delay)
{
    assert(delay == 1U || delay == 10U);
    if (delay == 10U) {
        assert(!selected && (gpio_a.levels & CLOCK_PIN));
        ++startup_delays;
    }
    elapsed_ms += delay;
}

// 在下降沿按实物协议采样，检查每帧16位、数据稳定和完整时钟脉冲。
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    assert((port == &gpio_a && (pin == CLOCK_PIN || pin == DATA_PIN)) ||
           (port == &gpio_b && pin == CS_PIN));
    bool was_high = (port->levels & pin) != 0U;
    if (port == &gpio_b && pin == CS_PIN) {
        if (state == GPIO_PIN_RESET) {
            assert(!selected && (gpio_a.levels & CLOCK_PIN));
            assert(startup_delays > 0U);
            selected = true;
            bit_count = rising_count = 0U;
            received = 0U;
        } else if (selected) {
            assert(bit_count == 16U && rising_count == 16U);
            assert(gpio_a.levels & CLOCK_PIN);
            assert(elapsed_ms - clock_changed_ms >= 1U);
            assert(word_count < sizeof(words) / sizeof(words[0]));
            words[word_count++] = received;
            selected = false;
        }
    } else if (pin == DATA_PIN && selected) {
        assert(gpio_a.levels & CLOCK_PIN);
        data_changed_ms = elapsed_ms;
    } else if (pin == CLOCK_PIN && selected) {
        if (was_high && state == GPIO_PIN_RESET) {
            assert(elapsed_ms - data_changed_ms >= 1U);
            assert(elapsed_ms - clock_changed_ms >= 1U);
            assert(bit_count < 16U);
            received = (uint16_t)((received << 1U) |
                                  ((gpio_a.levels & DATA_PIN) ? 1U : 0U));
            ++bit_count;
            clock_changed_ms = elapsed_ms;
        } else if (!was_high && state == GPIO_PIN_SET) {
            assert(elapsed_ms - clock_changed_ms >= 1U);
            ++rising_count;
            clock_changed_ms = elapsed_ms;
        }
    }
    if (state == GPIO_PIN_SET) {
        port->levels |= pin;
    } else {
        port->levels &= (uint16_t)~pin;
    }
}

// 检查最后一个16位帧和发送后的空闲电平。
static void expect_word(uint16_t word)
{
    assert(word_count > 0U && words[word_count - 1U] == word);
    assert(!selected && (gpio_b.levels & CS_PIN) && (gpio_a.levels & CLOCK_PIN));
}

// 检查设置频率前的0x2100及随后低高14位帧的顺序。
static void expect_frequency(uint16_t low, uint16_t high)
{
    assert(word_count >= 3U && words[word_count - 3U] == 0x2100U);
    assert(words[word_count - 2U] == low);
    expect_word(high);
}

// 验证GPIO边沿、五个接口、频率编码和非法参数不发送数据。
int main(void)
{
    ad9833_t dev = {0};
    ad9833_init_t config = {
        .sclk_port = &gpio_a, .sclk_pin = CLOCK_PIN,
        .sdata_port = &gpio_a, .sdata_pin = DATA_PIN,
        .cs_port = &gpio_b, .cs_pin = CS_PIN,
        .mclk_hz = 25000000U,
        .initial_frequency_hz = 0U,
        .initial_waveform = AD9833_WAVE_SINE
    };
    gpio_a.levels = CLOCK_PIN;
    gpio_b.levels = CS_PIN;
    assert(ad9833_start(&dev) == HAL_ERROR);
    assert(ad9833_stop(NULL) == HAL_ERROR);
    assert(ad9833_set_frequency(&dev, 1000U) == HAL_ERROR);
    assert(ad9833_set_waveform(&dev, AD9833_WAVE_SINE) == HAL_ERROR);
    assert(ad9833_init(NULL, &config) == HAL_ERROR);
    assert(ad9833_init(&dev, NULL) == HAL_ERROR);
    config.sclk_port = NULL;
    assert(ad9833_init(&dev, &config) == HAL_ERROR);
    config.sclk_port = &gpio_a;
    config.sclk_pin = 3U;
    assert(ad9833_init(&dev, &config) == HAL_ERROR);
    config.sclk_pin = CLOCK_PIN;
    config.sdata_pin = CLOCK_PIN;
    assert(ad9833_init(&dev, &config) == HAL_ERROR);
    config.sdata_pin = DATA_PIN;
    config.mclk_hz = 0U;
    assert(ad9833_init(&dev, &config) == HAL_ERROR);
    config.mclk_hz = 25000001U;
    assert(ad9833_init(&dev, &config) == HAL_ERROR);
    config.mclk_hz = 25000000U;
    config.initial_frequency_hz = 12500001U;
    assert(ad9833_init(&dev, &config) == HAL_ERROR);
    config.initial_frequency_hz = 0U;
    config.initial_waveform = (ad9833_waveform_t)3;
    assert(ad9833_init(&dev, &config) == HAL_ERROR);
    config.initial_waveform = AD9833_WAVE_SINE;
    assert(word_count == 0U && elapsed_ms == 0U && !dev.initialized);

    assert(ad9833_init(&dev, &config) == HAL_OK);
    const uint16_t initial[] = {
        0x0100, 0x2100, 0x4000, 0x4000, 0xC000,
        0x2100, 0x4000, 0x4000, 0x2100
    };
    assert(word_count == 9U && startup_delays == 1U);
    for (unsigned i = 0U; i < 9U; ++i) {
        assert(words[i] == initial[i]);
    }
    assert(dev.initialized && dev.control_word == 0x2100U);
    assert(dev.frequency_hz == 0U && dev.waveform == AD9833_WAVE_SINE);

    assert(ad9833_set_frequency(&dev, 1000U) == HAL_OK);
    expect_frequency(0x69F1, 0x4000);
    assert(dev.frequency_hz == 1000U && dev.waveform == AD9833_WAVE_SINE);
    assert(ad9833_set_waveform(&dev, AD9833_WAVE_TRIANGLE) == HAL_OK);
    expect_word(0x2102);
    assert(ad9833_start(&dev) == HAL_OK); expect_word(0x2002);
    assert(dev.frequency_hz == 1000U && dev.waveform == AD9833_WAVE_TRIANGLE);
    unsigned running_words = word_count;
    assert(ad9833_set_frequency(&dev, 12500001U) == HAL_ERROR);
    assert(ad9833_set_waveform(&dev, (ad9833_waveform_t)3) == HAL_ERROR);
    assert(word_count == running_words && dev.control_word == 0x2002U);
    assert(dev.frequency_hz == 1000U && dev.waveform == AD9833_WAVE_TRIANGLE);
    assert(ad9833_set_frequency(&dev, 10000U) == HAL_OK);
    expect_frequency(0x636E, 0x4006);
    assert(dev.control_word == 0x2100U);
    assert(dev.frequency_hz == 10000U && dev.waveform == AD9833_WAVE_SINE);
    assert(ad9833_start(&dev) == HAL_OK); expect_word(0x2000);
    assert(ad9833_set_frequency(&dev, 100000U) == HAL_OK);
    expect_frequency(0x624E, 0x4041);
    assert(ad9833_start(&dev) == HAL_OK); expect_word(0x2000);
    assert(ad9833_set_waveform(&dev, AD9833_WAVE_SQUARE) == HAL_OK);
    expect_word(0x2028);
    assert(ad9833_stop(&dev) == HAL_OK); expect_word(0x2128);
    assert(dev.frequency_hz == 100000U && dev.waveform == AD9833_WAVE_SQUARE);
    assert(ad9833_set_waveform(&dev, AD9833_WAVE_SINE) == HAL_OK);
    expect_word(0x2100);
    assert(ad9833_set_frequency(&dev, 0U) == HAL_OK);
    expect_frequency(0x4000, 0x4000);
    assert(dev.frequency_hz == 0U && dev.waveform == AD9833_WAVE_SINE);
    assert(ad9833_set_frequency(&dev, 12500000U) == HAL_OK);
    expect_frequency(0x4000, 0x6000);
    assert(dev.frequency_hz == 12500000U && dev.waveform == AD9833_WAVE_SINE);

    unsigned before = word_count;
    assert(ad9833_set_frequency(&dev, 12500001U) == HAL_ERROR);
    assert(ad9833_set_waveform(&dev, (ad9833_waveform_t)3) == HAL_ERROR);
    assert(word_count == before && dev.initialized);
    assert(dev.frequency_hz == 12500000U && dev.waveform == AD9833_WAVE_SINE);
    config.initial_frequency_hz = 100000U;
    config.initial_waveform = AD9833_WAVE_TRIANGLE;
    before = word_count;
    assert(ad9833_init(&dev, &config) == HAL_OK);
    assert(word_count == before + 9U);
    assert(words[before + 5U] == 0x2100U);
    assert(words[before + 6U] == 0x624EU && words[before + 7U] == 0x4041U);
    expect_word(0x2102);
    assert(dev.initialized && dev.control_word == 0x2102U);
    assert(dev.frequency_hz == 100000U && dev.waveform == AD9833_WAVE_TRIANGLE);
    config.initial_frequency_hz = 200000U;
    config.initial_waveform = AD9833_WAVE_SQUARE;
    assert(dev.frequency_hz == 100000U && dev.waveform == AD9833_WAVE_TRIANGLE);
    puts("AD9833初始化结构体、逐位时序、启停、频率及波形测试通过。");
    return 0;
}
