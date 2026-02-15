/*
 * Kavach UI: desktop clock when idle (big time + temp/hum); voice mode when wake word
 * detected (time moves to top-right, status and indicator center).
 */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "ui_kavach.h"
#include "app_sr_handler.h"
#include "lvgl.h"
#include "esp_log.h"
#include "bsp_board.h"

LV_FONT_DECLARE(font_en_12);
LV_FONT_DECLARE(font_en_24);
LV_FONT_DECLARE(font_en_64);
LV_FONT_DECLARE(font_en_bold_36);

static const char *TAG = "ui_kavach";

static lv_obj_t *g_status_label = NULL;
static lv_obj_t *g_light_indicator = NULL;
static lv_obj_t *g_title_label = NULL;
static lv_obj_t *g_temp_label = NULL;
static lv_obj_t *g_hum_label = NULL;
static lv_obj_t *g_time_label = NULL;
static lv_obj_t *g_time_panel = NULL;
static lv_obj_t *g_temp_card = NULL;
static lv_obj_t *g_hum_card = NULL;
static lv_timer_t *g_temp_hum_timer = NULL;
static lv_timer_t *g_clock_timer = NULL;
static lv_timer_t *g_flash_poll_timer = NULL;
static bool g_voice_mode = false;
static volatile bool g_trigger_alert_flash = false;
static volatile bool g_trigger_gas_alert = false;
static volatile bool g_trigger_intruder_alert = false;
#define PENDING_STATUS_LEN 64
#define IR_STATUS_MAX_WIDTH 280
#define OVERLAY_LABEL_MAX_W 280  /* max width for overlay labels so text wraps on screen */
static char g_pending_status_buf[PENDING_STATUS_LEN];
static volatile bool g_pending_status_ready = false;
static volatile bool g_pending_status_ir_style = false;  /* use small font + wrap for IR messages */
static volatile int g_pending_light = -1;  /* -1 = none, else kavach_light_t */

static void temp_hum_timer_cb(lv_timer_t *timer);
static void clock_timer_cb(lv_timer_t *timer);
static void apply_clock_mode(void);
static void apply_voice_mode(void);
static void alert_flash_restore_cb(lv_timer_t *timer);
static void alert_flash_poll_cb(lv_timer_t *timer);

/* Dark theme with teal accent: readable and visually distinct. */
#define COLOR_BG            0x1A2332u   /* dark blue-grey background */
#define COLOR_ALERT_FLASH   0x9B0000u   /* deep red for alerts */
#define COLOR_CARD          0x243447u   /* card: slightly lighter than bg */
#define COLOR_CARD_BORDER   0x26A69Au   /* teal accent border */
#define COLOR_ACCENT        0x4DD0E1u   /* bright cyan for highlights */
#define COLOR_TEXT          0xFFFFFFu   /* primary text */
#define COLOR_TEXT_DIM      0xB0BEC5u   /* secondary: soft grey, still readable */
#define COLOR_CLOCK         0x4DD0E1u   /* time in cyan – stands out, not harsh */
#define COLOR_TITLE         0x80DEEAu   /* title: light cyan */

static const uint32_t light_colors[KAVACH_LIGHT_MAX] = {
    [KAVACH_LIGHT_IDLE]       = 0x78909Cu,  /* blue-grey */
    [KAVACH_LIGHT_LISTENING]  = 0x69F0AEu,  /* bright teal-green */
    [KAVACH_LIGHT_COMMAND_OK] = 0x40C4FFu,  /* bright cyan-blue */
    [KAVACH_LIGHT_ALERT]      = 0xFF5252u,  /* bright red */
};

void kavach_ui_set_voice_mode(bool voice_mode)
{
    if (g_voice_mode == voice_mode) {
        return;
    }
    g_voice_mode = voice_mode;
    if (voice_mode) {
        apply_voice_mode();
    } else {
        apply_clock_mode();
    }
}

