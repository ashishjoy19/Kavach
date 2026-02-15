/*
 * Kavach: minimal SR handler – text + light; voice confirmation from SD card (or SPIFFS).
 * WAV files: on SD card use /sdcard/beep.wav (wake), /sdcard/echo_en_ok.wav etc (commands).
 * If a command WAV is missing, falls back to "ok". Set to 0 to disable all playback.
 */
#define KAVACH_VOICE_CONFIRM 1

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "app_sr.h"
#include "app_sr_handler.h"
#include "app_ir.h"
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

/* Paths to try for WAV files: SD card first (user-provided), then SPIFFS/storage. */
static const char *wav_prefixes[] = { "/sdcard/", "/spiffs/", "/storage/" };
#define WAV_PREFIX_NUM (sizeof(wav_prefixes) / sizeof(wav_prefixes[0]))

/* Playback request: run in a separate task so SR handler and AFE feed are not blocked. */
typedef struct {
    bool is_beep;
    bool is_gas_alarm;
    confirm_type_t confirm_type;
} play_req_t;

#define PLAY_QUEUE_LEN  4
static QueueHandle_t s_play_queue = NULL;
static TaskHandle_t s_play_task_handle = NULL;
static bool s_play_task_created = false;
/** Set by UI when gas alert overlay is dismissed; run_gas_alarm_playback checks this and stops. */
static volatile bool s_gas_alert_dismissed = false;

