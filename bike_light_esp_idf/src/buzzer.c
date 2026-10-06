#include "buzzer.h"
#include "config.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUZZER";

/* PC-speaker / магнитный излучатель: нужен меандр */
#define DEFAULT_FREQ_HZ    2200
#define CLICK_MS           55
#define SIREN_ON_MS        400
#define SIREN_OFF_MS       100
#define SIREN_PAUSE_MS     250
#define SIREN_BURST_COUNT  3

static bool tone_on = false;
static bool click_active = false;
static int64_t click_end_us = 0;

static bool siren_active = false;
static int siren_phase = 0;
static int siren_burst_left = 0;
static int64_t siren_next_us = 0;

/* Настраиваемые параметры (из config / веб) */
static float volume_percent = 100.0f;   /* 0–100 */
static uint16_t tone_freq_hz = DEFAULT_FREQ_HZ;

/* Мелодия */
typedef struct {
    uint16_t freq_hz;   /* 0 = пауза */
    uint16_t dur_ms;
} note_t;

static const note_t *melody_notes = NULL;
static int melody_len = 0;
static int melody_idx = 0;
static int64_t melody_note_end_us = 0;
static bool melody_active = false;

/* --- Внутренние --- */

static uint32_t duty_from_volume(void)
{
    /* 50 % duty = максимум громкости для меандра.
       volume 100 % → duty 128 (из 255), volume 0 → 0 */
    float v = volume_percent;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return (uint32_t)(v * 1.28f + 0.5f);   /* 0…128 */
}

static void tone_start_at(uint16_t hz)
{
    if (hz < 200) hz = 200;
    if (hz > 8000) hz = 8000;
    ledc_set_freq(LEDC_MODE, LEDC_TIMER_1, hz);
    uint32_t duty = duty_from_volume();
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_BUZZER, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_BUZZER);
    tone_on = true;
}

static void tone_start(void)
{
    tone_start_at(tone_freq_hz);
}

static void tone_stop(void)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_BUZZER, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_BUZZER);
    tone_on = false;
}

/* ===== Публичный API ===== */

esp_err_t buzzer_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LEDC_TIMER_1,
        .freq_hz         = DEFAULT_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = PIN_BUZZER,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL_BUZZER,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_1,
        .duty       = 0,
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
    tone_stop();

    /* Подтянуть значения из конфига, если уже загружен */
    volume_percent = g_config.buzzer_volume;
    tone_freq_hz   = g_config.buzzer_freq_hz;
    if (tone_freq_hz < 200) tone_freq_hz = DEFAULT_FREQ_HZ;

    ESP_LOGI(TAG, "Buzzer PWM %d Hz, vol %.0f%% on GPIO%d", tone_freq_hz, volume_percent, PIN_BUZZER);
    return ESP_OK;
}

void buzzer_on(void)
{
    if (!click_active && !melody_active) {
        tone_start();
    }
}

void buzzer_off(void)
{
    if (!click_active && !siren_active && !melody_active) {
        tone_stop();
    }
}

void buzzer_click(void)
{
    /* Короткий щелчок поверх всего (кроме активной мелодии — тогда просто игнор) */
    if (melody_active) return;
    tone_start();
    click_active = true;
    click_end_us = esp_timer_get_time() + (int64_t)CLICK_MS * 1000;
}

void buzzer_siren_start(void)
{
    if (melody_active) buzzer_stop_melody();
    siren_active = true;
    siren_phase = 0;
    siren_burst_left = SIREN_BURST_COUNT;
    siren_next_us = esp_timer_get_time() + (int64_t)SIREN_ON_MS * 1000;
    tone_start();
}

void buzzer_siren_stop(void)
{
    siren_active = false;
    if (!click_active && !melody_active) {
        tone_stop();
    }
}

bool buzzer_siren_is_active(void)
{
    return siren_active;
}

void buzzer_set_volume(float percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    volume_percent = percent;
    if (tone_on) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_BUZZER, duty_from_volume());
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_BUZZER);
    }
}

void buzzer_set_freq(uint16_t hz)
{
    if (hz < 200) hz = 200;
    if (hz > 8000) hz = 8000;
    tone_freq_hz = hz;
    if (tone_on && !melody_active) {
        ledc_set_freq(LEDC_MODE, LEDC_TIMER_1, tone_freq_hz);
    }
}

float buzzer_get_volume(void) { return volume_percent; }
uint16_t buzzer_get_freq(void) { return tone_freq_hz; }

void buzzer_beep(uint32_t duration_ms, float volume_unused)
{
    (void)volume_unused;
    buzzer_siren_stop();
    buzzer_stop_melody();
    tone_start();
    vTaskDelay(pdMS_TO_TICKS(duration_ms > 0 ? duration_ms : 50));
    tone_stop();
}

/* ---------- Мелодии ---------- */

/* 3 коротких щелчка */
static const note_t mel_clicks[] = {
    {2200, 45}, {0, 80}, {2200, 45}, {0, 80}, {2200, 45}
};

/* Imperial March (~15 с) — замена Vertu, id MELODY_VERTU сохранён для NVS */
static const note_t mel_vertu[] = {
    /* основной мотив */
    {392, 420}, {0, 60},
    {392, 420}, {0, 60},
    {392, 420}, {0, 60},
    {311, 300}, {0, 40},
    {466, 140}, {0, 30},
    {392, 420}, {0, 60},
    {311, 300}, {0, 40},
    {466, 140}, {0, 30},
    {392, 700}, {0, 180},
    /* верхний мотив */
    {587, 420}, {0, 60},
    {587, 420}, {0, 60},
    {587, 420}, {0, 60},
    {622, 300}, {0, 40},
    {466, 140}, {0, 30},
    {370, 420}, {0, 60},
    {311, 300}, {0, 40},
    {466, 140}, {0, 30},
    {392, 700}, {0, 200},
    /* короткая реприза */
    {392, 280}, {0, 40},
    {392, 280}, {0, 40},
    {392, 200}, {0, 30},
    {370, 200}, {0, 30},
    {349, 700}
};

