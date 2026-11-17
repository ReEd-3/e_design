#ifndef AD9833_TEST_HAL_H
#define AD9833_TEST_HAL_H
#include <stdint.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint16_t levels; } GPIO_TypeDef;

typedef struct { uint32_t unused; } SPI_TypeDef;
typedef struct {
    uint32_t Mode, Direction, DataSize, CLKPolarity, CLKPhase, NSS;
    uint32_t FirstBit, TIMode, CRCCalculation;
} SPI_InitTypeDef;
typedef enum {
    HAL_SPI_STATE_RESET = 0U,
    HAL_SPI_STATE_READY = 1U,
    HAL_SPI_STATE_BUSY_TX = 3U
} HAL_SPI_StateTypeDef;
typedef struct {
    SPI_TypeDef *Instance;
    SPI_InitTypeDef Init;
    HAL_SPI_StateTypeDef State;
} SPI_HandleTypeDef;

#define SPI_MODE_MASTER 1U               // 模拟SPI主机模式
#define SPI_DIRECTION_2LINES 0U          // 模拟全双工模式
#define SPI_DATASIZE_8BIT 8U             // 模拟8位数据帧
#define SPI_FIRSTBIT_MSB 0U              // 模拟最高位先发
#define SPI_POLARITY_LOW 0U              // 模拟时钟空闲低电平
#define SPI_PHASE_1EDGE 0U               // 模拟第一个边沿采样
#define SPI_NSS_SOFT 1U                  // 模拟软件片选管理
#define SPI_TIMODE_DISABLE 0U            // 模拟关闭TI模式
#define SPI_CRCCALCULATION_DISABLE 0U    // 模拟关闭CRC

// 记录等待时间并检查数据建立时间和时钟脉宽。
void HAL_Delay(uint32_t delay);
// 模拟GPIO电平并在时钟下降沿采样数据。
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
// 返回模拟SPI状态。
HAL_SPI_StateTypeDef HAL_SPI_GetState(const SPI_HandleTypeDef *hspi);
// 记录发送字节并模拟传输返回值。
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, const uint8_t *data,
                                   uint16_t size, uint32_t timeout);
#endif