/* Play a WAV file by path. Returns true if played, false if file missing/invalid. */
static bool play_wav_by_path(const char *path)
{
    if (!path || !path[0]) {
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 44 || fsize > 128 * 1024) {
        fclose(f);
        return false;
    }
    uint8_t *buf = heap_caps_malloc((size_t)fsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc((size_t)fsize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buf) {
        fclose(f);
        return false;
    }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        heap_caps_free(buf);
        fclose(f);
        return false;
    }
    fclose(f);

    uint8_t *pcm = NULL;
    size_t pcm_len = 0;
    uint32_t sample_rate = 16000;
    uint16_t bits_per_sample = 16;
    if (!wav_find_data(buf, (size_t)fsize, &pcm, &pcm_len, &sample_rate, &bits_per_sample)) {
        heap_caps_free(buf);
        return false;
    }

    /* Do NOT reconfigure codec sample rate: AFE capture runs at 16 kHz. Changing it causes
     * I2S conflict ("Pending out channel for in channel running") and "rb_out slow" / crash.
     * Only play 16 kHz WAVs so we leave the codec at 16 kHz. */
    if (sample_rate != 16000) {
        ESP_LOGW(TAG, "Skip %s: need 16 kHz WAV (file is %lu Hz). Re-export as 16 kHz.", path, (unsigned long)sample_rate);
        heap_caps_free(buf);
        return false;
    }
    if (bits_per_sample != 16) {
        ESP_LOGW(TAG, "Skip %s: need 16-bit WAV (file is %u-bit).", path, (unsigned)bits_per_sample);
        heap_caps_free(buf);
        return false;
    }

    /* I2S/codec often runs at 48 kHz. Our WAV is 16 kHz; if we send it raw, it plays 3x fast.
     * Upsample 16k -> 48k with linear interpolation so it sounds natural (not blocky/creepy). */
    const unsigned int upsample_factor = 3;
    size_t num_in = pcm_len / 2;
    size_t num_out = num_in * upsample_factor;
    size_t out_len = num_out * 2;
    uint8_t *out_buf = heap_caps_malloc(out_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out_buf) {
        out_buf = heap_caps_malloc(out_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (out_buf) {
        for (size_t o_samp = 0; o_samp < num_out; o_samp++) {
            size_t idx = o_samp / upsample_factor;
            unsigned int frac = o_samp % upsample_factor;
            int16_t v0 = (int16_t)((uint16_t)pcm[idx * 2] | ((uint16_t)pcm[idx * 2 + 1] << 8));
            int16_t v1 = (idx + 1 < num_in)
                         ? (int16_t)((uint16_t)pcm[(idx + 1) * 2] | ((uint16_t)pcm[(idx + 1) * 2 + 1] << 8))
                         : v0;
            int32_t interp = (v0 * (int32_t)(upsample_factor - frac) + v1 * (int32_t)frac) / (int32_t)upsample_factor;
            if (interp > 32767) {
                interp = 32767;
            } else if (interp < -32768) {
                interp = -32768;
            }
            uint16_t u = (uint16_t)(int16_t)interp;
            out_buf[o_samp * 2] = (uint8_t)(u & 0xff);
            out_buf[o_samp * 2 + 1] = (uint8_t)(u >> 8);
        }
    }

    bsp_codec_volume_set(100, NULL);
    bsp_codec_mute_set(true);
    vTaskDelay(pdMS_TO_TICKS(20));
    bsp_codec_mute_set(false);
    vTaskDelay(pdMS_TO_TICKS(50));

    s_echo_playing = true;
    size_t written = 0;
    if (out_buf) {
        esp_err_t err = bsp_i2s_write((char *)out_buf, out_len, &written, portMAX_DELAY);
        heap_caps_free(out_buf);
        (void)err;
    } else {
        bsp_i2s_write((char *)pcm, pcm_len, &written, portMAX_DELAY);
    }
    s_echo_playing = false;

    vTaskDelay(pdMS_TO_TICKS(30));
    bsp_codec_volume_set(settings_get_parameter()->volume, NULL);
    heap_caps_free(buf);
    return true;
}

/* Run wake beep playback (called from playback task only). */
static void run_wake_beep_playback(void)
{
    static const char *beep_names[] = { "beep.wav", "wake.wav" };
    for (size_t n = 0; n < sizeof(beep_names) / sizeof(beep_names[0]); n++) {
        for (size_t i = 0; i < WAV_PREFIX_NUM; i++) {
            char path[64];
            snprintf(path, sizeof(path), "%s%s", wav_prefixes[i], beep_names[n]);
            if (play_wav_by_path(path)) {
                ESP_LOGI(TAG, "Wake beep played: %s", path);
                return;
            }
        }
    }
    ESP_LOGW(TAG, "No wake beep WAV found. Add beep.wav to project spiffs/ folder and reflash (see spiffs/README.txt)");
}

/* Run confirmation WAV playback (called from playback task only). */
static void run_confirm_playback(confirm_type_t type)
{
    if (type >= CONFIRM_MAX) {
        type = CONFIRM_OK;
    }
    bool is_cn = (settings_get_parameter()->sr_lang == SR_LANG_CN);
    const char **suffixes = is_cn ? confirm_suffix_cn : confirm_suffix_en;
    const char *suffix = suffixes[type];
    const char *name_fmt = is_cn ? "echo_cn_%s.wav" : "echo_en_%s.wav";
    static char path_used[64];

    for (size_t i = 0; i < WAV_PREFIX_NUM; i++) {
        snprintf(path_used, sizeof(path_used), "%s", wav_prefixes[i]);
        snprintf(path_used + strlen(path_used), (size_t)(sizeof(path_used) - strlen(path_used)), name_fmt, suffix);
        if (play_wav_by_path(path_used)) {
            return;
        }
    }
    if (type != CONFIRM_OK) {
        for (size_t i = 0; i < WAV_PREFIX_NUM; i++) {
            snprintf(path_used, sizeof(path_used), "%s", wav_prefixes[i]);
            snprintf(path_used + strlen(path_used), (size_t)(sizeof(path_used) - strlen(path_used)), name_fmt, "ok");
            if (play_wav_by_path(path_used)) {
                return;
            }
        }
    }
    ESP_LOGW(TAG, "Confirmation WAV not found. Add echo_en_ok.wav (and beep.wav) to project spiffs/ folder and reflash");
}

#define GAS_ALARM_CHUNK  4096u

/* Run gas alarm WAV with chunked write so we can stop when overlay is dismissed. */
static void run_gas_alarm_playback(void)
{
    s_gas_alert_dismissed = false;
    static const char *name = "gas_alarm.wav";
    FILE *f = NULL;
    char path[64];
    for (size_t i = 0; i < WAV_PREFIX_NUM; i++) {
        snprintf(path, sizeof(path), "%s%s", wav_prefixes[i], name);
        f = fopen(path, "rb");
        if (f) {
            break;
        }
    }
    if (!f) {
        ESP_LOGW(TAG, "Gas alarm WAV not found. Add gas_alarm.wav (16 kHz, 16-bit) to spiffs/ folder.");
        return;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 44 || fsize > 128 * 1024) {
        fclose(f);
        return;
    }
    uint8_t *buf = heap_caps_malloc((size_t)fsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc((size_t)fsize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buf || fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        if (buf) {
            heap_caps_free(buf);
        }
        fclose(f);
        return;
    }
    fclose(f);

    uint8_t *pcm = NULL;
    size_t pcm_len = 0;
    uint32_t sample_rate = 16000;
    uint16_t bits_per_sample = 16;
    if (!wav_find_data(buf, (size_t)fsize, &pcm, &pcm_len, &sample_rate, &bits_per_sample) ||
        sample_rate != 16000 || bits_per_sample != 16) {
        heap_caps_free(buf);
        return;
    }

    const unsigned int upsample_factor = 3;
    size_t num_in = pcm_len / 2;
    size_t num_out = num_in * upsample_factor;
    size_t out_len = num_out * 2;
    uint8_t *out_buf = heap_caps_malloc(out_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out_buf) {
        out_buf = heap_caps_malloc(out_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!out_buf) {
        heap_caps_free(buf);
        return;
    }
    for (size_t o_samp = 0; o_samp < num_out; o_samp++) {
        size_t idx = o_samp / upsample_factor;
        unsigned int frac = o_samp % upsample_factor;
        int16_t v0 = (int16_t)((uint16_t)pcm[idx * 2] | ((uint16_t)pcm[idx * 2 + 1] << 8));
        int16_t v1 = (idx + 1 < num_in)
                     ? (int16_t)((uint16_t)pcm[(idx + 1) * 2] | ((uint16_t)pcm[(idx + 1) * 2 + 1] << 8))
                     : v0;
        int32_t interp = (v0 * (int32_t)(upsample_factor - frac) + v1 * (int32_t)frac) / (int32_t)upsample_factor;
        if (interp > 32767) {
            interp = 32767;
        } else if (interp < -32768) {
            interp = -32768;
        }
        uint16_t u = (uint16_t)(int16_t)interp;
        out_buf[o_samp * 2] = (uint8_t)(u & 0xff);
        out_buf[o_samp * 2 + 1] = (uint8_t)(u >> 8);
    }
    heap_caps_free(buf);

    bsp_codec_volume_set(100, NULL);
    bsp_codec_mute_set(true);
    vTaskDelay(pdMS_TO_TICKS(20));
    bsp_codec_mute_set(false);
    vTaskDelay(pdMS_TO_TICKS(50));
    s_echo_playing = true;

    for (size_t offset = 0; offset < out_len && !s_gas_alert_dismissed; offset += GAS_ALARM_CHUNK) {
        size_t chunk = (out_len - offset) < GAS_ALARM_CHUNK ? (out_len - offset) : GAS_ALARM_CHUNK;
        size_t written = 0;
        bsp_i2s_write((char *)out_buf + offset, chunk, &written, portMAX_DELAY);
    }

    s_echo_playing = false;
    vTaskDelay(pdMS_TO_TICKS(30));
    bsp_codec_volume_set(settings_get_parameter()->volume, NULL);
    heap_caps_free(out_buf);
    ESP_LOGI(TAG, "Gas alarm %s", s_gas_alert_dismissed ? "stopped (alert dismissed)" : "played");
}

/* Dedicated task: plays WAVs so SR handler and AFE feed task are not blocked (avoids "rb_out slow" / crash). */
static void playback_task(void *pvParam)
{
    (void)pvParam;
    play_req_t req;
    for (;;) {
        if (xQueueReceive(s_play_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (req.is_gas_alarm) {
            run_gas_alarm_playback();
        } else if (req.is_beep) {
            run_wake_beep_playback();
        } else {
            run_confirm_playback(req.confirm_type);
        }
    }
}

/* Ensure playback queue and task exist (lazy init from SR handler task). */
static void ensure_playback_task(void)
{
    if (s_play_task_created) {
        return;
    }
    if (s_play_queue == NULL) {
        s_play_queue = xQueueCreate(PLAY_QUEUE_LEN, sizeof(play_req_t));
    }
    if (s_play_queue != NULL && s_play_task_handle == NULL) {
        BaseType_t ok = xTaskCreatePinnedToCore(playback_task, "wav_play", 4 * 1024, NULL, 4, &s_play_task_handle, 0);
        if (ok == pdPASS) {
            s_play_task_created = true;
        }
    }
}

/* Post wake beep to playback task and return immediately (no blocking of SR/AFE). */
static void play_wake_beep(void)
{
#if !KAVACH_VOICE_CONFIRM
    return;
#endif
    ensure_playback_task();
    play_req_t req = { .is_beep = true, .is_gas_alarm = false, .confirm_type = CONFIRM_OK };
    if (s_play_queue != NULL) {
        xQueueSend(s_play_queue, &req, 0);
    }
}

/* Post confirmation WAV to playback task and return immediately. */
static void play_confirmation_wav(confirm_type_t type)
{
#if !KAVACH_VOICE_CONFIRM
    (void)type;
    return;
#endif
    ensure_playback_task();
    play_req_t req = { .is_beep = false, .is_gas_alarm = false, .confirm_type = type };
    if (s_play_queue != NULL) {
        xQueueSend(s_play_queue, &req, 0);
    }
}

/** Play gas alarm WAV (spiffs/gas_alarm.wav) when gas leak is detected. Safe to call from any task. */
void sr_handler_play_gas_alarm(void)
{
    ensure_playback_task();
    play_req_t req = { .is_beep = false, .is_gas_alarm = true, .confirm_type = CONFIRM_OK };
    if (s_play_queue != NULL) {
        xQueueSend(s_play_queue, &req, 0);
    }
}

/** Stop gas alarm playback (call when gas alert overlay is dismissed). */
void sr_handler_stop_gas_alarm(void)
{
    s_gas_alert_dismissed = true;
}

void sr_handler_task(void *pvParam)
{
    (void)pvParam;

    while (true) {
        sr_result_t result;
        app_sr_get_result(&result, portMAX_DELAY);

        if (result.state == ESP_MN_STATE_TIMEOUT) {
            kavach_ui_set_voice_mode(false);
            kavach_ui_set_status(app_sr_get_wake_prompt());
            kavach_ui_set_light(KAVACH_LIGHT_IDLE);
            continue;
        }

        if (result.wakenet_mode == WAKENET_DETECTED) {
            play_wake_beep();  /* short beep from /sdcard/beep.wav (or /spiffs/beep.wav) if present */
            kavach_ui_set_voice_mode(true);
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

            case SR_CMD_LIGHT_ON:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                app_mqtt_publish_appliance_json("light1", "ON");
                break;
            case SR_CMD_LIGHT_OFF:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                app_mqtt_publish_appliance_json("light1", "OFF");
                break;
            case SR_CMD_FAN_ON:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                app_mqtt_publish_appliance_json("fan1", "ON");
                break;
            case SR_CMD_FAN_OFF:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                app_mqtt_publish_appliance_json("fan1", "OFF");
                break;
            case SR_CMD_AC_ON:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                if (app_ir_has_ac_codes()) {
                    app_ir_send_ac_on();
                } else {
                    app_mqtt_publish_appliance_json("ac1", "ON");
                }
                break;
            case SR_CMD_AC_OFF:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                if (app_ir_has_ac_codes()) {
                    app_ir_send_ac_off();
                } else {
                    app_mqtt_publish_appliance_json("ac1", "OFF");
                }
                break;
            case SR_CMD_PLAY:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                app_mqtt_publish_appliance_json("player", "PLAY");
                break;
            case SR_CMD_PAUSE:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                app_mqtt_publish_appliance_json("player", "PAUSE");
                break;
            case SR_CMD_NEXT:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                app_mqtt_publish_appliance_json("player", "NEXT");
                break;
            case SR_CMD_SET_RED:
            case SR_CMD_SET_GREEN:
            case SR_CMD_SET_BLUE:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                app_mqtt_publish_appliance_json("light1", cmd->cmd == SR_CMD_SET_RED ? "RED" : cmd->cmd == SR_CMD_SET_GREEN ? "GREEN" : "BLUE");
                break;
            case SR_CMD_CUSTOMIZE_COLOR:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                app_mqtt_publish_appliance_json("light1", "CUSTOMIZE");
                break;
            default:
                kavach_ui_set_status((char *)cmd->str);
                kavach_ui_set_light(KAVACH_LIGHT_COMMAND_OK);
                play_confirmation_wav(CONFIRM_OK);
                app_mqtt_publish_appliance_json("unknown", "ON");
                break;
            }
        }
    }
    vTaskDelete(NULL);
}
