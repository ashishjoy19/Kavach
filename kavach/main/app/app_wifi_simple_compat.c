/*
 * Kavach: app_wifi.h compatibility when using simple WiFi (no RainMaker provisioning).
 * UI and net config still call app_wifi_*; this implements them with app_wifi_simple.
 */
#include <string.h>
#include "app_wifi.h"
#include "app_wifi_simple.h"
#include "sdkconfig.h"

static char s_prov_payload[64] = "WiFi: set in menuconfig";

void app_wifi_init(void)
{
    /* No-op for simple WiFi; netif/event loop are created in app_wifi_simple_start */
}

char *app_wifi_get_prov_payload(void)
{
    return s_prov_payload;
}

bool app_wifi_is_connected(void)
{
    return app_wifi_simple_connected();
}

esp_err_t app_wifi_start(void)
{
    return app_wifi_simple_start();
}

esp_err_t app_wifi_get_wifi_ssid(char *ssid, size_t len)
{
    if (!ssid || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(ssid, CONFIG_KAVACH_WIFI_SSID, len - 1);
    ssid[len - 1] = '\0';
    return ESP_OK;
}

esp_err_t app_wifi_prov_start(void)
{
    /* No BLE provisioning when using simple WiFi */
    return ESP_OK;
}

esp_err_t app_wifi_prov_stop(void)
{
    return ESP_OK;
}
