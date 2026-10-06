#include "battery.h"
#include "config.h"
#include "adc1_shared.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "BAT";

/*
 * Делитель 100k / 100k (1:2) + конденсатор у GPIO1.
 * GPIO1 = ADC1_CHANNEL_1. Единый ADC_UNIT_1 — модуль adc1_shared.
 */
#define DIVIDER_RATIO   2.0f
#define CAL_SCALE       1.0444f
#define SAMPLES         32
#define UPDATE_PERIOD_US  500000
#define EMA_ALPHA       0.25f

#if defined(ADC_ATTEN_DB_12)
#define ADC_ATTEN       ADC_ATTEN_DB_12
#define ADC_RAW_MAX_MV  3100
#else
#define ADC_ATTEN       ADC_ATTEN_DB_11
#define ADC_RAW_MAX_MV  2500
#endif

static adc_cali_handle_t cali_handle;
static bool use_cali;

static float voltage_v;
static uint8_t percent;
static int64_t last_us;
static bool has_sample;

static uint8_t voltage_to_percent(float v)
{
    static const struct { float v; uint8_t p; } t[] = {
        {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85},
        {4.02f, 80}, {3.98f, 75}, {3.94f, 70}, {3.90f, 65},
        {3.86f, 60}, {3.82f, 55}, {3.78f, 50}, {3.74f, 45},
        {3.70f, 40}, {3.66f, 35}, {3.62f, 30}, {3.58f, 25},
        {3.52f, 20}, {3.46f, 15}, {3.40f, 10}, {3.30f,  5},
        {3.20f,  0},
    };
    if (v >= t[0].v) return 100;
    for (int i = 0; i < (int)(sizeof(t) / sizeof(t[0])) - 1; i++) {
        if (v >= t[i + 1].v) {
            float dv = t[i].v - t[i + 1].v;
            float dp = (float)(t[i].p - t[i + 1].p);
            float k = (v - t[i + 1].v) / dv;
            return (uint8_t)(t[i + 1].p + k * dp + 0.5f);
        }
    }
    return 0;
}

esp_err_t battery_init(void)
{
    ESP_ERROR_CHECK(adc1_shared_init());
    ESP_ERROR_CHECK(adc1_shared_config_channel(ADC_CHANNEL_1, ADC_ATTEN));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = ADC_CHANNEL_1,
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    use_cali = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle) == ESP_OK);
    if (!use_cali) {
        ESP_LOGW(TAG, "ADC cali unavailable, raw fallback");
    }

    last_us = 0;
    has_sample = false;
    voltage_v = 0;
    percent = 0;

    battery_update();
    ESP_LOGI(TAG, "Battery ADC GPIO%d (CH1), scale=%.4f", PIN_BAT_ADC, CAL_SCALE);
    return ESP_OK;
}

void battery_update(void)
{
    int64_t now = esp_timer_get_time();
    if (last_us && (now - last_us) < UPDATE_PERIOD_US) {
        return;
    }
    last_us = now;

    if (!adc1_shared_ready()) return;

    int sum = 0;
    int ok = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int raw = 0;
        if (adc1_shared_read_raw(ADC_CHANNEL_1, &raw) == ESP_OK) {
            sum += raw;
            ok++;
        }
    }
    if (ok == 0) return;
    int raw_avg = sum / ok;

    int mv = 0;
    if (use_cali) {
        adc_cali_raw_to_voltage(cali_handle, raw_avg, &mv);
    } else {
        mv = (raw_avg * ADC_RAW_MAX_MV) / 4095;
    }

    float v = (mv / 1000.0f) * DIVIDER_RATIO * CAL_SCALE;

    if (!has_sample) {
        voltage_v = v;
        has_sample = true;
    } else {
        voltage_v = voltage_v * (1.0f - EMA_ALPHA) + v * EMA_ALPHA;
    }
    percent = voltage_to_percent(voltage_v);
}

float battery_get_voltage(void)
{
    return voltage_v;
}

uint8_t battery_get_percent(void)
{
    return percent;
}

bool battery_is_low(void)
{
    return has_sample && voltage_v < 3.20f;
}
