#pragma once
#include <stdbool.h>

bool adapter_init(void);
void adapter_set_pairable(bool enable);
void adapter_set_discoverable(bool enable);
