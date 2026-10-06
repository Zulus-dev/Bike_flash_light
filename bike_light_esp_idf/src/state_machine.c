#include "state_machine.h"
#include "config.h"
#include "led_control.h"
#include "buzzer.h"
#include "motion_sensor.h"
#include "button.h"
#include "effects.h"
#include "wifi_web.h"
#include "ldr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "STATE";

static device_mode_t current_mode = MODE_NORMAL;
static device_mode_t previous_mode = MODE_NORMAL;

static int64_t mode_enter_us = 0;
static int64_t last_motion_us = 0;
static int64_t stationary_start_us = 0;
static int64_t movement_start_us = 0;
static int64_t brake_active_until_us = 0;
static int64_t turn_signal_until_us = 0;
static int turn_direction = 0; // -1 left, +1 right
static int64_t tilt_start_us = 0;
static bool tilt_active = false;

static bool power_off_requested = false;
static int64_t alarm_trigger_us = 0;
static bool alarm_triggered = false;

static float breath_phase = 0;
static bool turn_blink_prev = false;
static bool turn_blink_have_prev = false;

/* ---------- Вспомогательная логика ленты и лазера ---------- */

/**
 * Вычисляет целевую яркость ленты (0–100) с учётом режима и LDR.
 * force_brightness — если >= 0, используется вместо расчётной (например, при стопе).
 */
static float compute_strip_brightness(float force_brightness)
{
    if (g_config.strip_mode == STRIP_MODE_OFF) {
        return 0.0f;
    }

    bool dark = ldr_is_dark();
    float day_br   = (float)g_config.strip_brightness_day;
    float night_br = (float)g_config.strip_brightness_night;

    float target = 0.0f;

    switch (g_config.strip_mode) {
        case STRIP_MODE_ALWAYS:
            target = (force_brightness >= 0.0f) ? force_brightness : night_br;
            break;
        case STRIP_MODE_DARK_ONLY:
            if (!dark) return 0.0f;
            target = (force_brightness >= 0.0f) ? force_brightness : night_br;
            break;
        case STRIP_MODE_SCALE: {
            float scale = ldr_get_brightness_scale(); /* 0=ярко … 1=темно */
            float base = day_br + (night_br - day_br) * scale;
            target = (force_brightness >= 0.0f) ? force_brightness * scale : base;
            break;
        }
        default:
            return 0.0f;
    }

    if (target < 0.01f) return 0.0f;
    return target;
}

/**
 * Применяет эффект ленты (дыхание / solid / hazard) к уже вычисленной базовой яркости.
 */
static void apply_strip_effect(float base_brightness, int64_t now)
{
    if (base_brightness < 0.01f) {
        strip_off();
        return;
    }

    float period = g_config.strip_period_sec;
    if (period < 0.2f) period = 0.2f;

    float out = base_brightness;

    switch (g_config.strip_effect) {
        case STRIP_EFFECT_BREATHING: {
            float s = (sinf(breath_phase * 2.0f * 3.14159265f *
                            (g_config.breathing_period_sec / period)) + 1.0f) * 0.5f;
            out = base_brightness * s;
            break;
        }
        case STRIP_EFFECT_SOLID:
            out = base_brightness;
            break;
        case STRIP_EFFECT_HAZARD: {
            float phase = fmodf(now / 1000000.0f, period) / period;
            float s = (sinf(phase * 2.0f * 3.14159265f) + 1.0f) * 0.5f;
            out = base_brightness * s;
            break;
        }
        default:
            break;
    }

    strip_set_brightness(out);
}

/**
 * Логика лазера.
 * is_braking — сейчас идёт стоп-сигнал.
 * brake_on   — текущая фаза мигания стопа (true = ярко).
 */
static void apply_laser(bool is_braking, bool brake_on)
{
    if (g_config.laser_mode == LASER_MODE_OFF) {
        laser_off();
        return;
    }

    bool dark = ldr_is_dark();
    float br = g_config.laser_brightness;
    if (br < 0.01f) {
        laser_off();
        return;
    }

    switch (g_config.laser_mode) {
        case LASER_MODE_ALWAYS:
            if (is_braking && g_config.laser_sync_brake) {
                laser_set_brightness(brake_on ? br : 0.0f);
            } else if (!is_braking) {
                laser_set_brightness(br);
            } else {
                laser_off();
            }
            break;

        case LASER_MODE_DARK_ONLY:
            if (!dark) {
                laser_off();
                break;
            }
            if (is_braking && g_config.laser_sync_brake) {
                laser_set_brightness(brake_on ? br : 0.0f);
            } else if (!is_braking) {
                laser_set_brightness(br);
            } else {
                laser_off();
            }
            break;

        case LASER_MODE_BRAKE_ONLY_DARK:
            if (!dark || !is_braking) {
                laser_off();
                break;
            }
            if (g_config.laser_sync_brake) {
                laser_set_brightness(brake_on ? br : 0.0f);
            } else {
                laser_set_brightness(br);
            }
            break;

        default:
            laser_off();
            break;
    }
}

