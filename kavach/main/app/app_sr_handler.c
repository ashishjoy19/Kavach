/*
 * Kavach: minimal SR handler – text + light; optional voice confirmation (SPIFFS/SD).
 * Set to 1 to play WAV confirmations; 0 to save SPIFFS until you add SD card.
 */
#define KAVACH_VOICE_CONFIRM 0

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "app_sr.h"
#include "app_sr_handler.h"
#include "ui_kavach.h"
#include "settings.h"
#include "app_mqtt.h"
#include "bsp_board.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "sr_handler";

static volatile bool s_echo_playing = false;

bool sr_echo_is_playing(void)
{
    return s_echo_playing;
}

/* Find "data" chunk in WAV and return pointer + length; *data_len 4-byte aligned. */
static bool wav_find_data(uint8_t *buf, size_t buf_len, uint8_t **out_pcm, size_t *out_len,
                          uint32_t *sample_rate, uint16_t *bits_per_sample)
{
    if (buf_len < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        return false;
    }
    size_t pos = 12;
    uint32_t sr = 16000;
    uint16_t bps = 16;

    while (pos + 8 <= buf_len) {
        uint32_t chunk_len = (uint32_t)buf[pos + 4] | ((uint32_t)buf[pos + 5] << 8)
                             | ((uint32_t)buf[pos + 6] << 16) | ((uint32_t)buf[pos + 7] << 24);
        if (pos + 8 + chunk_len > buf_len) {
            break;
        }
        if (memcmp(buf + pos, "fmt ", 4) == 0 && chunk_len >= 16) {
            sr = (uint32_t)buf[pos + 12] | ((uint32_t)buf[pos + 13] << 8)
                 | ((uint32_t)buf[pos + 14] << 16) | ((uint32_t)buf[pos + 15] << 24);
            bps = (uint16_t)buf[pos + 22] | ((uint16_t)buf[pos + 23] << 8);
        } else if (memcmp(buf + pos, "data", 4) == 0) {
            *out_pcm = buf + pos + 8;
            *out_len = chunk_len & (size_t)~3u;
            *sample_rate = sr;
            *bits_per_sample = bps ? bps : 16;
            return true;
        }
        pos += 8 + chunk_len;
    }
    return false;
}

/**
 * Confirmation message type: different voice per command.
 * Add WAVs to spiffs: echo_en_<suffix>.wav / echo_cn_<suffix>.wav
 * If missing, falls back to "ok".
 */
typedef enum {
    CONFIRM_OK = 0,
    CONFIRM_ALERTED,   /* "Alert sent" / "Alerted" */
    CONFIRM_CALLING,   /* "Calling" / "Calling your son" */
    CONFIRM_HELP,      /* Help summary */
    CONFIRM_MAX
} confirm_type_t;

static const char *confirm_suffix_en[] = { "ok", "alerted", "calling", "help" };
static const char *confirm_suffix_cn[] = { "ok", "alerted", "calling", "help" };

