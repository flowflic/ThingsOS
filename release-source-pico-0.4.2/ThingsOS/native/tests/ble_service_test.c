/* SPDX-License-Identifier: Apache-2.0 */
#define THINGSOS_BLE_HOST_TEST
#define THINGSOS_BLE_IDLE_RECLAIM_MS 15000u
#include "../firmware/components/thingsos_ble/ble_scan.c"
#include <assert.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "BLE service line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

void ble_store_config_init(void) {}

static void pump_init_and_sync(void) {
    CHECK(host_pending_init_fn != NULL);
    TaskFunction_t fn = host_pending_init_fn;
    void* arg = host_pending_init_arg;
    host_pending_init_fn = NULL;
    host_pending_init_arg = NULL;
    fn(arg);
    if (ble_hs_cfg.sync_cb) ble_hs_cfg.sync_cb();
}

static void pump_stop(void) {
    CHECK(host_pending_stop_fn != NULL);
    TaskFunction_t fn = host_pending_stop_fn;
    void* arg = host_pending_stop_arg;
    host_pending_stop_fn = NULL;
    host_pending_stop_arg = NULL;
    fn(arg);
}

static void advance_ms(uint64_t ms) {
    uint64_t target = host_clock_us + ms * 1000u;
    while (host_clock_us < target) {
        if (host_pending_stop_fn) {
            pump_stop();
            continue;
        }
        if (host_active_timer && host_active_timer->armed &&
            host_active_timer->fire_at_ms * 1000u <= target) {
            uint64_t next = host_active_timer->fire_at_ms * 1000u;
            if (next > host_clock_us) host_clock_us = next;
            host_active_timer->armed = false;
            host_active_timer->fn(&host_active_timer->ev);
            continue;
        }
        host_clock_us = target;
    }
    if (host_pending_stop_fn) pump_stop();
}

static thingsos_ble_device_t sample_device(const char* addr, const char* name) {
    thingsos_ble_device_t d = {0};
    strcpy(d.device.address, addr);
    strcpy(d.device.name, name);
    d.address_type = 1;
    d.connectable = true;
    d.complete_name = true;
    return d;
}

static int hook_mode;
static void overlap_hook(void) {
    thingsos_ble_device_t dev = sample_device("112233445566", "PageTurner");
    if (hook_mode == 1) {
        CHECK(thingsos_ble_scan(true));
        CHECK(thingsos_ble_connect(&dev));
        CHECK(host_pending_init_fn == NULL);
    } else if (hook_mode == 2) {
        CHECK(thingsos_ble_scan(true));
        CHECK(thingsos_ble_stop());
    }
}

