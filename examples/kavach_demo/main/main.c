/*
 * Kavach: voice recognition + MQTT. Publishes voice commands to broker, subscribes for appliance control.
 * Home button: short press = emergency; long press = setup IR remote learning for AC.
 */
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "settings.h"
#include "app_sr.h"
#include "app_wifi_simple.h"
#include "app_sntp.h"
#include "app_mqtt.h"
#include "app_ir.h"
#include "gui/ui_kavach.h"
#include "bsp_board.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "main";

#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
static void home_btn_emergency_cb(void *arg, void *user_data)
{
    (void)arg;
    (void)user_data;
    /* Ignore short-press when IR learn is active (long-press started IR learn; release must not trigger emergency) */
    if (app_ir_learn_is_active()) {
        return;
    }
    ESP_LOGI(TAG, "Home button: sending emergency");
    kavach_ui_set_status("Alert sent");
    kavach_ui_set_light(KAVACH_LIGHT_ALERT);
    kavach_ui_trigger_alert_flash();
    app_mqtt_publish_help("Emergency - home button");
}

static void home_btn_ir_learn_done_cb(bool ok, void *user_data)
{
    (void)user_data;
    if (ok) {
        kavach_ui_set_status_async_ir("Remote recorded! Say \"Turn on/off the Air\" to control AC.");
        kavach_ui_set_light_async(KAVACH_LIGHT_COMMAND_OK);
    } else {
        kavach_ui_set_status_async_ir("IR learn failed or cancelled. Long-press home to try again.");
        kavach_ui_set_light_async(KAVACH_LIGHT_IDLE);
    }
}

static void home_btn_long_press_ir_learn_cb(void *arg, void *user_data)
{
    (void)arg;
    (void)user_data;
    if (app_ir_learn_is_active()) {
        return;
    }
    ESP_LOGI(TAG, "Home button long press: start IR learning for AC");
    kavach_ui_set_voice_mode(true);  /* Show status + light (hidden in clock mode) */
    kavach_ui_set_status_async_ir("IR learn: press AC On then AC Off");
    kavach_ui_set_light_async(KAVACH_LIGHT_LISTENING);
    app_ir_learn_start(home_btn_ir_learn_done_cb, NULL);
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
    /* Voice WAVs: use /spiffs/ or /storage/ (this board BSP has no SD card driver; put beep.wav, echo_en_ok.wav in SPIFFS) */
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
    bsp_display_backlight_on();  /* Show splash immediately when powered on */
    vTaskDelay(pdMS_TO_TICKS(300));

    app_ir_init();

#if !CONFIG_BSP_BOARD_ESP32_S3_BOX_Lite
    esp_err_t ret = bsp_btn_register_callback(BSP_BUTTON_MAIN, BUTTON_PRESS_UP, home_btn_emergency_cb, NULL);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Home button: short press = emergency");
    }
    ret = bsp_btn_register_callback(BSP_BUTTON_MAIN, BUTTON_LONG_PRESS_START, home_btn_long_press_ir_learn_cb, NULL);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Home button: long press = IR learning for AC");
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Speech recognition start");
    app_sr_start(false);
}
