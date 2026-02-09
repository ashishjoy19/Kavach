/*
 * Kavach: voice recognition + MQTT. Publishes voice commands to broker, subscribes for appliance control.
 */
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "bsp_storage.h"
#include "settings.h"
#include "app_sr.h"
#include "app_wifi_simple.h"
#include "app_mqtt.h"
#include "gui/ui_kavach.h"
#include "bsp_board.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Kavach (voice + MQTT). Compile: %s %s", __DATE__, __TIME__);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(settings_read_parameter_from_nvs());

    /* WiFi then MQTT (broker on PC) */
    err = app_wifi_simple_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi not connected; MQTT will not work until WiFi is available");
    } else {
        err = app_mqtt_start();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "MQTT start failed");
        }
    }

    bsp_spiffs_mount();
    bsp_i2c_init();

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = 0,
        .flags = { .buff_dma = true }
    };
    cfg.lvgl_port_cfg.task_affinity = 1;
    bsp_display_start_with_config(&cfg);
    bsp_board_init();

    kavach_ui_start();
    vTaskDelay(pdMS_TO_TICKS(500));
    bsp_display_backlight_on();

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Speech recognition start");
    app_sr_start(false);
}
