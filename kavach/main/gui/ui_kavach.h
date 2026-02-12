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

/** Start the minimal Kavach screen (title + status text + light). Call once after display init. */
void kavach_ui_start(void);

/** Set the status line (e.g. "Listening...", command text, "Alert sent"). */
void kavach_ui_set_status(const char *text);

/** Set the on-screen indicator: IDLE / LISTENING / COMMAND_OK / ALERT. */
void kavach_ui_set_light(kavach_light_t state);

/** true = voice command mode (time top-right, status center); false = desktop clock mode (big clock center). */
void kavach_ui_set_voice_mode(bool voice_mode);

/** Trigger a full-screen red flash (e.g. when emergency button is pressed). Safe to call from any task. */
void kavach_ui_trigger_alert_flash(void);

#ifdef __cplusplus
}
#endif
