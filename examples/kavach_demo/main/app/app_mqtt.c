/*
 * MQTT client for Kavach: publish help commands, appliance commands, and sensor data.
 * Subscribes to fabacademy/kavach/ping (reply pong) and fabacademy/kavach/gas (gas leak alert).
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "bsp_board.h"
#include "app_mqtt.h"
#include "app_sr_handler.h"
#include "gui/ui_kavach.h"

static const char *TAG = "mqtt";

#define MQTT_URI_MAX 128
#define SENSOR_PAYLOAD_MAX 64
#define APPLIANCE_JSON_MAX 80
#define GAS_PAYLOAD_MAX 96

#define MQTT_TOPIC_PING "fabacademy/kavach/ping"
#define MQTT_TOPIC_PONG "fabacademy/kavach/pong"
#define MQTT_PAYLOAD_PONG "pong"
#define MQTT_TOPIC_GAS      "fabacademy/kavach/gas"
#define MQTT_TOPIC_INTRUDER "fabacademy/kavach/intruder"
#define INTRUDER_PAYLOAD_MAX 96
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

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        s_connected = true;
        if (s_sensor_timer) {
            esp_timer_start_periodic(s_sensor_timer, (uint64_t)CONFIG_KAVACH_MQTT_SENSOR_INTERVAL_SEC * 1000000);
        }
        /* Subscribe to ping topic so app can check device is online */
        if (esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_PING, 0) < 0) {
            ESP_LOGW(TAG, "Subscribe to %s failed", MQTT_TOPIC_PING);
        } else {
            ESP_LOGI(TAG, "Subscribed to %s (reply on %s)", MQTT_TOPIC_PING, MQTT_TOPIC_PONG);
        }
        /* Subscribe to gas leak topic: message only when leak (JSON with device, gas, state "LEAK") */
        if (esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_GAS, 0) < 0) {
            ESP_LOGW(TAG, "Subscribe to %s failed", MQTT_TOPIC_GAS);
        } else {
            ESP_LOGI(TAG, "Subscribed to %s (gas leak alert)", MQTT_TOPIC_GAS);
        }
        if (esp_mqtt_client_subscribe(s_client, MQTT_TOPIC_INTRUDER, 0) < 0) {
            ESP_LOGW(TAG, "Subscribe to %s failed", MQTT_TOPIC_INTRUDER);
        } else {
            ESP_LOGI(TAG, "Subscribed to %s (intruder/motion alert)", MQTT_TOPIC_INTRUDER);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        s_connected = false;
        if (s_sensor_timer) {
            esp_timer_stop(s_sensor_timer);
        }
        break;

    case MQTT_EVENT_DATA: {
        esp_mqtt_event_handle_t evt = (esp_mqtt_event_handle_t)event_data;
        if (!evt || evt->topic_len <= 0) {
            break;
        }
        /* Ping: reply with pong */
        if ((size_t)evt->topic_len == strlen(MQTT_TOPIC_PING) &&
            strncmp(evt->topic, MQTT_TOPIC_PING, evt->topic_len) == 0) {
            int msg_id = esp_mqtt_client_publish(s_client, MQTT_TOPIC_PONG, MQTT_PAYLOAD_PONG, -1, 0, 0);
            if (msg_id >= 0) {
                ESP_LOGI(TAG, "Ping received → pong sent");
            }
            break;
        }
        /* Gas leak: topic publishes only on leak; payload e.g. {"device":"gas_sensor","gas":99,"state":"LEAK"} */
        if ((size_t)evt->topic_len == strlen(MQTT_TOPIC_GAS) &&
            strncmp(evt->topic, MQTT_TOPIC_GAS, evt->topic_len) == 0 && evt->data_len > 0) {
            static char gas_buf[GAS_PAYLOAD_MAX];
            size_t copy_len = (size_t)evt->data_len < (sizeof(gas_buf) - 1) ? (size_t)evt->data_len : (sizeof(gas_buf) - 1);
            memcpy(gas_buf, evt->data, copy_len);
            gas_buf[copy_len] = '\0';
            if (strstr(gas_buf, "LEAK") != NULL) {
                ESP_LOGW(TAG, "Gas leak alert received: %s", gas_buf);
                kavach_ui_trigger_gas_leak_alert();  /* full-screen; dismiss returns to clock */
                sr_handler_play_gas_alarm();        /* play gas_alarm.wav from spiffs */
            }
        }
        /* Intruder: topic fabacademy/kavach/intruder, payload JSON with device id and "motion" field (e.g. {"device":"pir_sensor","motion":"detected"}) */
        if ((size_t)evt->topic_len == strlen(MQTT_TOPIC_INTRUDER) &&
            strncmp(evt->topic, MQTT_TOPIC_INTRUDER, evt->topic_len) == 0 && evt->data_len > 0) {
            static char intruder_buf[INTRUDER_PAYLOAD_MAX];
            size_t copy_len = (size_t)evt->data_len < (sizeof(intruder_buf) - 1) ? (size_t)evt->data_len : (sizeof(intruder_buf) - 1);
            memcpy(intruder_buf, evt->data, copy_len);
            intruder_buf[copy_len] = '\0';
            if (strstr(intruder_buf, "\"motion\"") != NULL) {
                ESP_LOGW(TAG, "Intruder/motion alert received: %s", intruder_buf);
                kavach_ui_trigger_intruder_alert();
            }
        }
        break;
    }

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
