/*
 * Stub when sensor monitor / IR learn UI is not used (minimal Kavach build).
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Always false when sensor UI is not built. */
bool sensor_ir_learn_enable(void);

#ifdef __cplusplus
}
#endif
