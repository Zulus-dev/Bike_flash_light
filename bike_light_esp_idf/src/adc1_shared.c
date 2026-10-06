#include "adc1_shared.h"
#include "esp_log.h"

static const char *TAG = "ADC1";

static adc_oneshot_unit_handle_t s_handle = NULL;
static bool s_ready = false;

esp_err_t adc1_shared_init(void)
{
    if (s_ready && s_handle != NULL) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        s_handle = NULL;
        s_ready = false;
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "ADC_UNIT_1 oneshot ready (shared)");
    return ESP_OK;
}

bool adc1_shared_ready(void)
{
    return s_ready && s_handle != NULL;
}

esp_err_t adc1_shared_config_channel(adc_channel_t chan, adc_atten_t atten)
{
    if (!adc1_shared_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = atten,
    };

    esp_err_t err = adc_oneshot_config_channel(s_handle, chan, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config channel %d failed: %s", (int)chan, esp_err_to_name(err));
    }
    return err;
}

esp_err_t adc1_shared_read_raw(adc_channel_t chan, int *raw_out)
{
    if (!adc1_shared_ready() || raw_out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return adc_oneshot_read(s_handle, chan, raw_out);
}

adc_oneshot_unit_handle_t adc1_shared_handle(void)
{
    return s_handle;
}
