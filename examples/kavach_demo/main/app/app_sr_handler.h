/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool sr_echo_is_playing(void);

/** Play gas alarm WAV (e.g. /spiffs/gas_alarm.wav). Call when gas leak is detected. */
void sr_handler_play_gas_alarm(void);
/** Stop gas alarm playback when the alert overlay is dismissed. */
void sr_handler_stop_gas_alarm(void);

void sr_handler_task(void *pvParam);

#ifdef __cplusplus
}
#endif
