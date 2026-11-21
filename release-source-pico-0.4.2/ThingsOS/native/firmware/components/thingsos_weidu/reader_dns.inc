/* SPDX-License-Identifier: Apache-2.0
 * lwIP name resolution can block for many seconds even with async TLS.
 * Resolve before creating the TLS client, outside the display/SD mutex.
 * A successful lookup populates the lwIP cache used by the following request.
 * Entry and exit own thingsos_io_lock; no request is sent here.
 */
static thingsos_weidu_error_t reader_dns_ready(const char* host) {
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo* address = NULL;
    uint64_t started = now_ms(NULL);
    xSemaphoreGive(thingsos_io_lock);
    int result = getaddrinfo(host, NULL, &hints, &address);
    bool resolved = result == 0 && address != NULL;
    if (address) freeaddrinfo(address);
    xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    ESP_LOGI("ThingsOS_weidu", "Reader DNS elapsed=%u ms resolved=%d",
        (unsigned)(now_ms(NULL) - started), resolved);
    if (abort_requested) return WD_CANCEL;
    return resolved ? WD_OK : WD_DNS;
}