static void apply_clock_mode(void)
{
    /* Desktop clock: big time center, temp/hum below */
    if (g_time_panel) {
        lv_obj_set_size(g_time_panel, 260, 110);
        lv_obj_align(g_time_panel, LV_ALIGN_CENTER, 0, -30);
    }
    if (g_time_label) {
        lv_obj_set_style_text_font(g_time_label, &font_en_64, LV_PART_MAIN);
        lv_obj_center(g_time_label);
    }
    if (g_status_label) {
        lv_obj_add_flag(g_status_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_light_indicator) {
        lv_obj_add_flag(g_light_indicator, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_title_label) {
        lv_obj_clear_flag(g_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(g_title_label, &font_en_24, LV_PART_MAIN);
        lv_obj_align(g_title_label, LV_ALIGN_TOP_MID, 0, 10);
    }
    if (g_temp_card) {
        lv_obj_clear_flag(g_temp_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(g_temp_card, LV_ALIGN_CENTER, -75, 72);
    }
    if (g_hum_card) {
        lv_obj_clear_flag(g_hum_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(g_hum_card, LV_ALIGN_CENTER, 75, 72);
    }
}

static void apply_voice_mode(void)
{
    /* Voice mode: time top-right small, status and indicator center */
    if (g_time_panel) {
        lv_obj_set_size(g_time_panel, 100, 44);
        lv_obj_align(g_time_panel, LV_ALIGN_TOP_RIGHT, -12, 10);
    }
    if (g_time_label) {
        lv_obj_set_style_text_font(g_time_label, &font_en_24, LV_PART_MAIN);
        lv_obj_center(g_time_label);
    }
    if (g_status_label) {
        lv_obj_clear_flag(g_status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(g_status_label, LV_ALIGN_CENTER, 0, -28);
    }
    if (g_light_indicator) {
        lv_obj_clear_flag(g_light_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(g_light_indicator, LV_ALIGN_CENTER, 0, 28);
    }
    if (g_title_label) {
        lv_obj_clear_flag(g_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(g_title_label, &font_en_24, LV_PART_MAIN);
        lv_obj_align(g_title_label, LV_ALIGN_TOP_LEFT, 12, 10);
    }
    /* Hide temp and humidity in voice mode */
    if (g_temp_card) {
        lv_obj_add_flag(g_temp_card, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_hum_card) {
        lv_obj_add_flag(g_hum_card, LV_OBJ_FLAG_HIDDEN);
    }
}

void kavach_ui_start(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);

    /* Title – same font (en_24) in both clock and voice mode; position set in apply_*_mode */
    g_title_label = lv_label_create(scr);
    lv_label_set_text_static(g_title_label, "Kavach");
    lv_obj_set_style_text_font(g_title_label, &font_en_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_title_label, lv_color_hex(COLOR_TITLE), LV_PART_MAIN);

    /* Time panel – size/position set by mode */
    g_time_panel = lv_obj_create(scr);
    lv_obj_set_scrollbar_mode(g_time_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(g_time_panel, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_radius(g_time_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(g_time_panel, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(g_time_panel, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(g_time_panel, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_time_panel, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_time_panel, lv_color_hex(COLOR_CARD_BORDER), LV_PART_MAIN);

    g_time_label = lv_label_create(g_time_panel);
    lv_label_set_text_static(g_time_label, "--:--");
    lv_obj_set_style_text_color(g_time_label, lv_color_hex(COLOR_CLOCK), LV_PART_MAIN);
    g_clock_timer = lv_timer_create(clock_timer_cb, 1000, (void *)g_time_label);
    lv_timer_set_repeat_count(g_clock_timer, -1);
    clock_timer_cb(g_clock_timer);

    /* Status and light – shown only in voice mode */
    g_status_label = lv_label_create(scr);
    lv_label_set_text_static(g_status_label, "Ready");
    lv_obj_set_style_text_font(g_status_label, &font_en_bold_36, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    g_light_indicator = lv_obj_create(scr);
    lv_obj_set_size(g_light_indicator, 52, 52);
    lv_obj_set_style_radius(g_light_indicator, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_light_indicator, lv_color_hex(light_colors[KAVACH_LIGHT_IDLE]), LV_PART_MAIN);
    lv_obj_set_style_border_width(g_light_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(g_light_indicator, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(g_light_indicator, lv_color_hex(light_colors[KAVACH_LIGHT_IDLE]), LV_PART_MAIN);

    /* Temp card: numbers on top, small label directly under */
    g_temp_card = lv_obj_create(scr);
    lv_obj_set_size(g_temp_card, 110, 64);
    lv_obj_set_style_bg_color(g_temp_card, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_radius(g_temp_card, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(g_temp_card, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(g_temp_card, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(g_temp_card, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_temp_card, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_temp_card, lv_color_hex(COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(g_temp_card, LV_SCROLLBAR_MODE_OFF);

    g_temp_label = lv_label_create(g_temp_card);
    lv_label_set_text_static(g_temp_label, "-- °C");
    lv_obj_set_style_text_font(g_temp_label, &font_en_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_temp_label, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(g_temp_label, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *temp_lab = lv_label_create(g_temp_card);
    lv_label_set_text_static(temp_lab, "Temp");
    lv_obj_set_style_text_font(temp_lab, &font_en_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(temp_lab, lv_color_hex(COLOR_TEXT_DIM), LV_PART_MAIN);
    lv_obj_align(temp_lab, LV_ALIGN_TOP_MID, 0, 34);

    /* Hum card: numbers on top, small label directly under */
    g_hum_card = lv_obj_create(scr);
    lv_obj_set_size(g_hum_card, 110, 64);
    lv_obj_set_style_bg_color(g_hum_card, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_radius(g_hum_card, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(g_hum_card, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(g_hum_card, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(g_hum_card, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_hum_card, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_hum_card, lv_color_hex(COLOR_CARD_BORDER), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(g_hum_card, LV_SCROLLBAR_MODE_OFF);

    g_hum_label = lv_label_create(g_hum_card);
    lv_label_set_text_static(g_hum_label, "--%");
    lv_obj_set_style_text_font(g_hum_label, &font_en_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_hum_label, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_align(g_hum_label, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *hum_lab = lv_label_create(g_hum_card);
    lv_label_set_text_static(hum_lab, "Humidity");
    lv_obj_set_style_text_font(hum_lab, &font_en_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(hum_lab, lv_color_hex(COLOR_TEXT_DIM), LV_PART_MAIN);
    lv_obj_align(hum_lab, LV_ALIGN_TOP_MID, 0, 34);

    g_temp_hum_timer = lv_timer_create(temp_hum_timer_cb, 3000, NULL);
    lv_timer_set_repeat_count(g_temp_hum_timer, -1);
    temp_hum_timer_cb(g_temp_hum_timer);

    g_voice_mode = false;
    apply_clock_mode();

    g_flash_poll_timer = lv_timer_create(alert_flash_poll_cb, 150, NULL);
    lv_timer_set_repeat_count(g_flash_poll_timer, -1);

    /* ========== TEMPORARY: display one image from SPIFFS ==========
     * Put img1.png in spiffs/ folder, then: idf.py build flash (full flash).
     * S: maps to /spiffs (LV_FS_POSIX_PATH). Remove block when done. */
    lv_obj_t *img1 = lv_img_create(scr);
    lv_img_set_src(img1, "S:/img1.png");   /* = /spiffs/img1.png */
    lv_obj_align(img1, LV_ALIGN_CENTER, 0, 0);
    /* ========== END TEMPORARY ========== */

    ESP_LOGI(TAG, "Kavach UI started (clock + voice modes)");
}

void kavach_ui_splash_finish(void)
{
    /* Splash removed; no-op for compatibility with main.c */
}

static void clock_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *lab_time = (lv_obj_t *)timer->user_data;
    if (!lab_time) {
        return;
    }
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    lv_label_set_text_fmt(lab_time, "%02u:%02u", (unsigned)timeinfo.tm_hour, (unsigned)timeinfo.tm_min);
}

static void alert_flash_restore_cb(lv_timer_t *timer)
{
    lv_obj_t *overlay = (lv_obj_t *)timer->user_data;
    if (overlay) {
        lv_obj_del(overlay);
    }
    lv_timer_del(timer);
}

/* Gas overlay dismiss: stop alarm audio, remove overlay, return to clock mode. */
static void gas_alert_restore_cb(lv_timer_t *timer)
{
    sr_handler_stop_gas_alarm();  /* stop gas_alarm.wav so it only plays while screen is up */
    lv_obj_t *overlay = (lv_obj_t *)timer->user_data;
    if (overlay) {
        lv_obj_del(overlay);
    }
    g_voice_mode = false;
    apply_clock_mode();
    lv_timer_del(timer);
}

/* Intruder overlay dismiss: remove overlay, return to clock mode. */
static void intruder_alert_restore_cb(lv_timer_t *timer)
{
    lv_obj_t *overlay = (lv_obj_t *)timer->user_data;
    if (overlay) {
        lv_obj_del(overlay);
    }
    g_voice_mode = false;
    apply_clock_mode();
    lv_timer_del(timer);
}

static void alert_flash_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    /* When async status/light is pending from another task (e.g. IR learn), switch to voice mode
     * so status and light indicator are visible (they are hidden in clock mode). */
    if ((g_pending_status_ready || g_pending_light >= 0) && !g_voice_mode) {
        g_voice_mode = true;
        apply_voice_mode();
    }
    /* Apply pending status/light from other tasks (e.g. IR learn callback) */
    if (g_pending_status_ready && g_status_label) {
        g_pending_status_ready = false;
        g_pending_status_buf[PENDING_STATUS_LEN - 1] = '\0';
        if (g_pending_status_ir_style) {
            g_pending_status_ir_style = false;
            lv_obj_set_style_text_font(g_status_label, &font_en_24, LV_PART_MAIN);
            lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_width(g_status_label, IR_STATUS_MAX_WIDTH);
            lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_WRAP);
        } else if (strcmp(g_pending_status_buf, "Say command") == 0) {
            lv_obj_set_style_text_font(g_status_label, &font_en_24, LV_PART_MAIN);
            lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_width(g_status_label, IR_STATUS_MAX_WIDTH);
            lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_WRAP);
        } else {
            lv_obj_set_style_text_font(g_status_label, &font_en_bold_36, LV_PART_MAIN);
            lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_width(g_status_label, IR_STATUS_MAX_WIDTH);
            lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_WRAP);
        }
        lv_label_set_text(g_status_label, g_pending_status_buf);
    }
    if (g_pending_light >= 0 && g_pending_light < KAVACH_LIGHT_MAX && g_light_indicator) {
        int s = g_pending_light;
        g_pending_light = -1;
        uint32_t c = light_colors[s];
        lv_obj_set_style_bg_color(g_light_indicator, lv_color_hex(c), LV_PART_MAIN);
        lv_obj_set_style_shadow_color(g_light_indicator, lv_color_hex(c), LV_PART_MAIN);
    }
    if (g_trigger_gas_alert) {
        g_trigger_gas_alert = false;
        lv_obj_t *scr = lv_scr_act();
        lv_obj_t *overlay = lv_obj_create(scr);
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(COLOR_ALERT_FLASH), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_foreground(overlay);

#define GAS_LABEL_W 220
        lv_obj_t *title = lv_label_create(overlay);
        lv_label_set_text_static(title, "GAS LEAK!");
        lv_obj_set_style_text_font(title, &font_en_bold_36, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_width(title, GAS_LABEL_W);
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

        lv_obj_t *msg = lv_label_create(overlay);
        lv_label_set_text_static(msg, "Clear the kitchen");
        lv_obj_set_style_text_font(msg, &font_en_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(msg, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_width(msg, GAS_LABEL_W);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, -4);

        lv_obj_t *sub = lv_label_create(overlay);
        lv_label_set_text_static(sub, "Ventilate now! Open windows.");
        lv_obj_set_style_text_font(sub, &font_en_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(sub, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_width(sub, GAS_LABEL_W);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 28);

        lv_timer_t *restore = lv_timer_create(gas_alert_restore_cb, 3000, overlay);
        lv_timer_set_repeat_count(restore, 1);
        return;
    }

    if (g_trigger_intruder_alert) {
        g_trigger_intruder_alert = false;
        lv_obj_t *scr = lv_scr_act();
        lv_obj_t *overlay = lv_obj_create(scr);
        lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(COLOR_ALERT_FLASH), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_foreground(overlay);

#define INTRUDER_LABEL_W 220
        lv_obj_t *title = lv_label_create(overlay);
        lv_label_set_text_static(title, "INTRUDER ALERT");
        lv_obj_set_style_text_font(title, &font_en_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_width(title, INTRUDER_LABEL_W);
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

        lv_obj_t *msg = lv_label_create(overlay);
        lv_label_set_text_static(msg, "Motion detected outside");
        lv_obj_set_style_text_font(msg, &font_en_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(msg, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_width(msg, INTRUDER_LABEL_W);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, -4);

        lv_timer_t *restore = lv_timer_create(intruder_alert_restore_cb, 3000, overlay);
        lv_timer_set_repeat_count(restore, 1);
        return;
    }

    if (!g_trigger_alert_flash) {
        return;
    }
    g_trigger_alert_flash = false;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *overlay = lv_obj_create(scr);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(COLOR_ALERT_FLASH), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(overlay);

    /* Title: "Emergency" */
    lv_obj_t *title = lv_label_create(overlay);
    lv_label_set_text_static(title, "Emergency");
    lv_obj_set_style_text_font(title, &font_en_bold_36, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(title, OVERLAY_LABEL_MAX_W);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

    /* Confirmation: "Message sent" */
    lv_obj_t *msg = lv_label_create(overlay);
    lv_label_set_text_static(msg, "Message sent");
    lv_obj_set_style_text_font(msg, &font_en_bold_36, LV_PART_MAIN);
    lv_obj_set_style_text_color(msg, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(msg, OVERLAY_LABEL_MAX_W);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, -10);

    /* Subtext: "Help has been notified" */
    lv_obj_t *sub = lv_label_create(overlay);
    lv_label_set_text_static(sub, "Help has been notified");
    lv_obj_set_style_text_font(sub, &font_en_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(sub, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(sub, OVERLAY_LABEL_MAX_W);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 32);

    lv_timer_t *restore = lv_timer_create(alert_flash_restore_cb, 1200, overlay);
    lv_timer_set_repeat_count(restore, 1);
}

void kavach_ui_trigger_alert_flash(void)
{
    g_trigger_alert_flash = true;
}

void kavach_ui_trigger_gas_leak_alert(void)
{
    g_trigger_gas_alert = true;
}

void kavach_ui_trigger_intruder_alert(void)
{
    g_trigger_intruder_alert = true;
}

void kavach_ui_set_status_async(const char *text)
{
    if (!text) {
        return;
    }
    strncpy(g_pending_status_buf, text, PENDING_STATUS_LEN - 1);
    g_pending_status_buf[PENDING_STATUS_LEN - 1] = '\0';
    g_pending_status_ir_style = false;
    g_pending_status_ready = true;
}

void kavach_ui_set_status_async_ir(const char *text)
{
    if (!text) {
        return;
    }
    strncpy(g_pending_status_buf, text, PENDING_STATUS_LEN - 1);
    g_pending_status_buf[PENDING_STATUS_LEN - 1] = '\0';
    g_pending_status_ir_style = true;
    g_pending_status_ready = true;
}

void kavach_ui_set_light_async(kavach_light_t state)
{
    if (state >= KAVACH_LIGHT_MAX) {
        state = KAVACH_LIGHT_IDLE;
    }
    g_pending_light = (int)state;
}

#define TEMP_HUM_BUF_SIZE 16
static void temp_hum_timer_cb(lv_timer_t *timer)
{
    float temp = 0, hum = 0;
    if (!g_temp_label || !g_hum_label) {
        return;
    }
    if (bsp_board_get_sensor_handle()->get_humiture(&temp, &hum) == ESP_OK) {
        char buf[TEMP_HUM_BUF_SIZE];
        snprintf(buf, sizeof(buf), "%.1f °C", (double)temp);
        lv_label_set_text(g_temp_label, buf);
        snprintf(buf, sizeof(buf), "%.0f%%", (double)hum);
        lv_label_set_text(g_hum_label, buf);
    } else {
        lv_label_set_text_static(g_temp_label, "-- °C");
        lv_label_set_text_static(g_hum_label, "--%");
    }
}

void kavach_ui_set_status(const char *text)
{
    if (!g_status_label || !text) {
        return;
    }
    lv_obj_set_width(g_status_label, IR_STATUS_MAX_WIDTH);
    lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_WRAP);
    if (strcmp(text, "Say command") == 0) {
        lv_obj_set_style_text_font(g_status_label, &font_en_24, LV_PART_MAIN);
        lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    } else {
        lv_obj_set_style_text_font(g_status_label, &font_en_bold_36, LV_PART_MAIN);
        lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    lv_label_set_text(g_status_label, text);
}

void kavach_ui_set_light(kavach_light_t state)
{
    if (state >= KAVACH_LIGHT_MAX) {
        state = KAVACH_LIGHT_IDLE;
    }
    if (g_light_indicator) {
        uint32_t c = light_colors[state];
        lv_obj_set_style_bg_color(g_light_indicator, lv_color_hex(c), LV_PART_MAIN);
        lv_obj_set_style_shadow_color(g_light_indicator, lv_color_hex(c), LV_PART_MAIN);
    }
}
