#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t wifi_web_init(void);
void wifi_web_start_ap(void);
void wifi_web_stop_ap(void);
void wifi_web_toggle(void);
bool wifi_web_is_active(void);
/** Idle-таймаут SoftAP + профилактический restart HTTP при долгом отсутствии клиентов */
void wifi_web_update(void);
