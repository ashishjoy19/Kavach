/*
 * Kavach: voice recognition + MQTT. Publishes voice commands to broker, subscribes for appliance control.
 * Home button: sends emergency message (same as voice "Send alert").
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
#include "app_sntp.h"
#include "app_mqtt.h"
#include "gui/ui_kavach.h"
#include "bsp_board.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "main";

#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
static void home_btn_emergency_cb(void *arg, void *user_data)
{
    (void)arg;
    (void)user_data;
    ESP_LOGI(TAG, "Home button: sending emergency");
    kavach_ui_set_status("Alert sent");
    kavach_ui_set_light(KAVACH_LIGHT_ALERT);
    kavach_ui_trigger_alert_flash();
    app_mqtt_publish_help("Emergency - home button");
}
#endif

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

    /* WiFi then SNTP (for correct time) and MQTT (broker on PC) */
    err = app_wifi_simple_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi not connected; MQTT will not work until WiFi is available");
    } else {
        app_sntp_init();  /* sync time via NTP so UI clock is correct */
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

#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
    esp_err_t ret = bsp_btn_register_callback(BSP_BUTTON_MAIN, BUTTON_PRESS_UP, home_btn_emergency_cb, NULL);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Home button: press to send emergency");
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Speech recognition start");
    app_sr_start(false);
}
