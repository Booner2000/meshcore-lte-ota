# Temporary UART speed and recovery

The baseline is fixed at 115200 baud. Firmware transfers may use 460800 baud;
`LTE_MODEM_TRANSFER_BAUD=115200` disables acceleration. gzip is not used.

Every boot wakes the separately powered modem and probes the known rates
115200, 230400, 460800 and 921600 within a bounded wake deadline. Wrong-rate
framing errors are cleared between probes. A discovered non-baseline rate is
returned to 115200 with temporary `AT+IPR`, a 500-ms settling delay on each side
of host UART reconfiguration, an AT readiness check and an exact IPR query.
Persistent `AT+IPREX` is never written.

Only the worker changes UART settings. The radio CLI stays nonblocking and
`lte modem io` reports the actual current host rate. S remains HIGH throughout
communication. Cancellation during a switch still invokes bounded recovery;
failure to restore the baseline blocks OTA activation and leaves S awake.
The previous sleep permission is restored only after successful cleanup.

Acceleration additionally requires the other OTA partition to be VALID and
pass SDK image verification, including its image hash, and contain
`MESHCORE_LTE_UART_RECOVERY:v1` inside the verified image length. This check runs
on the worker before the inactive partition is overwritten. The current valid
application can recover if a reset interrupts writing that partition. After
successful activation, the previous valid image can recover on rollback.

Each newly installed image must also contain this marker, in addition to the
existing LTE capability, valid signed manifest and exact raw-image SHA-256.
The checks are cumulative. The marker is a compatibility assertion by the
trusted firmware publisher; it is not a substitute for code review or signing.

Migration therefore requires two steps: install a recovery-capable baseline,
then install a recovery-capable successor. Transfers remain at 115200 while
the other slot is old, invalid or not VALID. An intentional rollback probe
aborts its test slot, so acceleration can remain unavailable until a subsequent
successful OTA restores a valid fallback. Never enable faster transfer merely
by setting the baseline UART build flag.

Host regression tests cover discovery at all four rates, absent communication,
switching back, cancellation immediately after IPR, and blocking activation when
baseline restoration fails. Hardware qualification is recorded separately.

## Hardware qualification, 2026-10-27

Version `1.18.0-lte-ota.6` passed on the externally powered XIAO/A7670E setup:

| Check | Result |
| --- | --- |
| ESP restart with modem left at 460800 | Automatic return to 115200, identity preserved, S LOW |
| SDK rollback with health deliberately withheld | Pending app returned to previous VALID app in 64.53 s |
| Unqualified fallback | Fast mode withheld before the baseline OTA |
| Signed baseline OTA, 1,185,200 bytes | 245.58 s, independent flash verification and confirmed boot |
| Production-stack 64-KiB fast download | SHA-256 correct; 460800 and S HIGH observed; baseline restored |
| ESP reset during a fast download job | Valid app recovered automatically; 115200 and S LOW restored |
| Complete signed fast OTA, identical image | 164.88 s; 460800 and S HIGH observed; independent flash verification and confirmed boot |
| Final state | app1 VALID, pending=0, release floor=6, 115200, S LOW, installed version reported |

The full-job timing runs from command submission to successful activation;
subsequent reboot health confirmation and the installed report are checked
separately. The fast run took approximately 33% less time, or 1.49 times the
throughput, for this matched image. Network timing varies; this is one pair of
complete runs, not a guaranteed future speedup. Both used fresh HTTP contexts,
32-KiB ranges and 1024-byte reads, with no repeated file ranges.

The device's cumulative UART diagnostic count increased by two during the
deliberate wrong-rate detection following reset. Those framing faults were
cleared between probes; the fast full transfer added none. Its final reboot
reported zero UART faults. Wrong-rate probing is distinct from accepting
corrupt transfer data: a transfer fault still fails the framed read and remains
subject to signed whole-image and flash readback validation.

The owner confirmed the radio CLI responded during the baseline download.
All 721 portable checks and 16 server test groups passed. The new server upload
check rejects firmware missing either required marker. Temporary test data was
removed and the station was blocked again after successful installed reporting.
New qualification traffic served 2,468,704 bytes of file payload; this excludes
TLS, TCP/IP, manifests and reports and is not the carrier's billing counter.
Sleep is permitted at S LOW; physical sleep current and battery operation remain
outside these tests. UART acceleration does not reduce the firmware byte count.
