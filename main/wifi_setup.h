#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t wifi_setup_init(void);
bool wifi_setup_is_connected(void);
