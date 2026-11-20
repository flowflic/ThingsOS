// SPDX-License-Identifier: MIT
/* Read-only UI interpretation. DPDM is disabled by the existing charger policy,
   so VBUS_STAT=0 does not disprove active CC/CV/top-off charging. See the
   upstream docs/battery-soc-and-charge.md sections 4.1 and 5. */
static metalio_charge_t charge_status_decode(uint8_t raw, int percent) {
    unsigned stat = (raw >> 3) & 3, vbus = raw & 7;
    if (vbus == 7) return METALIO_CHARGE_NONE; /* OTG output, not an adapter. */
    if (stat != 0) return METALIO_CHARGE_CHARGING;
    if (vbus == 0) return METALIO_CHARGE_NONE;
    return percent >= 99 ? METALIO_CHARGE_FULL : METALIO_CHARGE_NONE;
}
