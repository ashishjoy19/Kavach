/*
 * Kavach minimal UI: one screen with title, status text, temp/humidity, and on-screen light.
 * No buttons, no menu. Signals: wake word, command recognised, alert sent.
 */
#include <stdio.h>
#include "ui_kavach.h"
#include "lvgl.h"
#include "esp_log.h"
#include "bsp_board.h"

LV_FONT_DECLARE(font_en_24);

static const char *TAG = "ui_kavach";

static lv_obj_t *g_status_label = NULL;
static lv_obj_t *g_light_indicator = NULL;
static lv_obj_t *g_title_label = NULL;
static lv_obj_t *g_temp_hum_label = NULL;
static lv_timer_t *g_temp_hum_timer = NULL;

static void temp_hum_timer_cb(lv_timer_t *timer);

static const uint32_t light_colors[KAVACH_LIGHT_MAX] = {
    [KAVACH_LIGHT_IDLE]       = 0x404040,  /* grey */
    [KAVACH_LIGHT_LISTENING]  = 0x00C853,  /* green */
    [KAVACH_LIGHT_COMMAND_OK] = 0x2196F3,  /* blue */
    [KAVACH_LIGHT_ALERT]      = 0xF44336,  /* red */
};

void kavach_ui_start(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), LV_PART_MAIN);

    /* Title */
    g_title_label = lv_label_create(scr);
    lv_label_set_text_static(g_title_label, "Kavach");
    lv_obj_set_style_text_font(g_title_label, &font_en_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(g_title_label, LV_ALIGN_TOP_MID, 0, 24);

    /* Temperature and humidity (top right) */
    g_temp_hum_label = lv_label_create(scr);
    lv_label_set_text_static(g_temp_hum_label, "-- C  --%");
    lv_obj_set_style_text_font(g_temp_hum_label, &font_en_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_temp_hum_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(g_temp_hum_label, LV_ALIGN_TOP_RIGHT, -16, 24);

    /* Status text */
    g_status_label = lv_label_create(scr);
    lv_label_set_text_static(g_status_label, "Ready");
    lv_obj_set_style_text_font(g_status_label, &font_en_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_status_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(g_status_label, LV_ALIGN_CENTER, 0, -20);

    /* On-screen "light" indicator (circle) */
    g_light_indicator = lv_obj_create(scr);
    lv_obj_set_size(g_light_indicator, 48, 48);
    lv_obj_set_style_radius(g_light_indicator, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_light_indicator, lv_color_hex(light_colors[KAVACH_LIGHT_IDLE]), LV_PART_MAIN);
    lv_obj_set_style_border_width(g_light_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(g_light_indicator, 15, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(g_light_indicator, lv_color_hex(light_colors[KAVACH_LIGHT_IDLE]), LV_PART_MAIN);
    lv_obj_align(g_light_indicator, LV_ALIGN_CENTER, 0, 48);

    kavach_ui_set_light(KAVACH_LIGHT_IDLE);

    /* Timer to refresh temperature and humidity every 3 s */
    g_temp_hum_timer = lv_timer_create(temp_hum_timer_cb, 3000, NULL);
    lv_timer_set_repeat_count(g_temp_hum_timer, -1);
    temp_hum_timer_cb(g_temp_hum_timer);  /* first update */

    ESP_LOGI(TAG, "Kavach minimal UI started");
}

#define TEMP_HUM_BUF_SIZE 24
static void temp_hum_timer_cb(lv_timer_t *timer)
{
    float temp = 0, hum = 0;
    if (!g_temp_hum_label) {
        return;
    }
    if (bsp_board_get_sensor_handle()->get_humiture(&temp, &hum) == ESP_OK) {
        char buf[TEMP_HUM_BUF_SIZE];
        snprintf(buf, sizeof(buf), "%.1f C  %.0f%%", (double)temp, (double)hum);
        lv_label_set_text(g_temp_hum_label, buf);
    } else {
        lv_label_set_text_static(g_temp_hum_label, "-- C  --%");
    }
}

void kavach_ui_set_status(const char *text)
{
    if (g_status_label && text) {
        lv_label_set_text(g_status_label, text);
    }
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
