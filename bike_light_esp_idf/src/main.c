/*
 * Умный габарит-поворотник-стоп-сигнал для велосипеда
 * Редакция 2.6 — чистый ESP-IDF + фоторезистор GL5528
 *
 * ESP32-C3 SuperMini + MPU6050 (SDA=GPIO9, SCL=GPIO10)
 * LED_LEFT = GPIO2, LED_STRIP = GPIO20 (GPIO8 свободен — strapping)
 * Laser (GPIO7 via ULN2003A) + Battery ADC (GPIO1)
 * LDR GL5528 (GPIO0 ADC) + POWER_HOLD (GPIO21)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

#include "config.h"
#include "led_control.h"
#include "buzzer.h"
#include "motion_sensor.h"
#include "button.h"
#include "effects.h"
#include "state_machine.h"
#include "wifi_web.h"
#include "battery.h"
#include "ldr.h"

static const char *TAG = "MAIN";

/* Входы ULN в LOW до инициализации LEDC */
static void force_transistors_off(void)
{
    const int pins[] = {
        PIN_LED_LEFT,
        PIN_LED_CENTER,
        PIN_LED_RIGHT,
        PIN_LED_STRIP,
        PIN_BUZZER,
        PIN_LASER
    };

    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << pins[i]),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io);
        gpio_set_level(pins[i], 0);
    }
}

static void power_hold_on(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_POWER_HOLD),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
    gpio_set_level(PIN_POWER_HOLD, 1);
}

static void power_hold_off(void)
{
    // Сначала гасим все нагрузки
    force_transistors_off();
    gpio_set_level(PIN_POWER_HOLD, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void main_task(void *arg)
{
    ESP_LOGI(TAG, "Main task started");

    while (1) {
        button_update();
        buzzer_update();
        wifi_web_update();
        battery_update();
        state_update();

                static bool was_power_off_fx = false;
        if (state_is_power_off_requested()) {
            if (effects_is_playing()) {
                was_power_off_fx = true;
            } else if (was_power_off_fx) {
                ESP_LOGW(TAG, "Powering off...");
                power_hold_off();
                while (1) {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // ~50 Hz
    }
}

void app_main(void)
{
    force_transistors_off();
    power_hold_on();

    ESP_LOGI(TAG, "=== Bike Light v2.4 (ESP-IDF) starting ===");

        ESP_ERROR_CHECK(config_init());
    config_set_defaults();
    config_load();
    config_print();

        ESP_ERROR_CHECK(led_control_init());
    ESP_ERROR_CHECK(buzzer_init());
    ESP_ERROR_CHECK(button_init());
    ESP_ERROR_CHECK(motion_init());
    ESP_ERROR_CHECK(battery_init());
    ESP_ERROR_CHECK(ldr_init());
    effects_init();
    wifi_web_init();
    state_init();

        effects_start(EFFECT_POWER_ON);
    while (effects_is_playing()) {
        effects_update();
        buzzer_update();
        vTaskDelay(pdMS_TO_TICKS(15));
    }

        wifi_web_start_ap();

        xTaskCreate(main_task, "main_task", 12288, NULL, 5, NULL);

    ESP_LOGI(TAG, "=== Ready ===");
}
