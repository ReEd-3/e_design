# AD9833驱动使用说明

当前使用GPIO模拟SPI，基础引脚配置与e_design.ioc保持一致：

| STM32引脚 | MX标签 | AD9833引脚 | 配置 |
|---|---|---|---|
| PA5 | DDS_SCLK | SCLK | 推挽输出、无上下拉、低速、初始高 |
| PA7 | DDS_SDATA | SDATA | 推挽输出、无上下拉、低速、初始低 |
| PB0 | DDS_CS | FSYNC | 推挽输出、无上下拉、低速、初始高 |

AD9833沿用原软件SPI接线。硬件SPI1现独立配置到PB3/SCK、PB5/MOSI，供后续电位器驱动使用，
已加入MX、main初始化和构建；PB1/POT_CS初始高。PA6为硬件SPI1的MISO，当前只写位置时不用连接。
硬件SPI1采用模式0、256分频、GPIO Low，与DDS的PA5、PA7软件SPI互不占用。
模块DGND、AGND与STM32共地，示波器测OUT相对AGND。

## 五个接口

| 接口 | 作用 |
|---|---|
| ad9833_init | 接收ad9833_init_t，配置接口、初始频率、初始波形和零相位，保持停止 |
| ad9833_start | 清除RESET，开始输出 |
| ad9833_stop | 设置RESET，停止交流输出 |
| ad9833_set_frequency | 先写0x2100，再写整数Hz频率，范围0至MCLK/2，完成后保持停止 |
| ad9833_set_waveform | 选择正弦、三角或方波 |

设置频率会恢复正弦模式并保持RESET，随后设置所需波形、调用start才输出。
设置波形会保留启停状态，方波频率与设置值相同。停止时输出停在中间电平，
不等于0 V。仅使用频率寄存器0和相位寄存器0。

ad9833_t句柄负责保存frequency_hz和waveform，设置成功后由驱动更新。
初始化记录配置指定的频率与波形；设置频率记录新频率并将waveform恢复正弦；设置波形仅更新waveform。
启停不改变这两个配置值，非法参数也不改变它们。APP通过DDS句柄读取，不重复缓存。
frequency_hz表示配置的整数Hz值，不是考虑晶振偏差后测得的实际频率，两个字段均不来自硬件读回。

## 调用示例

用户已确认模块振荡器丝印为25 MHz，示例按此配置。
以下为独立使用驱动的示例；先调用MX_GPIO_Init，再在USER CODE区域调用。
当前main已通过APP层管理DDS，初始化结构体用法见`App.md`。

```c
#include "ad9833.h"

static ad9833_t dds;
const ad9833_init_t config = {
    .sclk_port = DDS_SCLK_GPIO_Port,
    .sclk_pin = DDS_SCLK_Pin,
    .sdata_port = DDS_SDATA_GPIO_Port,
    .sdata_pin = DDS_SDATA_Pin,
    .cs_port = DDS_CS_GPIO_Port,
    .cs_pin = DDS_CS_Pin,
    .mclk_hz = 25000000U,
    .initial_frequency_hz = 1000U,
    .initial_waveform = AD9833_WAVE_SINE
};

// GPIO初始化完成后设置1 kHz正弦并启动。
if (ad9833_init(&dds, &config) != HAL_OK ||
    ad9833_start(&dds) != HAL_OK) {
    Error_Handler();
}
```

初始化时保持FSYNC、SCLK高电平，SDATA低电平，等待10 ms。
驱动先将频率和相位归零，再复用设置接口写入指定的初始频率与波形，保持RESET。
初始化参数由驱动层定义和校验，APP只组合并转交配置；驱动不保存config指针。
内部只有一个16位发送函数：拉低FSYNC，最高位先发，每位先设置SDATA，
等待后拉低SCLK，再等待后拉高SCLK；16个完整时钟结束后释放FSYNC。
各时钟半周期调用HAL_Delay(1)，用于低速排查，实际时长受HAL tick影响。
这些延时只影响参数写入速度，DDS输出频率由频率字和模块MCLK决定。

## 返回值与调用条件

函数返回HAL_StatusTypeDef：参数或初始化状态不正确时返回HAL_ERROR，
GPIO写入流程完成时返回HAL_OK。HAL_GPIO_WritePin没有通信应答，
initialized和control_word只是软件状态，不能证明AD9833已接收或实际输出正常。

使用前将对象置零，初始化后不要直接修改其字段。
在主循环中串行调用，HAL tick必须正常运行，不在中断中调用这些阻塞函数。
三个引脚由MX配置为普通推挽输出；现有MX_SPI1_Init使用独立的PB3、PB5，不占用DDS引脚。
修改引脚配置后应重新初始化驱动并设置参数。

## 验证

软件测试命令：

```sh
cc -std=c11 -Wall -Wextra -Werror -Itests/stubs -ICore/Inc \
  Core/Src/ad9833.c tests/test_ad9833.c -o /tmp/e_design_ad9833_tests
/tmp/e_design_ad9833_tests
```

测试按GPIO下降沿重建接收字，检查片选边界、每帧16个完整时钟、
数据建立时间、初始等待、频率编码和五个接口的行为；不能代替实物测量。

上面的独立驱动示例设置1 kHz，对应频率帧0x69F1和0x4000。
当前main的APP初始化参数为100 kHz，对应频率帧0x624E和0x4041，启动控制字为0x2000。
保留输出3秒、停止3秒、再次启动流程，主循环不重复发送。
烧录后测OUT相对AGND，独立1 kHz示例可用200 μs/div观察1 ms周期；
当前100 kHz配置可用2 μs/div观察10 μs周期。
再按实际幅度调整垂直量程；探头实物档位与示波器设置一致。
用户此前实测GPIO版本能输出约1 kHz正弦，但报告STM32通电时显示晃动、偶尔跳变，
STM32断电后稳定。两模块已共地，干扰路径尚未确定。
用户进一步报告：连接STM32与AD9833的共地线时干扰明显，偶有显示相位跳变。
按用户要求已撤回空闲SCLK低电平修改，恢复空闲高电平；接地干扰路径尚待验证。
用户最新已确认波形正常，最终处理措施未说明，不能记为已通过某项特定改动解决。
