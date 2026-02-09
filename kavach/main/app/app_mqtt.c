/*
 * MQTT client for Kavach: publish help commands to one topic, appliance commands to another.
 * No subscribe; your app uses these messages for emergency contacts and to control IoT devices.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "app_mqtt.h"

static const char *TAG = "mqtt";

#define MQTT_URI_MAX 128
static char s_mqtt_uri[MQTT_URI_MAX];
static esp_mqtt_client_handle_t s_client;
static bool s_connected;

/* Build full URI if config is just hostname (e.g. mqtt.fabcloud.org → mqtt://mqtt.fabcloud.org:1883) */
static const char *get_broker_uri(void)
{
    const char *cfg = CONFIG_KAVACH_MQTT_BROKER_URI;
    if (strncmp(cfg, "mqtt://", 7) == 0 || strncmp(cfg, "mqtts://", 8) == 0) {
        return cfg;
    }
    snprintf(s_mqtt_uri, sizeof(s_mqtt_uri), "mqtt://%s:1883", cfg);
    return s_mqtt_uri;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        s_connected = true;
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        s_connected = false;
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

static esp_err_t publish_to(const char *topic, const char *payload)
{
    if (!s_client || !s_connected || !topic || !payload) {
        return ESP_ERR_INVALID_STATE;
    }
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t app_mqtt_start(void)
{
    const char *uri = get_broker_uri();
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
    };

    /* Optional username/password for public or secured brokers */
    if (strlen(CONFIG_KAVACH_MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username = CONFIG_KAVACH_MQTT_USERNAME;
        if (strlen(CONFIG_KAVACH_MQTT_PASSWORD) > 0) {
            mqtt_cfg.credentials.authentication.password = CONFIG_KAVACH_MQTT_PASSWORD;
        }
    }

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        return ESP_ERR_NO_MEM;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "MQTT start (broker %s, user %s); help→%s, appliances→%s",
            uri,
            strlen(CONFIG_KAVACH_MQTT_USERNAME) > 0 ? CONFIG_KAVACH_MQTT_USERNAME : "(none)",
            CONFIG_KAVACH_MQTT_TOPIC_HELP,
            CONFIG_KAVACH_MQTT_TOPIC_APPLIANCES);
    return ESP_OK;
}

esp_err_t app_mqtt_publish_help(const char *cmd_str)
{
    return publish_to(CONFIG_KAVACH_MQTT_TOPIC_HELP, cmd_str);
}

esp_err_t app_mqtt_publish_appliance(const char *cmd_str)
{
    return publish_to(CONFIG_KAVACH_MQTT_TOPIC_APPLIANCES, cmd_str);
}

bool app_mqtt_connected(void)
{
    return s_connected;
}
