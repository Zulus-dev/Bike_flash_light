#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Фоторезистор GL5528 на GPIO0 (ADC1_CH0).
 * Делитель: +3.3V — 10k —●— GPIO0 — GL5528 — GND + 100nF.
 * Чем темнее → выше сырое значение АЦП → выше percent (0–100).
 */

esp_err_t ldr_init(void);

/** Периодическое обновление (вызывать из main loop ~50 Гц). */
void ldr_update(void);

/** true = сейчас считается «темно» (с учётом гистерезиса). */
bool ldr_is_dark(void);

/** Текущий уровень «темноты» 0–100 % (0 = ярко, 100 = полная темнота). */
uint8_t ldr_get_percent(void);

/** Сырое усреднённое значение АЦП 0–4095 (для веб-статуса). */
uint16_t ldr_get_raw(void);

/**
 * Масштаб яркости 0.0–1.0 в зависимости от режима и освещённости.
 * Используется для STRIP_MODE_SCALE.
 */
float ldr_get_brightness_scale(void);
