#include "button.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "BTN";

#define DEBOUNCE_US     40000
#define SHORT_MAX_US    1000000
#define POWER_MIN_US    2000000
#define POWER_MAX_US    4000000
#define ARM_MIN_US      4000000
#define ARM_MAX_US      8000000
#define WIFI_MIN_US     8000000

static bool last_stable = true;  // pull-up, pressed = false
static bool last_raw = true;
static int64_t last_change_us = 0;
static int64_t press_start_us = 0;
static bool pressed = false;
static button_action_t pending = BUTTON_ACTION_NONE;

esp_err_t button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_BUTTON_MODE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    last_stable = gpio_get_level(PIN_BUTTON_MODE);
    last_raw = last_stable;
    ESP_LOGI(TAG, "Button init on GPIO%d", PIN_BUTTON_MODE);
    return ESP_OK;
}

void button_update(void)
{
    bool raw = gpio_get_level(PIN_BUTTON_MODE);
    int64_t now = esp_timer_get_time();

    if (raw != last_raw) {
        last_change_us = now;
        last_raw = raw;
    }

    if ((now - last_change_us) > DEBOUNCE_US) {
        if (raw != last_stable) {
            last_stable = raw;

            if (last_stable == false) { // нажатие (LOW)
                pressed = true;
                press_start_us = now;
            } else { // отпускание
                if (pressed) {
                    int64_t duration = now - press_start_us;
                    pressed = false;

                    if (duration < SHORT_MAX_US) {
                        pending = BUTTON_ACTION_SHORT;
                    } else if (duration >= POWER_MIN_US && duration < POWER_MAX_US) {
                        pending = BUTTON_ACTION_POWER_OFF;
                    } else if (duration >= ARM_MIN_US && duration < ARM_MAX_US) {
                        pending = BUTTON_ACTION_ARM_DISARM;
                    } else if (duration >= WIFI_MIN_US) {
                        pending = BUTTON_ACTION_WIFI_TOGGLE;
                    }
                }
            }
        }
    }
}

button_action_t button_get_action(void)
{
    button_action_t a = pending;
    pending = BUTTON_ACTION_NONE;
    return a;
}

bool button_is_pressed(void)
{
    return pressed;
}
