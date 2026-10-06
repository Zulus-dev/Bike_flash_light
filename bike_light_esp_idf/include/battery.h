#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Мониторинг 1S Li-ion (GPIO1, ADC1_CHANNEL_1).
 * АЦП — через общий модуль adc1_shared (один ADC_UNIT_1 на всю плату).
 */

esp_err_t battery_init(void);
void battery_update(void);
float battery_get_voltage(void);
uint8_t battery_get_percent(void);
bool battery_is_low(void);
