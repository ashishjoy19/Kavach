/*
 * Kavach: RainMaker stub (not used; MQTT will be used later for alerts/calls).
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_rmaker_start(void);
bool app_rmaker_is_connected(void);

#ifdef __cplusplus
}
#endif