/* Play confirmation WAV from SPIFFS (or SD later). No-op when KAVACH_VOICE_CONFIRM is 0. */
static void play_confirmation_wav(confirm_type_t type)
{
#if !KAVACH_VOICE_CONFIRM
    (void)type;
    return;
#endif
    if (type >= CONFIRM_MAX) {
        type = CONFIRM_OK;
    }
    bool is_cn = (settings_get_parameter()->sr_lang == SR_LANG_CN);
    const char **suffixes = is_cn ? confirm_suffix_cn : confirm_suffix_en;
    const char *suffix = suffixes[type];

    static const char *prefixes[] = { "/spiffs/", "/storage/" };
    FILE *f = NULL;
    static char path_used[64];
    const char *name_fmt = is_cn ? "echo_cn_%s.wav" : "echo_en_%s.wav";

    /* Try type-specific file first */
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        snprintf(path_used, sizeof(path_used), "%s", prefixes[i]);
        snprintf(path_used + strlen(path_used), (size_t)(sizeof(path_used) - strlen(path_used)), name_fmt, suffix);
        f = fopen(path_used, "rb");
        if (f) {
            break;
        }
    }
    /* Fallback to ok if specific file not found */
    if (!f && type != CONFIRM_OK) {
        for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
            snprintf(path_used, sizeof(path_used), "%s", prefixes[i]);
            snprintf(path_used + strlen(path_used), (size_t)(sizeof(path_used) - strlen(path_used)), name_fmt, "ok");
            f = fopen(path_used, "rb");
            if (f) {
                break;
            }
        }
    }
    if (!f) {
        ESP_LOGW(TAG, "Confirmation WAV not found. Do full flash: idf.py flash");
        return;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 44 || fsize > 128 * 1024) {
        ESP_LOGW(TAG, "WAV size invalid: %ld", (long)fsize);
        fclose(f);
        return;
    }

    uint8_t *buf = heap_caps_malloc((size_t)fsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc((size_t)fsize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buf) {
        fclose(f);
        return;
    }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        heap_caps_free(buf);
        fclose(f);
        return;
    }
    fclose(f);

    uint8_t *pcm = NULL;
    size_t pcm_len = 0;
    uint32_t sample_rate = 16000;
    uint16_t bits_per_sample = 16;

    if (!wav_find_data(buf, (size_t)fsize, &pcm, &pcm_len, &sample_rate, &bits_per_sample)) {
        ESP_LOGW(TAG, "WAV format invalid");
        heap_caps_free(buf);
        return;
    }

    ESP_LOGI(TAG, "Playing OK WAV: %s, rate=%lu, bits=%u, len=%u", path_used,
             (unsigned long)sample_rate, (unsigned)bits_per_sample, (unsigned)pcm_len);

    /* Set codec and volume before unmute */
    bsp_codec_volume_set(100, NULL);
    bsp_codec_set_fs(sample_rate, (uint32_t)bits_per_sample, I2S_SLOT_MODE_STEREO);
    bsp_codec_mute_set(true);
    vTaskDelay(pdMS_TO_TICKS(20));
    bsp_codec_mute_set(false);
    vTaskDelay(pdMS_TO_TICKS(50));

    s_echo_playing = true;
    size_t written = 0;
    esp_err_t err = bsp_i2s_write((char *)pcm, pcm_len, &written, portMAX_DELAY);
    s_echo_playing = false;

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S write failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "OK played, %u bytes", (unsigned)written);
    }

    vTaskDelay(pdMS_TO_TICKS(30));
    bsp_codec_volume_set(settings_get_parameter()->volume, NULL);
    heap_caps_free(buf);
}

void sr_handler_task(void *pvParam)
{
    (void)pvParam;

    while (true) {
        sr_result_t result;
        app_sr_get_result(&result, portMAX_DELAY);

        if (result.state == ESP_MN_STATE_TIMEOUT) {
            kavach_ui_set_status("Timeout");
            kavach_ui_set_light(KAVACH_LIGHT_IDLE);
            continue;
        }

        if (result.wakenet_mode == WAKENET_DETECTED) {
            kavach_ui_set_status("Say command");
            kavach_ui_set_light(KAVACH_LIGHT_LISTENING);
            continue;
        }

        if (result.state == ESP_MN_STATE_DETECTED) {
            const sr_cmd_t *cmd = app_sr_get_cmd_from_id(result.command_id);
            if (!cmd) {
                continue;
            }
            ESP_LOGI(TAG, "command: %s, id: %d", cmd->str, (int)cmd->cmd);

            switch (cmd->cmd) {
            case SR_CMD_HELP_ALERT:
                kavach_ui_set_status("Alert sent");
                kavach_ui_set_light(KAVACH_LIGHT_ALERT);
                play_confirmation_wav(CONFIRM_ALERTED);
                app_mqtt_publish_help(cmd->str);  /* → kavach/help for emergency contacts */
                break;

            case SR_CMD_CALL_FAMILY:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_CALLING);
                app_mqtt_publish_help(cmd->str);  /* → kavach/help for emergency contacts */
                break;

            case SR_CMD_HELP:
                kavach_ui_set_status("Help");
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_HELP);
                app_mqtt_publish_help(cmd->str);  /* → kavach/help */
                break;

            default:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                app_mqtt_publish_appliance(cmd->str);  /* → kavach/appliances for IoT control */
                break;
            }
        }
    }
    vTaskDelete(NULL);
}
