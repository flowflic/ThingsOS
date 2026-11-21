/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "thingsos/system_monitor.h"
#ifndef THINGSOS_SYSTEM_API
#define THINGSOS_SYSTEM_API
#endif
THINGSOS_SYSTEM_API void thingsos_system_reset(void);
THINGSOS_SYSTEM_API void thingsos_system_read(thingsos_system_snapshot_t*);
