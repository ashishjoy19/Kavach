/*
 * MQTT client for Kavach: publish help commands, appliance commands, and sensor data.
 * No subscribe; your app uses these messages for emergency contacts, IoT control, and monitoring.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "bsp_board.h"
#include "app_mqtt.h"

static const char *TAG = "mqtt";

#define MQTT_URI_MAX 128
#define SENSOR_PAYLOAD_MAX 64
#define APPLIANCE_JSON_MAX 80
static char s_mqtt_uri[MQTT_URI_MAX];
static char s_sensor_payload[SENSOR_PAYLOAD_MAX];
static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static esp_timer_handle_t s_sensor_timer = NULL;

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

static void sensor_timer_cb(void *arg);

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        s_connected = true;
        if (s_sensor_timer) {
            esp_timer_start_periodic(s_sensor_timer, (uint64_t)CONFIG_KAVACH_MQTT_SENSOR_INTERVAL_SEC * 1000000);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        s_connected = false;
        if (s_sensor_timer) {
            esp_timer_stop(s_sensor_timer);
        }
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

static void sensor_timer_cb(void *arg)
{
    float temp = 0, hum = 0;
    if (bsp_board_get_sensor_handle()->get_humiture(&temp, &hum) != ESP_OK) {
        return;
    }
    (void)snprintf(s_sensor_payload, sizeof(s_sensor_payload), "{\"temp\": %.1f, \"hum\": %.0f}", (double)temp, (double)hum);
    publish_to(CONFIG_KAVACH_MQTT_TOPIC_SENSOR, s_sensor_payload);
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

    /* Create periodic timer for sensor publish (started when MQTT connects) */
    const esp_timer_create_args_t timer_args = {
        .callback = &sensor_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mqtt_sensor",
    };
    if (esp_timer_create(&timer_args, &s_sensor_timer) == ESP_OK) {
        /* started in MQTT_EVENT_CONNECTED */
    } else {
        s_sensor_timer = NULL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(err));
        if (s_sensor_timer) {
            esp_timer_delete(s_sensor_timer);
            s_sensor_timer = NULL;
        }
        return err;
    }

    ESP_LOGI(TAG, "MQTT start (broker %s, user %s); help→%s, appliances→%s, sensor→%s (%ds)",
            uri,
            strlen(CONFIG_KAVACH_MQTT_USERNAME) > 0 ? CONFIG_KAVACH_MQTT_USERNAME : "(none)",
            CONFIG_KAVACH_MQTT_TOPIC_HELP,
            CONFIG_KAVACH_MQTT_TOPIC_APPLIANCES,
            CONFIG_KAVACH_MQTT_TOPIC_SENSOR,
            (int)CONFIG_KAVACH_MQTT_SENSOR_INTERVAL_SEC);
    return ESP_OK;
}

esp_err_t app_mqtt_publish_help(const char *cmd_str)
{
    return publish_to(CONFIG_KAVACH_MQTT_TOPIC_HELP, cmd_str);
}

static char s_appliance_payload[APPLIANCE_JSON_MAX];

esp_err_t app_mqtt_publish_appliance_json(const char *device, const char *state)
{
    if (!device || !state) {
        return ESP_ERR_INVALID_ARG;
    }
    int n = snprintf(s_appliance_payload, sizeof(s_appliance_payload),
                     "{\"device\":\"%s\",\"state\":\"%s\"}", device, state);
    if (n < 0 || (size_t)n >= sizeof(s_appliance_payload)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return publish_to(CONFIG_KAVACH_MQTT_TOPIC_APPLIANCES, s_appliance_payload);
}

esp_err_t app_mqtt_publish_sensor(float temp_c, float humidity_pct)
{
    (void)snprintf(s_sensor_payload, sizeof(s_sensor_payload), "{\"temp\": %.1f, \"hum\": %.0f}", (double)temp_c, (double)humidity_pct);
    return publish_to(CONFIG_KAVACH_MQTT_TOPIC_SENSOR, s_sensor_payload);
}

bool app_mqtt_connected(void)
{
    return s_connected;
}
