#ifndef APP_H
#define APP_H

#include "ad9833.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_UNINITIALIZED = 0U, // 尚未完成初始化
    APP_IDLE = 1U,          // 待机，DDS保持停止
    APP_RUNNING = 2U,       // DDS正在输出
    APP_ERROR = 3U          // 部件操作失败，需要重新初始化
} app_state_t;

typedef struct {
    ad9833_t *dds_src;       // 由调用者提供的DDS对象
    ad9833_init_t dds_init;  // 驱动层定义的DDS初始化参数
} app_init_t;

typedef struct {
    ad9833_t *dds_src;         // 应用管理的DDS对象
    app_state_t state;         // 应用工作状态
    HAL_StatusTypeDef last_error; // 最近操作结果，错误状态下保留故障原因
} global_t;

// 按初始化参数配置部件，成功后保持待机，两个对象使用前须置零。
HAL_StatusTypeDef app_init(global_t *global, const app_init_t *config);

// 开始输出应用设置的波形。
HAL_StatusTypeDef app_start(global_t *global);

// 停止交流输出并进入待机状态。
HAL_StatusTypeDef app_stop(global_t *global);

// 修改频率并恢复所选波形及修改前的启停状态。
HAL_StatusTypeDef app_set_dds_frequency(global_t *global, uint32_t frequency_hz);

// 修改输出波形并保留启停状态。
HAL_StatusTypeDef app_set_dds_waveform(global_t *global, ad9833_waveform_t waveform);

#ifdef __cplusplus
}
#endif

#endif
