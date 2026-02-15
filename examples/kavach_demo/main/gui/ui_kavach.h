/*
 * Kavach minimal UI: text + on-screen light only (no buttons, no factory UI).
 * For wake word detected, command recognised, and alert sent.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KAVACH_LIGHT_IDLE = 0,
    KAVACH_LIGHT_LISTENING,
    KAVACH_LIGHT_COMMAND_OK,
    KAVACH_LIGHT_ALERT,
    KAVACH_LIGHT_MAX
} kavach_light_t;

/** Start the minimal Kavach screen (title + status text + light). Shows splash overlay until kavach_ui_splash_finish(). Call once after display init. */
void kavach_ui_start(void);

/** Hide splash and show main UI. Call when initialisation (WiFi, MQTT, SR, etc.) is done. */
void kavach_ui_splash_finish(void);

/** Set the status line (e.g. "Listening...", command text, "Alert sent"). */
void kavach_ui_set_status(const char *text);

/** Set the on-screen indicator: IDLE / LISTENING / COMMAND_OK / ALERT. */
void kavach_ui_set_light(kavach_light_t state);

/** true = voice command mode (time top-right, status center); false = desktop clock mode (big clock center). */
void kavach_ui_set_voice_mode(bool voice_mode);

/** Trigger a full-screen red flash (e.g. when emergency button is pressed). Safe to call from any task. */
void kavach_ui_trigger_alert_flash(void);

/** Trigger full-screen gas leak alert (e.g. when MQTT gas topic reports LEAK). Safe to call from any task. */
void kavach_ui_trigger_gas_leak_alert(void);

/** Trigger full-screen intruder/motion alert (e.g. when MQTT intruder topic reports motion). Safe to call from any task. */
void kavach_ui_trigger_intruder_alert(void);

/** Set status from any task. Applied on LVGL task. Uses normal voice-mode font/size. */
void kavach_ui_set_status_async(const char *text);
/** Set status for IR learn messages: smaller font + wrap so long text fits. Does not affect voice mode. */
void kavach_ui_set_status_async_ir(const char *text);
/** Set light from any task. Applied on LVGL task. */
void kavach_ui_set_light_async(kavach_light_t state);

#ifdef __cplusplus
}
#endif
