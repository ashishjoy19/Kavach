/*
 * Stub for mute/voice flag when no factory UI (minimal Kavach build).
 * SR always listens when this returns true.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Always true so SR always feeds audio (no mute overlay). */
bool get_mute_play_flag(void);

#ifdef __cplusplus
}
#endif
