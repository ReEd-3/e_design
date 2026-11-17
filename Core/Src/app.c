#include "app.h"
#include <stddef.h>

// 检查应用是否已初始化且允许操作部件。
static HAL_StatusTypeDef app_check_ready(global_t *global)
{
    if (global == NULL) {
        return HAL_ERROR;
    }
    if (global->dds_src == NULL || !global->dds_src->initialized ||
        (global->state != APP_IDLE && global->state != APP_RUNNING)) {
        if (global->state != APP_ERROR) {
            global->last_error = HAL_ERROR;
        }
        return HAL_ERROR;
    }
    return HAL_OK;
}

// 部件操作失败后尝试停止DDS，并保留原始错误。
static HAL_StatusTypeDef app_set_error(global_t *global, HAL_StatusTypeDef status)
{
    if (global->dds_src != NULL && global->dds_src->initialized) {
        (void)ad9833_stop(global->dds_src);
    }
    global->state = APP_ERROR;
    global->last_error = status;
    return status;
}

// 绑定部件对象并转交驱动初始化参数，完成后保持待机。
HAL_StatusTypeDef app_init(global_t *global, const app_init_t *config)
{
    if (global == NULL) {
        return HAL_ERROR;
    }
    if (global->state == APP_RUNNING) {
        global->last_error = HAL_BUSY;
        return HAL_BUSY;
    }
    if (config == NULL || config->dds_src == NULL) {
        if (global->state != APP_ERROR) {
            global->last_error = HAL_ERROR;
        }
        return HAL_ERROR;
    }

    global->dds_src = config->dds_src;
    global->state = APP_UNINITIALIZED;
    HAL_StatusTypeDef status = ad9833_init(global->dds_src, &config->dds_init);
    if (status != HAL_OK) {
        return app_set_error(global, status);
    }

    global->state = APP_IDLE;
    global->last_error = HAL_OK;
    return HAL_OK;
}

// 开始输出，已经运行时不重复发送控制字。
HAL_StatusTypeDef app_start(global_t *global)
{
    if (app_check_ready(global) != HAL_OK) {
        return HAL_ERROR;
    }
    if (global->state == APP_IDLE) {
        HAL_StatusTypeDef status = ad9833_start(global->dds_src);
        if (status != HAL_OK) {
            return app_set_error(global, status);
        }
        global->state = APP_RUNNING;
    }
    global->last_error = HAL_OK;
    return HAL_OK;
}

// 停止输出，已经待机时不重复发送控制字。
HAL_StatusTypeDef app_stop(global_t *global)
{
    if (app_check_ready(global) != HAL_OK) {
        return HAL_ERROR;
    }
    if (global->state == APP_RUNNING) {
        HAL_StatusTypeDef status = ad9833_stop(global->dds_src);
        if (status != HAL_OK) {
            return app_set_error(global, status);
        }
        global->state = APP_IDLE;
    }
    global->last_error = HAL_OK;
    return HAL_OK;
}

// 设置新频率，恢复原波形，并在原来运行时重新启动输出。
HAL_StatusTypeDef app_set_dds_frequency(global_t *global, uint32_t frequency_hz)
{
    if (app_check_ready(global) != HAL_OK) {
        return HAL_ERROR;
    }
    if (frequency_hz > global->dds_src->mclk_hz / 2U) {
        global->last_error = HAL_ERROR;
        return HAL_ERROR;
    }
    if (frequency_hz == global->dds_src->frequency_hz) {
        global->last_error = HAL_OK;
        return HAL_OK;
    }

    ad9833_waveform_t waveform = global->dds_src->waveform;
    HAL_StatusTypeDef status = ad9833_set_frequency(global->dds_src, frequency_hz);
    if (status == HAL_OK) {
        status = ad9833_set_waveform(global->dds_src, waveform);
    }
    if (status == HAL_OK && global->state == APP_RUNNING) {
        status = ad9833_start(global->dds_src);
    }
    if (status != HAL_OK) {
        return app_set_error(global, status);
    }

    global->last_error = HAL_OK;
    return HAL_OK;
}

// 设置所选波形，参数无效时保留原有输出。
HAL_StatusTypeDef app_set_dds_waveform(global_t *global, ad9833_waveform_t waveform)
{
    if (app_check_ready(global) != HAL_OK) {
        return HAL_ERROR;
    }
    if (waveform != AD9833_WAVE_SINE && waveform != AD9833_WAVE_TRIANGLE &&
        waveform != AD9833_WAVE_SQUARE) {
        global->last_error = HAL_ERROR;
        return HAL_ERROR;
    }
    if (waveform == global->dds_src->waveform) {
        global->last_error = HAL_OK;
        return HAL_OK;
    }

    HAL_StatusTypeDef status = ad9833_set_waveform(global->dds_src, waveform);
    if (status != HAL_OK) {
        return app_set_error(global, status);
    }
    global->last_error = HAL_OK;
    return HAL_OK;
}
