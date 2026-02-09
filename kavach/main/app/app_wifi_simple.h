/*
 * Simple WiFi STA for Kavach (no provisioning UI). Connects using SSID/password from menuconfig.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start WiFi and block until connected (or give up after retries). */
esp_err_t app_wifi_simple_start(void);

/** Return true if WiFi is connected. */
bool app_wifi_simple_connected(void);

#ifdef __cplusplus
}
#endif
