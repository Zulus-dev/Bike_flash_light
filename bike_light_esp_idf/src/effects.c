#include "effects.h"
#include "config.h"
#include "led_control.h"
#include "buzzer.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "FX";

static effect_type_t current = EFFECT_NONE;
static int64_t start_us = 0;
static int current_cycle = 0;
static int total_cycles = 0;
static float cycle_dur = 0;
static bool melody_started = false;

void effects_init(void)
{
    current = EFFECT_NONE;
    melody_started = false;
}

void effects_start(effect_type_t type)
{
    current = type;
    start_us = esp_timer_get_time();
    current_cycle = 0;
    melody_started = false;

    switch (type) {
        case EFFECT_POWER_ON:
        case EFFECT_POWER_OFF:
            total_cycles = g_config.power_effect_cycles;
            cycle_dur = g_config.power_effect_cycle_dur;
            break;
        case EFFECT_ARM:
        case EFFECT_DISARM:
            total_cycles = g_config.arm_effect_cycles;
            cycle_dur = g_config.arm_effect_cycle_dur;
            break;
        default:
            current = EFFECT_NONE;
            return;
    }
    ESP_LOGI(TAG, "Start effect %d, cycles=%d", (int)type, total_cycles);
}

bool effects_is_playing(void)
{
    return current != EFFECT_NONE;
}

void effects_update(void)
{
    if (current == EFFECT_NONE) return;

    int64_t elapsed_us = esp_timer_get_time() - start_us;
    float elapsed = elapsed_us / 1000000.0f;
    float t = fmodf(elapsed, cycle_dur);
    float half = cycle_dur / 2.0f;

    float bright;
    if (t < half) {
        bright = (t / half) * 100.0f;
    } else {
        bright = (1.0f - (t - half) / half) * 100.0f;
    }

    /* Эффекты включения/выключения/охраны всегда показывают ленту и лазер
       на полную — игнорируем LDR и режимы (это служебные анимации). */
    led_set_all(bright, bright, bright);
    strip_set_brightness(bright);
    laser_set_brightness(bright);

    /* Мелодии при включении / выключении вместо простых щелчков */
    if (!melody_started) {
        melody_started = true;
        if (current == EFFECT_POWER_ON) {
            melody_id_t id = (melody_id_t)g_config.melody_power_on;
            if (id > MELODY_NONE && id < MELODY_COUNT) {
                buzzer_play_melody(id);
            } else {
                buzzer_click();
            }
        } else if (current == EFFECT_POWER_OFF) {
            melody_id_t id = (melody_id_t)g_config.melody_power_off;
            if (id > MELODY_NONE && id < MELODY_COUNT) {
                buzzer_play_melody(id);
            } else {
                buzzer_click();
            }
        } else {
            /* ARM / DISARM — короткий щелчок в начале каждого цикла */
            buzzer_click();
        }
    }

    /* Для ARM/DISARM — щелчок на каждом новом цикле */
    if (current == EFFECT_ARM || current == EFFECT_DISARM) {
        static int last_click_cycle = -1;
        if (current_cycle != last_click_cycle) {
            buzzer_click();
            last_click_cycle = current_cycle;
        }
        if (elapsed >= (current_cycle + 1) * cycle_dur) {
            current_cycle++;
            if (current_cycle >= total_cycles) {
                led_all_off();
                strip_off();
                buzzer_off();
                last_click_cycle = -1;
                current = EFFECT_NONE;
                ESP_LOGI(TAG, "Effect finished");
            }
        }
        return;
    }

    /* POWER_ON / POWER_OFF: ждём окончания мелодии + световых циклов */
    bool melody_done = !buzzer_melody_is_playing();
    bool light_done  = (elapsed >= total_cycles * cycle_dur);

    if (melody_done && light_done) {
        led_all_off();
        strip_off();
        buzzer_off();
        current = EFFECT_NONE;
        ESP_LOGI(TAG, "Power effect finished");
    }
}
