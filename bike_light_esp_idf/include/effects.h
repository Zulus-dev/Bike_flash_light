#pragma once

#include <stdbool.h>

typedef enum {
    EFFECT_NONE = 0,
    EFFECT_POWER_ON,
    EFFECT_POWER_OFF,
    EFFECT_ARM,
    EFFECT_DISARM
} effect_type_t;

void effects_init(void);
void effects_start(effect_type_t type);
bool effects_is_playing(void);
void effects_update(void);
