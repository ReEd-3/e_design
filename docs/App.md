# APP层使用说明

当前APP只管理AD9833，源码为`Core/Inc/app.h`、`Core/Src/app.c`。
沿用调用者提供DDS对象、`global_t`保存部件指针的设计；基础GPIO由MX配置。

## 初始化参数

`app_init(global_t *global, const app_init_t *config)`接收应用对象和初始化参数。

| 参数 | 作用 |
|---|---|
| dds_src | 调用者提供的AD9833对象 |
| dds_init | 驱动层ad9833_init_t类型的DDS初始化参数 |

ad9833_init_t在ad9833.h中定义GPIO、MCLK、initial_frequency_hz和initial_waveform。
设备参数由驱动校验并应用，APP只负责组合参数及调用初始化，不再展开定义DDS参数。
APP和驱动均不保存config指针。DDS对象的生命周期须覆盖APP使用期间。
运行配置统一由DDS句柄记录，APP不重复保存。
`global_t`和DDS对象使用前置零，初始化成功后不直接修改这些对象，也不绕过APP调用驱动。

```c
#include "app.h"

static ad9833_t dds;
static global_t global;

const app_init_t config = {
    .dds_src = &dds,
    .dds_init = {
        .sclk_port = DDS_SCLK_GPIO_Port,
        .sclk_pin = DDS_SCLK_Pin,
        .sdata_port = DDS_SDATA_GPIO_Port,
        .sdata_pin = DDS_SDATA_Pin,
        .cs_port = DDS_CS_GPIO_Port,
        .cs_pin = DDS_CS_Pin,
        .mclk_hz = 25000000U,
        .initial_frequency_hz = 100000U,
        .initial_waveform = AD9833_WAVE_SINE
    }
};

// 在MX_GPIO_Init完成后初始化应用，再启动输出。
if (app_init(&global, &config) != HAL_OK || app_start(&global) != HAL_OK) {
    Error_Handler();
}
```

APP只调用ad9833_init完成DDS的接口、频率和波形初始化，成功后为APP_IDLE，不会自动输出。
运行中再次初始化返回HAL_BUSY；先停止输出再重新初始化。

## 接口行为

| 接口 | 行为 |
|---|---|
| app_init | 根据初始化结构体配置部件，成功后待机 |
| app_start | 开始输出，已运行时直接返回HAL_OK |
| app_stop | 停止交流输出，已待机时直接返回HAL_OK |
| app_set_dds_frequency | 换频后恢复所选波形，原来运行则重新启动，否则保持待机 |
| app_set_dds_waveform | 修改波形，保留启停状态 |

相同频率、相同波形和重复启停请求不重复发送。
频率、波形参数无效时返回HAL_ERROR，保留原输出与DDS句柄配置。
换频内部使用现有驱动的RESET流程，会短暂停止输出，不能承诺相位连续。

## 状态与错误

`global.state`有未初始化、待机、运行、错误四种状态。
global_t只保存DDS指针、应用状态和错误。
频率通过`global.dds_src->frequency_hz`读取，波形通过`global.dds_src->waveform`读取，
由AD9833驱动在设置成功后更新。APP换频前从句柄临时记住原波形，再完成恢复。

部件操作失败后，APP尝试停止DDS，进入`APP_ERROR`并通过`last_error`保留原始错误。
后续启停和参数修改会被拒绝，须重新调用app_init。
如果频率写入成功而恢复波形失败，句柄频率记录新值，波形仍为驱动设置频率后的正弦。
APP错误不会把这些字段回滚成旧配置。句柄是软件记录而非硬件读回，尝试停止也不等于已实测停止。

接口在主循环中串行调用，不在中断中调用。现有软件SPI使用HAL_Delay，APP接口也会阻塞，
HAL tick须正常运行。HAL_OK仍只表示软件流程完成，不表示已读取芯片寄存器或验证模拟输出。

## 当前main与验证

当前main通过初始化结构体设置100 kHz正弦，保留输出3秒、停止3秒、再次持续输出的流程。
main不再直接调用AD9833接口，主循环不重复发控制字。
原先创建的add.h已统一为app.h，CMake已同步；AD9833通信时序与MX配置保持原样。
AD9833驱动增加频率和波形记录，APP不再重复持有这两个字段。

APP测试用模拟部件验证初始化参数转交、频率修改后的波形与启停恢复、参数拒绝和故障处理：

```sh
cc -std=c11 -Wall -Wextra -Werror -Itests/stubs -ICore/Inc \
  Core/Src/app.c tests/test_app.c -o /tmp/e_design_app_tests
/tmp/e_design_app_tests
```

另用原有`tests/test_ad9833.c`检查实际驱动的GPIO时序。
本次APP迁移已通过ARM编译和软件测试，尚未烧录验证。

## 电位器硬件接口配置

SPI1专用于后续MCP4151驱动：PB3为SCK、PB5为MOSI，PB1/POT_CS为推挽片选且初始高。
PA6保留为MISO，当前只写位置时不连接。SPI1为主机、模式0、8位、MSB先发、软件NSS，
256分频对应328.125 kHz，GPIO输出速度为Low，TI与CRC关闭。
MX配置和生成代码已同步；main先初始化GPIO使两个片选为高，再初始化SPI1，随后初始化APP。
AD9833继续使用PA5、PA7的软件SPI。当前尚无MCP4151驱动，APP也未加入电位器对象。
