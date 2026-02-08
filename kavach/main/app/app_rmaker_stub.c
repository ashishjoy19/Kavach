/*
 * Kavach: RainMaker stub – no-op so UI and main still link without RainMaker.
 */
#include "app_rmaker_stub.h"
#include "esp_log.h"

static const char *TAG = "kavach_rmaker_stub";

void app_rmaker_start(void)
{
    ESP_LOGI(TAG, "Kavach: RainMaker disabled; use MQTT for alerts/calls later.");
}

bool app_rmaker_is_connected(void)
{
    return false;
}
