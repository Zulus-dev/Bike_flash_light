#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// ==================== ПИНЫ (редакция 2.6) ====================
// GPIO8 — strapping: для Joint Download Boot должен быть HIGH.
// Нагрузка через ULN на GPIO8 срывает USB-прошивку → LED_STRIP на GPIO20.
#define PIN_POWER_HOLD   21  /* удержание питания (Q2) */
#define PIN_BAT_ADC      1   /* ADC1_CH1 — делитель батареи */
#define PIN_LED_LEFT     2   /* ULN I4 — поворотник LEFT */
#define PIN_LED_CENTER   3
#define PIN_LED_RIGHT    4
#define PIN_BUZZER       5
#define PIN_BUTTON_MODE  6
#define PIN_LASER        7   /* ULN I5 */
#define PIN_LED_STRIP   20  /* ULN I6 — перенесён с GPIO8 (strapping) */
#define PIN_I2C_SDA      9
#define PIN_I2C_SCL     10
#define PIN_LDR          0   /* ADC1_CH0 — GL5528 */

// LEDC
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT
#define LEDC_FREQUENCY          5000

#define LEDC_CHANNEL_LEFT       LEDC_CHANNEL_0
#define LEDC_CHANNEL_CENTER     LEDC_CHANNEL_1
#define LEDC_CHANNEL_RIGHT      LEDC_CHANNEL_2
#define LEDC_CHANNEL_STRIP      LEDC_CHANNEL_3
#define LEDC_CHANNEL_BUZZER     LEDC_CHANNEL_4
#define LEDC_CHANNEL_LASER      LEDC_CHANNEL_5

// ==================== РЕЖИМЫ ЛЕНТЫ И ЛАЗЕРА ====================
typedef enum {
    STRIP_MODE_OFF = 0,          /* полностью выключена */
    STRIP_MODE_ALWAYS,           /* всегда (игнорирует LDR) */
    STRIP_MODE_DARK_ONLY,        /* только когда темно */
    STRIP_MODE_SCALE             /* яркость масштабируется от освещённости */
} strip_mode_t;

typedef enum {
    LASER_MODE_OFF = 0,              /* полностью выключен */
    LASER_MODE_ALWAYS,               /* всегда (игнорирует LDR) */
    LASER_MODE_DARK_ONLY,            /* только когда темно */
    LASER_MODE_BRAKE_ONLY_DARK       /* только при торможении + только в темноте */
} laser_mode_t;

typedef enum {
    STRIP_EFFECT_BREATHING = 0,
    STRIP_EFFECT_SOLID,
    STRIP_EFFECT_HAZARD
} strip_effect_t;

// ==================== КОНФИГУРАЦИЯ ====================
typedef struct {
    // Габарит
    float breathing_period_sec;
    float side_max_brightness;      // 0-100
    float center_max_brightness;

    // Стоп
    float brake_decel_threshold;
    float brake_blink_period_sec;
    float brake_active_time_sec;
    float brake_brightness;           // 0-100 яркость стопа
    uint8_t brake_bump_reject;        // 1–10: защита от кочек (10 = макс. отсечение ложных)
    float brake_confirm_sec;          // сколько должен держаться тормоз, прежде чем сработать
    float brake_pitch_deg;            // наклон вперёд (°) для стопа на спуске; 0 = выкл

    // Поворотники
    float tilt_angle_min_deg;
    float tilt_angle_max_deg;
    float tilt_hold_time_sec;
    float turn_signal_active_sec;
    bool  invert_turn_signals;        // инверсия лево/право

    // Автоаварийка
    float stationary_time_sec;       // сек тишины → включить
    float movement_time_sec;         // сек движения → выключить
    uint8_t still_sensitivity;       // 1–10: насколько тихо, чтобы включить
    uint8_t move_sensitivity;        // 1–10: насколько заметно движение, чтобы выключить

    // Охрана
    float alarm_motion_threshold;
    float alarm_confirm_time_sec;
    float alarm_siren_duration_sec;

    // Эффекты
    int   power_effect_cycles;
    float power_effect_cycle_dur;
    int   arm_effect_cycles;
    float arm_effect_cycle_dur;

    // Звук (PC-speaker / магнитный)
    float    buzzer_volume;     /* 0–100 % */
    uint16_t buzzer_freq_hz;    /* частота тона 200–8000 */
    uint8_t  melody_power_on;   /* melody_id_t */
    uint8_t  melody_power_off;

    // ----- Фоторезистор (новый) -----
    bool     ldr_enable;            /* использовать фоторезистор */
    uint8_t  ldr_threshold;         /* порог «темно» 0–100 % (100 = полная темнота) */
    uint8_t  ldr_hysteresis;        /* гистерезис 0–30 % */

    // ----- Светодиодная лента (полная замена старых strip_*) -----
    uint8_t  strip_mode;            /* strip_mode_t */
    uint8_t  strip_effect;          /* strip_effect_t */
    uint8_t  strip_brightness_day;  /* 0–100, обычно 0 */
    uint8_t  strip_brightness_night;/* 0–100 */
    float    strip_period_sec;      /* период дыхания/мигания */

    // ----- Лазер (полная замена старых laser_*) -----
    uint8_t  laser_mode;            /* laser_mode_t */
    float    laser_brightness;      /* 0–100 */
    bool     laser_sync_brake;      /* мигать вместе со стоп-сигналом */

    // WiFi
    char  wifi_ap_ssid[32];
    char  wifi_ap_password[64];
    uint8_t wifi_ap_channel;
    uint8_t wifi_ap_max_clients;
    uint16_t wifi_idle_timeout_sec;

    // UI (только внешний вид веб-интерфейса, не влияет на логику фонаря)
    // 0=classic, 1=hud, 2=tiles, 3=orbit
    uint8_t ui_theme;
    // 0=soft (Neon Soft), 1=bold (Neon Bold), 2=duo (Neon Duo)
    uint8_t ui_icon_style;

    // Калибровка MPU
    float accel_offset_x;
    float accel_offset_y;
    float accel_offset_z;
    float gyro_offset_x;
    float gyro_offset_y;
    float gyro_offset_z;
} bike_config_t;

extern bike_config_t g_config;

esp_err_t config_init(void);
esp_err_t config_load(void);
esp_err_t config_save(void);
void config_set_defaults(void);
void config_print(void);
