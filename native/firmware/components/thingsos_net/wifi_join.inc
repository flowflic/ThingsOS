// SPDX-License-Identifier: Apache-2.0
#define WIFI_TRY_INTERVAL_US 2000000LL
#define WIFI_CONNECT_WINDOW_US 30000000LL
#define WIFI_TRY_LIMIT 4

typedef struct {
    int64_t deadline, retry_at;
    unsigned attempts, reason;
    bool retry, associated, has_ip;
} wifi_join_t;

static void wifi_join_begin(wifi_join_t* join, int64_t now) {
    *join = (wifi_join_t){.deadline = now + WIFI_CONNECT_WINDOW_US, .retry = true};
}

static void wifi_join_disconnected(wifi_join_t* join, int64_t now, unsigned reason) {
    if (join->has_ip) wifi_join_begin(join, now);
    if (join->deadline) {
        join->associated = false;
        join->reason = reason;
        join->retry = true;
    }
}

enum wifi_join_action { WIFI_JOIN_WAIT, WIFI_JOIN_TRY, WIFI_JOIN_END };

static enum wifi_join_action wifi_join_step(wifi_join_t* join, int64_t now, bool scanning) {
    if (!join->deadline) return WIFI_JOIN_WAIT;
    if (now >= join->deadline || (join->retry && join->attempts >= WIFI_TRY_LIMIT)) {
        join->deadline = 0;
        return WIFI_JOIN_END;
    }
    if (scanning || !join->retry || now < join->retry_at) return WIFI_JOIN_WAIT;
    join->retry = false;
    join->retry_at = now + WIFI_TRY_INTERVAL_US;
    ++join->attempts;
    return WIFI_JOIN_TRY;
}

static bool wifi_join_got_ip(wifi_join_t* join) {
    if (!join->associated || (!join->deadline && !join->has_ip)) return false;
    *join = (wifi_join_t){.associated = true, .has_ip = true};
    return true;
}
