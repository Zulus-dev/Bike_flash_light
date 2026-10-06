#pragma once

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Один общий oneshot-блок ADC_UNIT_1 для всех аналоговых входов платы.
 * Каналы:
 *   GPIO0 → ADC1_CHANNEL_0  (фоторезистор)
 *   GPIO1 → ADC1_CHANNEL_1  (батарея)
 *
 * Нельзя вызывать adc_oneshot_new_unit(ADC_UNIT_1) второй раз — только этот модуль.
 */

esp_err_t adc1_shared_init(void);

/** true после успешного adc1_shared_init() */
bool adc1_shared_ready(void);

/**
 * Настроить канал (atten/bitwidth). Можно вызывать один раз на канал.
 * chan: ADC_CHANNEL_0 или ADC_CHANNEL_1
 */
esp_err_t adc1_shared_config_channel(adc_channel_t chan, adc_atten_t atten);

/** Одно измерение raw 0…4095. Возвращает ESP_OK при успехе. */
esp_err_t adc1_shared_read_raw(adc_channel_t chan, int *raw_out);

/** Handle только для калибровки батареи (curve fitting). */
adc_oneshot_unit_handle_t adc1_shared_handle(void);
