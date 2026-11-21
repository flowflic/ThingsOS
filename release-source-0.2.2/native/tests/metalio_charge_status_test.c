// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef enum {
    METALIO_CHARGE_UNKNOWN, METALIO_CHARGE_NONE,
    METALIO_CHARGE_CHARGING, METALIO_CHARGE_FULL
} metalio_charge_t;
#include "../metalio/components/metalio_bsp/charge_status.inc"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
int main(void) {
    /* DPDM disabled: real CC/CV/top-off must survive VBUS_STAT=0, at every
       displayed SOC, including 100% while the charger is still topping off. */
    for(unsigned stat=1;stat<=3;++stat)for(int level=-1;level<=100;++level)
        CHECK(charge_status_decode((uint8_t)(stat<<3),level)==METALIO_CHARGE_CHARGING);
    /* Also cover each adapter classification and ignore unrelated status bits. */
    for(unsigned high=0;high<8;++high)for(unsigned vbus=0;vbus<8;++vbus) {
        uint8_t raw=(uint8_t)((high<<5)|vbus);
        CHECK(charge_status_decode(raw,50)==METALIO_CHARGE_NONE);
        CHECK(charge_status_decode(raw,99)==(vbus>0 && vbus<7?METALIO_CHARGE_FULL:METALIO_CHARGE_NONE));
        for(unsigned stat=1;stat<=3;++stat)
            CHECK(charge_status_decode((uint8_t)(raw|(stat<<3)),50)==
                  (vbus==7?METALIO_CHARGE_NONE:METALIO_CHARGE_CHARGING));
    }
    puts("Metalio charge status: DPDM-off charging, idle/full, OTG passed");
    return 0;
}
