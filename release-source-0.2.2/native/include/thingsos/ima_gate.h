// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
/* Opening the ima phone-pairing page only serves a static HTML file from
   flash. An audio session claims the network solely to exclude uploads/OTA/USB
   while its file is open, so it must not block pairing: the page would show a
   "finish the current task" notice that no user action can clear while music
   plays. A real transfer or an in-flight upload still owns SD/IO and does
   block it, and the station must be associated to have an address at all. */
static inline bool thingsos_ima_pair_allowed(bool connected,bool uploading,bool wifi_busy) {
    return connected && !uploading && !wifi_busy;
}
