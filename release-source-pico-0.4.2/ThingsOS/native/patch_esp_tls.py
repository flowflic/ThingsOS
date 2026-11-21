"""Preserve ESP-IDF 6.1's pending socket across asynchronous select timeouts."""
from pathlib import Path
import sys


def patched_source(source: Path) -> bytes:
    original = source.read_bytes()
    newline = b'\r\n' if b'\r\n' in original else b'\n'
    original = original.replace(b'\r\n', b'\n')
    old = b'''            if (select(tls->sockfd + 1, &tls->rset, &tls->wset, NULL,
                       cfg->timeout_ms>0 ? &tv : NULL) == 0) {
                ESP_LOGD(TAG, "select() timed out");
                return 0;
            }
            if (FD_ISSET(tls->sockfd, &tls->rset) || FD_ISSET(tls->sockfd, &tls->wset)) {'''
    new = b'''            /* select overwrites its fd sets, including on timeout. Keep the
               socket watched when the next non-blocking step retries. */
            fd_set read_ready = tls->rset;
            fd_set write_ready = tls->wset;
            if (select(tls->sockfd + 1, &read_ready, &write_ready, NULL,
                       cfg->timeout_ms>0 ? &tv : NULL) == 0) {
                ESP_LOGD(TAG, "select() timed out");
                return 0;
            }
            if (FD_ISSET(tls->sockfd, &read_ready) || FD_ISSET(tls->sockfd, &write_ready)) {'''
    if original.count(old) != 1:
        raise ValueError(f'Recheck the ESP-TLS async select fix against {source}')
    return original.replace(old, new).replace(b'\n', newline)


if __name__ == '__main__':
    source, output = map(Path, sys.argv[1:])
    content = patched_source(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_bytes() != content:
        output.write_bytes(content)
