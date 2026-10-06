#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CONFIG";
bike_config_t g_config;

void config_set_defaults(void)
{
    g_config.breathing_period_sec     = 2.0f;
    g_config.side_max_brightness      = 75.0f;
    g_config.center_max_brightness    = 100.0f;

    g_config.brake_decel_threshold    = 0.45f; /* динамическое Δg, не сырой ax */
    g_config.brake_blink_period_sec   = 0.5f;
    g_config.brake_active_time_sec    = 1.5f;
    g_config.brake_brightness         = 100.0f;
    g_config.brake_bump_reject        = 5;
    g_config.brake_confirm_sec        = 0.08f; /* 80 мс */
    g_config.brake_pitch_deg          = 0.0f;  /* 0 = стоп по наклону выкл */

    g_config.tilt_angle_min_deg       = 18.0f;
    g_config.tilt_angle_max_deg       = 90.0f; /* без жёсткого «потолка» 60° */
    g_config.tilt_hold_time_sec       = 0.8f;  /* быстрее реакция на поворот */
    g_config.turn_signal_active_sec   = 8.0f;
    g_config.invert_turn_signals      = false;

    g_config.stationary_time_sec      = 12.0f;
    g_config.movement_time_sec        = 3.0f;
    g_config.still_sensitivity        = 5;
    g_config.move_sensitivity         = 6;

    g_config.alarm_motion_threshold   = 0.15f;
    g_config.alarm_confirm_time_sec   = 0.5f;
    g_config.alarm_siren_duration_sec = 30.0f;

    g_config.power_effect_cycles      = 3;
    g_config.power_effect_cycle_dur   = 2.0f;
    g_config.arm_effect_cycles        = 2;
    g_config.arm_effect_cycle_dur     = 1.0f;

    g_config.buzzer_volume            = 100.0f;
    g_config.buzzer_freq_hz           = 2200;
    g_config.melody_power_on          = 2;   /* MELODY_VERTU */
    g_config.melody_power_off         = 4;   /* MELODY_POWER_OFF */

    /* Фоторезистор */
    g_config.ldr_enable               = true;
    g_config.ldr_threshold            = 55;  /* % — средние сумерки */
    g_config.ldr_hysteresis           = 8;

    /* Лента — новые параметры */
    g_config.strip_mode               = STRIP_MODE_DARK_ONLY;
    g_config.strip_effect             = STRIP_EFFECT_BREATHING;
    g_config.strip_brightness_day     = 0;
    g_config.strip_brightness_night   = 80;
    g_config.strip_period_sec         = 2.0f;

    /* Лазер — новые параметры */
    g_config.laser_mode               = LASER_MODE_BRAKE_ONLY_DARK;
    g_config.laser_brightness         = 100.0f;
    g_config.laser_sync_brake         = true;

    strncpy(g_config.wifi_ap_ssid, "BikeLight_AP", sizeof(g_config.wifi_ap_ssid));
    strncpy(g_config.wifi_ap_password, "12345678", sizeof(g_config.wifi_ap_password));
    g_config.wifi_ap_channel      = 6;
    g_config.wifi_ap_max_clients  = 2;
    g_config.wifi_idle_timeout_sec = 300;

    g_config.ui_theme = 2;       /* tiles по умолчанию */
    g_config.ui_icon_style = 0;  /* Neon Soft по умолчанию */

    g_config.accel_offset_x = 0;
    g_config.accel_offset_y = 0;
    g_config.accel_offset_z = 0;
    g_config.gyro_offset_x  = 0;
    g_config.gyro_offset_y  = 0;
    g_config.gyro_offset_z  = 0;
}

esp_err_t config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t config_load(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("bike_cfg", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No config in NVS, using defaults");
        config_set_defaults();
        return err;
    }

    size_t size = sizeof(bike_config_t);
    err = nvs_get_blob(handle, "config", &g_config, &size);
    nvs_close(handle);

    if (err != ESP_OK || size != sizeof(bike_config_t)) {
        ESP_LOGW(TAG, "Invalid config blob (size mismatch after schema change), using defaults");
        config_set_defaults();
        return err;
    }

    ESP_LOGI(TAG, "Config loaded from NVS");
    return ESP_OK;
}

esp_err_t config_save(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("bike_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, "config", &g_config, sizeof(bike_config_t));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Config saved");
    } else {
        ESP_LOGE(TAG, "Config save failed: %s", esp_err_to_name(err));
    }
    return err;
}

void config_print(void)
{
    ESP_LOGI(TAG, "=== Bike Config ===");
    ESP_LOGI(TAG, "Breathing: %.1fs  side=%.0f%% center=%.0f%%",
             g_config.breathing_period_sec,
             g_config.side_max_brightness,
             g_config.center_max_brightness);
    ESP_LOGI(TAG, "Brake thresh: %.2f g", g_config.brake_decel_threshold);
    ESP_LOGI(TAG, "LDR: en=%d thr=%u hyst=%u",
             g_config.ldr_enable, g_config.ldr_threshold, g_config.ldr_hysteresis);
    ESP_LOGI(TAG, "Strip mode=%u effect=%u day=%u night=%u",
             g_config.strip_mode, g_config.strip_effect,
             g_config.strip_brightness_day, g_config.strip_brightness_night);
    ESP_LOGI(TAG, "Laser mode=%u br=%.0f sync=%d",
             g_config.laser_mode, g_config.laser_brightness, g_config.laser_sync_brake);
    ESP_LOGI(TAG, "WiFi SSID: %s", g_config.wifi_ap_ssid);
}