/* ---------- Основные режимы ---------- */

void state_init(void)
{
    current_mode = MODE_NORMAL;
    mode_enter_us = esp_timer_get_time();
    last_motion_us = mode_enter_us;
    power_off_requested = false;
    ESP_LOGI(TAG, "State machine init → NORMAL");
}

device_mode_t state_get_mode(void)
{
    return current_mode;
}

void state_set_mode(device_mode_t mode)
{
    if (mode == current_mode) return;
    previous_mode = current_mode;
    current_mode = mode;
    mode_enter_us = esp_timer_get_time();
    alarm_triggered = false;
    alarm_trigger_us = 0;
    turn_direction = 0;
    tilt_active = false;
    turn_blink_have_prev = false;
    movement_start_us = 0;
    stationary_start_us = 0;
    if (mode == MODE_HAZARD || mode == MODE_ALARM) {
        motion_save_reference();
    }
    if (mode != MODE_ALARM) {
        buzzer_siren_stop();
    }
    ESP_LOGI(TAG, "Mode → %d", (int)mode);
}

void state_request_power_off(void)
{
    power_off_requested = true;
}

bool state_is_power_off_requested(void)
{
    return power_off_requested;
}

bool state_is_alarm_triggered(void)
{
    return alarm_triggered;
}

void state_arm_alarm(void)
{
    if (current_mode == MODE_ALARM) return;
    effects_start(EFFECT_ARM);
    state_set_mode(MODE_ALARM);
    wifi_web_start_ap();
}

void state_disarm_alarm(void)
{
    if (current_mode != MODE_ALARM) return;
    buzzer_siren_stop();
    effects_start(EFFECT_DISARM);
    state_set_mode(previous_mode == MODE_ALARM ? MODE_NORMAL : previous_mode);
}

static void update_breathing(void)
{
    float period = g_config.breathing_period_sec;
    breath_phase += 0.02f / period;
    if (breath_phase > 1.0f) breath_phase -= 1.0f;

    float s = (sinf(breath_phase * 2 * 3.14159265f) + 1.0f) * 0.5f;
    float side = s * g_config.side_max_brightness;
    float center = s * g_config.center_max_brightness;

    led_set_all(side, center, side);

    float strip_br = compute_strip_brightness(-1.0f);
    apply_strip_effect(strip_br, esp_timer_get_time());
    apply_laser(false, false);
}

static void update_hazard(void)
{
    int64_t now = esp_timer_get_time();
    bool blink = ((now / 250000) % 2) == 0; // 2 Hz

    float side = blink ? 100.0f : 0.0f;
    float center_phase = fmodf(now / 1000000.0f, 2.0f) / 2.0f;
    float center = (sinf(center_phase * 2 * 3.14159265f) + 1.0f) * 0.5f * 50.0f;

    led_set_all(side, center, side);

    /* В аварийке лента работает независимо от LDR (безопасность) */
    if (g_config.strip_mode != STRIP_MODE_OFF) {
        float br = (float)g_config.strip_brightness_night;
        if (br < 1.0f) br = 60.0f;
        apply_strip_effect(br, now);
    } else {
        strip_off();
    }
    laser_off();
}

static void update_alarm_siren(void)
{
    int64_t now = esp_timer_get_time();
    bool state = ((now / 200000) % 2) == 0;

    float side = state ? 100.0f : 0.0f;
    float center = state ? 0.0f : 80.0f;
    led_set_all(side, center, side);

    /* В сирене лента тоже игнорирует LDR */
    if (g_config.strip_mode != STRIP_MODE_OFF) {
        strip_set_brightness(side);
    } else {
        strip_off();
    }

    if (!buzzer_siren_is_active()) {
        buzzer_siren_start();
    }
}

