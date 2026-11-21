# WeRead trust root

`weread_root.pem` is the complete DigiCert Global Root G2 certificate extracted
from ESP-IDF's `components/mbedtls/esp_crt_bundle/cacrt_all.pem` trusted CA store.
It is a public trust-anchor certificate, not the server leaf or intermediate.

DER SHA-256 fingerprint:
`CB:3C:CB:B7:60:31:E5:E0:13:8F:8D:D3:9A:23:F9:DE:47:FF:C3:5E:43:C1:14:4C:EA:27:D4:6A:5A:B1:CB:5F`.

The micro-reading client restricts connections to `weread.qq.com` and uses
ESP-TLS's standard PEM CA chain validation, including hostname and certificate
validity checks. If the provider changes to a different CA, update the trust
anchor from a trusted CA store and revalidate the live chain.