/* Восходящая power-on */
static const note_t mel_power_on[] = {
    {523, 120}, {0, 30},
    {659, 120}, {0, 30},
    {784, 120}, {0, 30},
    {1047, 280}
};

/* Нисходящая power-off */
static const note_t mel_power_off[] = {
    {1047, 140}, {0, 30},
    {784, 140}, {0, 30},
    {659, 140}, {0, 30},
    {523, 300}
};

/* Два бипа */
static const note_t mel_beep_beep[] = {
    {1800, 90}, {0, 70}, {1800, 90}
};

/* Короткая сирена-фраза */
static const note_t mel_siren_short[] = {
    {800, 120}, {1200, 120}, {800, 120}, {1200, 120},
    {800, 120}, {1200, 120}, {900, 200}
};

/* Мягкий перезвон */
static const note_t mel_chime[] = {
    {1047, 160}, {0, 40},
    {1319, 160}, {0, 40},
    {1568, 160}, {0, 40},
    {2093, 320}, {0, 80},
    {1568, 200}
};

static const struct {
    const note_t *notes;
    int len;
    const char *name;
} melody_table[] = {
    [MELODY_NONE]        = {NULL, 0, "Нет"},
    [MELODY_CLICKS]      = {mel_clicks, sizeof(mel_clicks)/sizeof(note_t), "Щелчки"},
    [MELODY_VERTU]       = {mel_vertu, sizeof(mel_vertu)/sizeof(note_t), "Имперский марш"},
    [MELODY_POWER_ON]    = {mel_power_on, sizeof(mel_power_on)/sizeof(note_t), "Включение"},
    [MELODY_POWER_OFF]   = {mel_power_off, sizeof(mel_power_off)/sizeof(note_t), "Выключение"},
    [MELODY_BEEP_BEEP]   = {mel_beep_beep, sizeof(mel_beep_beep)/sizeof(note_t), "Два бипа"},
    [MELODY_SIREN_SHORT] = {mel_siren_short, sizeof(mel_siren_short)/sizeof(note_t), "Сирена"},
    [MELODY_CHIME]       = {mel_chime, sizeof(mel_chime)/sizeof(note_t), "Перезвон"},
};

const char *buzzer_melody_name(melody_id_t id)
{
    if (id < 0 || id >= MELODY_COUNT) return "?";
    return melody_table[id].name;
}

void buzzer_play_melody(melody_id_t id)
{
    if (id <= MELODY_NONE || id >= MELODY_COUNT) {
        buzzer_stop_melody();
        return;
    }
    buzzer_siren_stop();
    click_active = false;

    melody_notes = melody_table[id].notes;
    melody_len   = melody_table[id].len;
    melody_idx   = 0;
    melody_active = true;

    /* Первая нота */
    const note_t *n = &melody_notes[0];
    if (n->freq_hz > 0) {
        tone_start_at(n->freq_hz);
    } else {
        tone_stop();
    }
    melody_note_end_us = esp_timer_get_time() + (int64_t)n->dur_ms * 1000;
    ESP_LOGI(TAG, "Play melody %s (%d notes)", melody_table[id].name, melody_len);
}

void buzzer_stop_melody(void)
{
    if (!melody_active) return;
    melody_active = false;
    melody_notes = NULL;
    melody_len = 0;
    melody_idx = 0;
    tone_stop();
}

bool buzzer_melody_is_playing(void)
{
    return melody_active;
}

void buzzer_update(void)
{
    int64_t now = esp_timer_get_time();

    /* Мелодия имеет приоритет */
    if (melody_active) {
        if (now >= melody_note_end_us) {
            melody_idx++;
            if (melody_idx >= melody_len) {
                buzzer_stop_melody();
                return;
            }
            const note_t *n = &melody_notes[melody_idx];
            if (n->freq_hz > 0) {
                tone_start_at(n->freq_hz);
            } else {
                tone_stop();
            }
            melody_note_end_us = now + (int64_t)n->dur_ms * 1000;
        }
        return;
    }

    if (click_active) {
        if (now >= click_end_us) {
            click_active = false;
            if (siren_active && siren_phase == 0) {
                tone_start();
            } else {
                tone_stop();
            }
        }
        return;
    }

    if (!siren_active) return;
    if (now < siren_next_us) return;

    switch (siren_phase) {
        case 0:
            tone_stop();
            siren_burst_left--;
            if (siren_burst_left > 0) {
                siren_phase = 1;
                siren_next_us = now + (int64_t)SIREN_OFF_MS * 1000;
            } else {
                siren_phase = 2;
                siren_next_us = now + (int64_t)SIREN_PAUSE_MS * 1000;
            }
            break;
        case 1:
            tone_start();
            siren_phase = 0;
            siren_next_us = now + (int64_t)SIREN_ON_MS * 1000;
            break;
        case 2:
            tone_start();
            siren_burst_left = SIREN_BURST_COUNT;
            siren_phase = 0;
            siren_next_us = now + (int64_t)SIREN_ON_MS * 1000;
            break;
        default:
            siren_phase = 0;
            break;
    }
}
