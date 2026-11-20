// SPDX-License-Identifier: Apache-2.0
#include "thingsos/touch_privacy.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Touch privacy line %d\n",__LINE__);return 1;}}while(0)
int main(void) {
    thingsos_touch_privacy_t state={0};
    CHECK(thingsos_touch_log_allowed(&state,false,true));
    CHECK(!thingsos_touch_log_allowed(&state,true,true));
    CHECK(!thingsos_touch_log_allowed(&state,false,true));
    CHECK(!thingsos_touch_log_allowed(&state,false,false));
    CHECK(thingsos_touch_log_allowed(&state,false,true));
    CHECK(thingsos_touch_log_allowed(&state,false,false));
    CHECK(!thingsos_touch_log_allowed(&state,true,true));
    CHECK(!thingsos_touch_log_allowed(&state,true,false));
    CHECK(thingsos_touch_log_allowed(&state,false,false));
    puts("PASS: private PIN contacts do not leak coordinates after page changes");return 0;
}
