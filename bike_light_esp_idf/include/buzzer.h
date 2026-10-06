#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t buzzer_init(void);

/** Полностью включить / выключить */
void buzzer_on(void);
void buzzer_off(void);

/** Короткий щелчок «как реле» (~45–70 мс). Неблокирующий. */
void buzzer_click(void);

/**
 * Сирена в стиле автомобильной сигнализации.
 * Вызывать start при срабатывании, stop при снятии.
 * В loop периодически вызывать buzzer_update().
 */
void buzzer_siren_start(void);
void buzzer_siren_stop(void);
bool buzzer_siren_is_active(void);

/** Обслуживание кликов, сирены и мелодий — вызывать из основного цикла */
void buzzer_update(void);

/** Короткий гудок (блокирующий) — для совместимости */
void buzzer_beep(uint32_t duration_ms, float volume_unused);

/**
 * Громкость 0–100 %. Для PC-speaker (магнитный) влияет на duty cycle.
 * 100 % = 50 % меандр (максимум громкости), ниже — quieter.
 */
void buzzer_set_volume(float percent);

/** Частота тона в Гц (400–5000). Меняет высоту звука. */
void buzzer_set_freq(uint16_t hz);

/** Текущие значения (для веб-интерфейса) */
float buzzer_get_volume(void);
uint16_t buzzer_get_freq(void);

/* ===== Мелодии ===== */
typedef enum {
    MELODY_NONE = 0,
    MELODY_CLICKS,      /* 3 коротких щелчка */
    MELODY_VERTU,       /* Imperial March (~15 с), id=2 сохранён для NVS */
    MELODY_POWER_ON,    /* восходящая */
    MELODY_POWER_OFF,   /* нисходящая */
    MELODY_BEEP_BEEP,   /* два бипа */
    MELODY_SIREN_SHORT, /* короткая сирена-фраза */
    MELODY_CHIME,       /* мягкий перезвон */
    MELODY_COUNT
} melody_id_t;

/** Запустить мелодию (неблокирующе). Останавливает предыдущую. */
void buzzer_play_melody(melody_id_t id);

/** Остановить текущую мелодию */
void buzzer_stop_melody(void);

bool buzzer_melody_is_playing(void);

/** Имя мелодии для UI (статическая строка) */
const char *buzzer_melody_name(melody_id_t id);
