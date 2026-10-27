# Transfer performance and data usage

Measured on the existing XIAO ESP32-S3, A7670E-LASA A131B08A7670M6C and voltage adaptation, with external modem power and no RTS/CTS. A temporary USB/UART measurement application separated modem timing from the repeater application. The qualified repeater image was restored afterwards. These are small-transfer measurements; higher-speed full-image OTA qualification is still pending.

## UART and real HTTPS

Three local 7,287-byte command-list transfers at each rate produced identical SHA-256 values and zero recorded UART faults. Local transfers consume no LTE data. Measured local throughput was approximately 11, 23, 44 and 78 kB/s respectively.

The real HTTPS test used two 32-KiB transfers per baud rate, 1-KiB sequential reads, a fresh HTTP context and an independent expected-data SHA-256 check. All eight payload checks passed, with zero recorded UART faults.

| Baud | Mean whole request | Mean UART-read phase | Mean HTTP-action phase |
| --- | --- | --- | --- |
| 115200 | 7.664 s | 3.304 s | 4.156 s |
| 230400 | 4.758 s | 1.805 s | 2.758 s |
| 460800 | 3.961 s | 1.008 s | 2.774 s |
| 921600 | 3.649 s | 0.711 s | 2.758 s |

460800 is the useful candidate: the UART phase was over three times shorter, while doubling it again brought little whole-request improvement. The observed whole-request ratio of about 1.9 is not a guaranteed full-update speedup: the network phase also varied between measurements. One bounded local HTTPREAD retry occurred at 921600; there were no additional LTE requests for that retry.

TLS certificate/date/name checks remained enabled. Tests did not write firmware through OTA or change the device trust key.

## Reset safety prevents immediate production activation

The modem confirmed temporary IPR=460800 while permanent IPREX remained 115200. After a software restart, the ESP bridge initialized at 115200, but the modem still answered at 460800. Recovery required detecting the active rate and returning it to 115200.

The existing fallback application's UART initialization is fixed at 115200. Therefore a crash during a faster session could leave that application unable to communicate with the externally powered modem. Before enabling a faster production session, baud detection/recovery must be available in every image that can run after rollback. A safe deployment needs a recovery-capable baseline first, followed by the faster image, with reset/rollback tests. Merely changing the baud build flag is insufficient.

The [SIMCom A76XX AT manual, sections 10.2.3 and 10.2.4](https://files.waveshare.com/wiki/A7670E-Cat-1-GNSS-HAT/A76XX_Series_AT_Command_Manual_V1.09.pdf) distinguishes temporary IPR from permanent IPREX. IPREX was only queried during these tests. The final confirmed temporary and permanent rates were both 115200.

## What can reduce transferred bytes?

VPS-side packet-header accounting found approximately 42–43 kB of bidirectional IP traffic for a 32-KiB file range, with one new TCP connection per request. This is about 28–33% overhead over the file payload in this measurement. It includes TCP/TLS/IP traffic, but excludes DNS and other carrier traffic, so it is not a SIM billing-counter measurement. Per-case timing windows had a 120-ms margin.

| Approach | Evidence | Decision |
| --- | --- | --- |
| Higher UART rate | Less UART time; similar network bytes | Speed candidate after reset recovery is qualified |
| Reuse native HTTP context | Second request retained the old Range header; SHA-256 failed; a new TCP connection was still opened | Keep fresh contexts |
| 64-KiB ranges | Server delivered the sample, but the carrier restricted the SIM before the test completed | Unqualified; no conclusion about block-size stability |
| Real persistent TLS socket | Modem command list includes CCH SSL socket operations; not exercised end-to-end | Separate transport work and security qualification required |
| gzip | Offline image shrank from 1,183,568 to 788,800 bytes; decompression reproduced the exact original SHA-256 | About 33.35% file savings possible; no device decoder deployed |

The owner confirmed that the configured SIM volume limit had been reached. Online tests stopped immediately. The entire series served 360,448 bytes (352 KiB) of fixture payload, within the 512-KiB limit. No full firmware file was downloaded. The temporary fixture, temporary station and packet capture were removed; production station policy and installed version were preserved.

Safe compression would require authenticated transport metadata, bounded streaming decompression into the inactive partition, original-image size/hash checks, bounded retries and reset/rollback qualification. Serving gzip transparently to the current raw-image client would fail its length/hash checks. Certificate or signature checks must remain enabled throughout any optimization.

The production profile remains at 115200 baud with 32-KiB ranges and fresh HTTP contexts. Remaining work is staged baud recovery, then a small production-stack transfer test; data-saving transport changes require further online tests once the SIM is available.
