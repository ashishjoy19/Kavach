/*
 * MQTT client for Kavach: publish only.
 * - Help/alert/call commands → fabacademy/kavach/help (for emergency contacts).
 * - Appliance commands → fabacademy/kavach/appliances (for your app to control IoT devices).
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start MQTT client (connect only; no subscribe). Call after WiFi connected. */
esp_err_t app_mqtt_start(void);

/** Publish help-related command (I need help, Send alert, Call family, Help). */
esp_err_t app_mqtt_publish_help(const char *cmd_str);

/** Publish appliance command (light on/off, play, pause, etc.). */
esp_err_t app_mqtt_publish_appliance(const char *cmd_str);

/** Publish temperature and humidity (e.g. to fabacademy/kavach/sensor). Payload "temp=25.3,hum=60". */
esp_err_t app_mqtt_publish_sensor(float temp_c, float humidity_pct);

/** Return true if MQTT is connected. */
bool app_mqtt_connected(void);

#ifdef __cplusplus
}
#endif
