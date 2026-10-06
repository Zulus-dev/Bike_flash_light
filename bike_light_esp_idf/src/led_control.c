#include "led_control.h"
#include "config.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "LED";

static float current_brightness[5] = {0};

static const ledc_channel_t channels[5] = {
    LEDC_CHANNEL_LEFT,
    LEDC_CHANNEL_CENTER,
    LEDC_CHANNEL_RIGHT,
    LEDC_CHANNEL_STRIP,
    LEDC_CHANNEL_LASER
};

static const int pins[5] = {
    PIN_LED_LEFT,
    PIN_LED_CENTER,
    PIN_LED_RIGHT,
    PIN_LED_STRIP,
    PIN_LASER
};

esp_err_t led_control_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    for (int i = 0; i < 5; i++) {
        ledc_channel_config_t ch_cfg = {
            .gpio_num   = pins[i],
            .speed_mode = LEDC_MODE,
            .channel    = channels[i],
            .intr_type  = LEDC_INTR_DISABLE,
            .timer_sel  = LEDC_TIMER,
            .duty       = 0,
            .hpoint     = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
    }

    ESP_LOGI(TAG, "LEDC initialized (LEFT/CENTER/RIGHT/STRIP/LASER)");
    return ESP_OK;
}

void led_set_brightness(led_group_t group, float percent)
{
    if (group > LED_GROUP_LASER) return;
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    /* Оптимизация энергии: при 0 % жёстко держим duty = 0 (пин LOW) */
    current_brightness[group] = percent;
    uint32_t duty = (percent <= 0.01f) ? 0 : (uint32_t)(percent * 2.55f);
    ledc_set_duty(LEDC_MODE, channels[group], duty);
    ledc_update_duty(LEDC_MODE, channels[group]);
}

void led_set_all(float left, float center, float right)
{
    led_set_brightness(LED_GROUP_LEFT, left);
    led_set_brightness(LED_GROUP_CENTER, center);
    led_set_brightness(LED_GROUP_RIGHT, right);
}

void led_all_off(void)
{
    led_set_all(0, 0, 0);
    strip_off();
    laser_off();
}

void strip_set_brightness(float percent)
{
    led_set_brightness(LED_GROUP_STRIP, percent);
}

void strip_off(void)
{
    led_set_brightness(LED_GROUP_STRIP, 0);
}

void laser_set_brightness(float percent)
{
    led_set_brightness(LED_GROUP_LASER, percent);
}

void laser_off(void)
{
    led_set_brightness(LED_GROUP_LASER, 0);
}