void state_update(void)
{
    int64_t now = esp_timer_get_time();

    if (effects_is_playing()) {
        effects_update();
        return;
    }

    if (power_off_requested) {
        effects_start(EFFECT_POWER_OFF);
        return;
    }

    motion_update();
    ldr_update();

    /* Постановка автоаварийки */
    {
        uint8_t ss = g_config.still_sensitivity;
        if (ss < 1) ss = 1;
        if (ss > 10) ss = 10;
        float k = (ss - 1) / 9.0f;
        float accel_th = 0.12f - k * 0.08f;
        float gyro_th  = 25.0f - k * 17.0f;
        if (motion_is_stationary_ex(accel_th, gyro_th)) {
            if (stationary_start_us == 0) stationary_start_us = now;
        } else {
            stationary_start_us = 0;
        }
    }

    /* Снятие автоаварийки */
    if (current_mode == MODE_HAZARD) {
        uint8_t ms = g_config.move_sensitivity;
        if (ms < 1) ms = 1;
        if (ms > 10) ms = 10;
        float k = (ms - 1) / 9.0f;
        float exit_th = 0.18f - k * 0.14f;
        if (motion_detect_alarm(exit_th)) {
            last_motion_us = now;
            if (movement_start_us == 0) movement_start_us = now;
        } else {
            movement_start_us = 0;
        }
    }

    // Кнопка
    button_action_t act = button_get_action();
    if (act != BUTTON_ACTION_NONE) {
        switch (act) {
            case BUTTON_ACTION_SHORT:
                if (current_mode != MODE_ALARM) {
                    state_set_mode(current_mode == MODE_NORMAL ? MODE_HAZARD : MODE_NORMAL);
                }
                break;
            case BUTTON_ACTION_POWER_OFF:
                if (current_mode != MODE_ALARM) {
                    state_request_power_off();
                }
                break;
            case BUTTON_ACTION_ARM_DISARM:
                if (current_mode == MODE_ALARM) {
                    effects_start(EFFECT_DISARM);
                    state_set_mode(previous_mode == MODE_ALARM ? MODE_NORMAL : previous_mode);
                } else {
                    effects_start(EFFECT_ARM);
                    state_set_mode(MODE_ALARM);
                    wifi_web_start_ap();
                }
                break;
            case BUTTON_ACTION_WIFI_TOGGLE:
                wifi_web_toggle();
                break;
            default: break;
        }
    }

    switch (current_mode) {
        case MODE_NORMAL: {
            if (motion_is_braking(g_config.brake_decel_threshold)) {
                brake_active_until_us = now + (int64_t)(g_config.brake_active_time_sec * 1000000);
            }

            if (now < brake_active_until_us) {
                int64_t half_us = (int64_t)(g_config.brake_blink_period_sec * 500000.0f);
                if (half_us < 50000) half_us = 50000;
                bool on = ((now / half_us) % 2) == 0;
                float b = on ? g_config.brake_brightness : 0.0f;
                led_set_all(b, b, b);

                /* Лента при стопе — форсируем яркость стопа, но с учётом LDR-режима */
                float strip_br = compute_strip_brightness(b);
                strip_set_brightness(strip_br);

                apply_laser(true, on);
                break;
            }

            bool left_tilt = motion_is_tilted_left(g_config.tilt_angle_min_deg, g_config.tilt_angle_max_deg);
            bool right_tilt = motion_is_tilted_right(g_config.tilt_angle_min_deg, g_config.tilt_angle_max_deg);

            if (g_config.invert_turn_signals) {
                bool tmp = left_tilt;
                left_tilt = right_tilt;
                right_tilt = tmp;
            }

            if (left_tilt || right_tilt) {
                if (!tilt_active) {
                    tilt_start_us = now;
                    tilt_active = true;
                } else if (now - tilt_start_us >= (int64_t)(g_config.tilt_hold_time_sec * 1000000)) {
                    turn_direction = left_tilt ? -1 : 1;
                    turn_signal_until_us = now + (int64_t)(g_config.turn_signal_active_sec * 1000000);
                }
            } else {
                tilt_active = false;
            }

            if (now < turn_signal_until_us && turn_direction != 0) {
                bool on = ((now / 250000) % 2) == 0;
                if (!turn_blink_have_prev) {
                    turn_blink_prev = on;
                    turn_blink_have_prev = true;
                    buzzer_click();
                } else if (on != turn_blink_prev) {
                    buzzer_click();
                    turn_blink_prev = on;
                }
                if (turn_direction < 0) {
                    led_set_all(on ? 100.0f : 0.0f, 75.0f, 75.0f);
                } else {
                    led_set_all(75.0f, 75.0f, on ? 100.0f : 0.0f);
                }

                float strip_br = compute_strip_brightness(-1.0f);
                apply_strip_effect(strip_br, now);
                apply_laser(false, false);
                break;
            } else {
                if (turn_direction != 0) {
                    buzzer_off();
                }
                turn_direction = 0;
                turn_blink_have_prev = false;
            }

            if (stationary_start_us > 0 &&
                (now - stationary_start_us) >= (int64_t)(g_config.stationary_time_sec * 1000000)) {
                state_set_mode(MODE_HAZARD);
                break;
            }

            update_breathing();
            break;
        }

        case MODE_HAZARD: {
            buzzer_siren_stop();
            update_hazard();
            if (movement_start_us > 0 &&
                (now - movement_start_us) >= (int64_t)(g_config.movement_time_sec * 1000000)) {
                state_set_mode(MODE_NORMAL);
            }
            break;
        }

        case MODE_ALARM: {
            if (!alarm_triggered) {
                led_all_off();
                strip_off();
                laser_off();
                buzzer_siren_stop();
                buzzer_off();

                if (motion_detect_alarm(g_config.alarm_motion_threshold)) {
                    if (alarm_trigger_us == 0) alarm_trigger_us = now;
                    else if (now - alarm_trigger_us >= (int64_t)(g_config.alarm_confirm_time_sec * 1000000)) {
                        alarm_triggered = true;
                        ESP_LOGW(TAG, "ALARM TRIGGERED!");
                        buzzer_siren_start();
                    }
                } else {
                    alarm_trigger_us = 0;
                }
            } else {
                update_alarm_siren();
            }
            break;
        }
    }
}