int main(void) {
    thingsos_ble_status_t st = {0};

    /* 1. Scan completes unconnected -> idle grace expires -> controller reclaimed; state kept. */
    CHECK(thingsos_ble_scan(true));
    pump_init_and_sync();
    CHECK(host_controller_active && host_init_calls == 1 && host_scan_calls == 1);
    {
        struct ble_gap_event ev = { .type = BLE_GAP_EVENT_DISC };
        uint8_t adv[] = { 2, 1, 6, 6, 9, 'P', 'i', 'c', 'o', 'K' };
        ev.disc.event_type = BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
        ev.disc.addr.type = 1;
        ev.disc.addr.val[5] = 0xAA; ev.disc.addr.val[4] = 0xBB; ev.disc.addr.val[3] = 0xCC;
        ev.disc.addr.val[2] = 0xDD; ev.disc.addr.val[1] = 0xEE; ev.disc.addr.val[0] = 0xFF;
        ev.disc.rssi = -55;
        ev.disc.data = adv;
        ev.disc.length_data = sizeof(adv);
        host_gap_cb(&ev, host_scan_arg);
        ev.type = BLE_GAP_EVENT_DISC_COMPLETE;
        host_gap_cb(&ev, host_scan_arg);
    }
    advance_ms(14000);
    CHECK(host_controller_active && !thingsos_ble_quiet());
    advance_ms(2000);
    CHECK(!host_controller_active && host_deinit_calls == 1 && thingsos_ble_quiet());
    thingsos_ble_status(&st);
    CHECK(st.count == 1 && !strcmp(st.devices[0].device.name, "PicoK"));
    CHECK(thingsos_ble_scan(false));
    CHECK(!host_controller_active && host_pending_init_fn == NULL);

    /* 2. Active HID connection never triggers idle reclaim. */
    thingsos_ble_device_t dev = sample_device("AABBCCDDEEFF", "PicoK");
    CHECK(thingsos_ble_connect(&dev));
    pump_init_and_sync();
    CHECK(host_controller_active && host_init_calls == 2 && host_connect_calls == 1);
    {
        struct ble_gap_event ev = { .type = BLE_GAP_EVENT_CONNECT };
        ev.connect.status = 0;
        ev.connect.conn_handle = 1;
        host_gap_cb(&ev, host_gap_arg);
        ev.type = BLE_GAP_EVENT_ENC_CHANGE;
        ev.enc_change.status = 0;
        ev.enc_change.conn_handle = 1;
        host_gap_cb(&ev, host_gap_arg);
    }
    CHECK(thingsos_ble_connected());
    advance_ms(120000);
    CHECK(host_controller_active && thingsos_ble_connected() && host_deinit_calls == 1);

    /* 3. Unexpected disconnect runs all 3 auto-reconnect retries, then reclaims after idle grace. */
    host_connect_result = -1;
    {
        struct ble_gap_event ev = { .type = BLE_GAP_EVENT_DISCONNECT };
        ev.disconnect.conn.conn_handle = 1;
        host_gap_cb(&ev, host_gap_arg);
    }
    unsigned connects_before = host_connect_calls;
    advance_ms(18000); /* 2s + 5s + 10s retries finish */
    CHECK(host_connect_calls == connects_before + 3);
    CHECK(host_controller_active);
    advance_ms(16000); /* 15s idle grace expires */
    CHECK(!host_controller_active && host_deinit_calls == 2 && thingsos_ble_quiet());
    host_connect_result = 0;

    /* 4. Manual disconnect reclaims after idle grace; disconnect while quiet is a no-op. */
    CHECK(thingsos_ble_connect(&dev));
    pump_init_and_sync();
    {
        struct ble_gap_event ev = { .type = BLE_GAP_EVENT_CONNECT };
        ev.connect.status = 0;
        ev.connect.conn_handle = 2;
        host_gap_cb(&ev, host_gap_arg);
    }
    CHECK(thingsos_ble_disconnect());
    advance_ms(200);
    {
        struct ble_gap_event ev = { .type = BLE_GAP_EVENT_DISCONNECT };
        ev.disconnect.conn.conn_handle = 2;
        host_gap_cb(&ev, host_gap_arg);
    }
    advance_ms(16000);
    CHECK(!host_controller_active && host_deinit_calls == 3 && thingsos_ble_quiet());
    CHECK(thingsos_ble_disconnect());
    CHECK(!host_controller_active && host_pending_init_fn == NULL);

    /* 5. Stop/restart overlap: command submitted mid-deinit automatically restarts controller. */
    CHECK(thingsos_ble_scan(true));
    pump_init_and_sync();
    {
        struct ble_gap_event ev = { .type = BLE_GAP_EVENT_DISC_COMPLETE };
        host_gap_cb(&ev, host_scan_arg);
    }
    hook_mode = 1;
    host_deinit_hook = overlap_hook;
    advance_ms(16000);
    host_deinit_hook = NULL;
    CHECK(host_deinit_calls == 4 && host_pending_init_fn != NULL);
    unsigned connects_now = host_connect_calls;
    pump_init_and_sync();
    CHECK(host_controller_active && host_connect_calls == connects_now + 1);

    /* 6. Explicit stop during idle reclaim cancels queued restart and keeps controller stopped. */
    {
        struct ble_gap_event ev = { .type = BLE_GAP_EVENT_CONNECT };
        ev.connect.status = 1;
        host_gap_cb(&ev, host_gap_arg);
    }
    hook_mode = 2;
    host_deinit_hook = overlap_hook;
    advance_ms(16000);
    host_deinit_hook = NULL;
    CHECK(host_deinit_calls == 5 && !host_controller_active && host_pending_init_fn == NULL && thingsos_ble_quiet());

    /* 7. Display pause cancels background auto-reconnect during transition, then resumes cleanly;
       active HID connections stay connected across display pause. */
    CHECK(thingsos_ble_reconnect());
    pump_init_and_sync();
    thingsos_ble_status(&st);
    CHECK(st.phase == THINGSOS_BLE_CONNECTING);
    unsigned cancels_before = host_conn_cancel_calls;
    unsigned connects_now2 = host_connect_calls;
    thingsos_ble_display_pause(true);
    advance_ms(1);
    CHECK(host_conn_cancel_calls == cancels_before + 1);
    thingsos_ble_status(&st);
    CHECK(st.phase == THINGSOS_BLE_IDLE);
    advance_ms(600);
    CHECK(host_connect_calls == connects_now2);
    thingsos_ble_display_pause(false);
    advance_ms(250);
    CHECK(host_connect_calls == connects_now2 + 1);
    {
        struct ble_gap_event ev = { .type = BLE_GAP_EVENT_CONNECT };
        ev.connect.status = 0;
        ev.connect.conn_handle = 3;
        host_gap_cb(&ev, host_gap_arg);
        ev.type = BLE_GAP_EVENT_ENC_CHANGE;
        ev.enc_change.status = 0;
        ev.enc_change.conn_handle = 3;
        host_gap_cb(&ev, host_gap_arg);
    }
    CHECK(thingsos_ble_connected());
    unsigned terms_before = host_terminate_calls;
    thingsos_ble_display_pause(true);
    advance_ms(10);
    thingsos_ble_display_pause(false);
    CHECK(thingsos_ble_connected() && host_terminate_calls == terms_before);

    puts("PASS: BLE idle reclaim, reconnect retries, quiet disconnect, and stop/restart overlap");
    return 0;
}
