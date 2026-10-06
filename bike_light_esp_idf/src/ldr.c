#include "ldr.h"
#include "config.h"
#include "adc1_shared.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "LDR";

static bool initialized = false;

static uint16_t raw_avg = 0;
static uint8_t  percent = 0;
static bool     is_dark = false;
static int64_t  last_sample_us = 0;

#define LDR_AVG_SAMPLES  8
static int samples[LDR_AVG_SAMPLES];
static int sample_idx = 0;
static int sample_count = 0;

#if defined(ADC_ATTEN_DB_12)
#define LDR_ADC_ATTEN  ADC_ATTEN_DB_12
#else
#define LDR_ADC_ATTEN  ADC_ATTEN_DB_11
#endif

esp_err_t ldr_init(void)
{
    if (!adc1_shared_ready()) {
        esp_err_t err = adc1_shared_init();
        if (err != ESP_OK) return err;
    }

    esp_err_t err = adc1_shared_config_channel(ADC_CHANNEL_0, LDR_ADC_ATTEN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CH0 config failed: %s", esp_err_to_name(err));
        return err;
    }

    initialized = true;
    is_dark = false;
    percent = 0;
    raw_avg = 0;
    sample_idx = 0;
    sample_count = 0;
    last_sample_us = 0;

    ESP_LOGI(TAG, "LDR GPIO%d (ADC1_CH0)", PIN_LDR);
    return ESP_OK;
}

void ldr_update(void)
{
    if (!initialized || !g_config.ldr_enable || !adc1_shared_ready()) {
        is_dark = false;
        percent = 0;
        return;
    }

    int64_t now = esp_timer_get_time();
    if (now - last_sample_us < 50000) return;
    last_sample_us = now;

    int raw = 0;
    if (adc1_shared_read_raw(ADC_CHANNEL_0, &raw) != ESP_OK) {
        return;
    }

    samples[sample_idx] = raw;
    sample_idx = (sample_idx + 1) % LDR_AVG_SAMPLES;
    if (sample_count < LDR_AVG_SAMPLES) sample_count++;

    int sum = 0;
    for (int i = 0; i < sample_count; i++) sum += samples[i];
    raw_avg = (uint16_t)(sum / sample_count);

    percent = (uint8_t)((raw_avg * 100) / 4095);
    if (percent > 100) percent = 100;

    uint8_t thr = g_config.ldr_threshold;
    if (thr > 100) thr = 100;
    uint8_t hyst = g_config.ldr_hysteresis;
    if (hyst > 30) hyst = 30;

    if (is_dark) {
        if (percent + hyst < thr) is_dark = false;
    } else {
        if (percent >= thr) is_dark = true;
    }
}

bool ldr_is_dark(void)
{
    if (!g_config.ldr_enable) return false;
    return is_dark;
}

uint8_t ldr_get_percent(void)
{
    return percent;
}

uint16_t ldr_get_raw(void)
{
    return raw_avg;
}

float ldr_get_brightness_scale(void)
{
    if (!g_config.ldr_enable) return 1.0f;
    return (float)percent / 100.0f;
}
