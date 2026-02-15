/*
 * Universal IR remote: learn and send AC on/off (and extensible for more keys).
 * Learn via long-press home; send via voice "Turn on/off the Air" or API.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Callback when IR learning finishes: ok = true if both AC on and AC off were learned and saved. */
typedef void (*app_ir_learn_done_cb_t)(bool ok, void *user_data);

/** Initialize IR TX task. Call once after bsp_board_init(). */
void app_ir_init(void);

/** Return true if IR learn mode is active (e.g. so SR can skip adding AC commands). */
bool app_ir_learn_is_active(void);

/** Start IR learning for AC on + AC off. User points remote and presses buttons when prompted. cb called when done. */
void app_ir_learn_start(app_ir_learn_done_cb_t cb, void *user_data);

/** Stop IR learning. */
void app_ir_learn_stop(void);

/** Return true if we have learned AC on and AC off codes. */
bool app_ir_has_ac_codes(void);

/** Send learned AC-on IR burst. No-op if not learned. */
void app_ir_send_ac_on(void);

/** Send learned AC-off IR burst. No-op if not learned. */
void app_ir_send_ac_off(void);

#ifdef __cplusplus
}
#endif
