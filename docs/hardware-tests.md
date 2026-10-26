# Hardware qualification, 2026-10-26

Scope: XIAO ESP32-S3, Wio-SX1262 B2B, A7670E-LASA with firmware A131B08A7670M6C, UART 115200 8N1 and external 5 V modem supply. S is GPIO2; supply enable and PWRKEY are not wired.

| Check | Observed result |
| --- | --- |
| Native tests | 458 state/AT checks, 70 install transaction checks and 27 binary HTTP reader checks passed |
| Server tests | 15 test groups, including signing and altered-payload rejection, passed |
| Real signed LTE OTA | 1,180,560-byte app transferred and installed; approximately 244 seconds for download and pre-reboot processing |
| Transfer accounting | Exactly one image's payload served in 37 ranges; no range retransmission in the successful run |
| Transient modem read | Retried locally before accepting a payload header; no additional LTE GET |
| Validation | Ed25519 manifest, target/capability, stream SHA-256, ESP image validation and raw flash readback passed |
| First boot | Inactive app1 booted pending, then became VALID after modem AT and sustained MeshCore loop health |
| Installed report | Automatically acknowledged by the server after successful boot |
| Rollback | Deliberately unconfirmed app0 boot reset and returned to valid app1 after approximately 64 seconds, without LTE download |
| Identity/settings | Public identity and node name unchanged across update and rollback |
| Invalid signature | Rejected on the real device before any firmware request |
| Repeated release | Already installed counter rejected before firmware download |
| Blocked station | Rejected with HTTP_BLOCKED; no app activation |
| Radio administration | Operator confirmed both status commands during LTE download and OTA status after reboot/rollback |
| Final modem state | CSCLK confirmed, S LOW, sleep permitted; zero UART faults recorded |

Two earlier failed paths were retained as qualification evidence: a transfer stopped at its retry budget while preserving the active app, and explicit-offset reads exposed relative-position behavior on this modem firmware. A USB readback confirmed that all 720,896 bytes written before the first abort matched the signed server artifact. The production reader uses sequential reads and final whole-image verification.

This qualifies the tested externally powered setup. Battery measurement, boost enable control, true supply disconnection, PWRKEY, sleep current, environmental endurance and physical brownout/power-cut testing are outside this result. Boot health confirmation proves local startup and modem AT readiness; it does not continuously certify future LTE or radio reachability.

Detailed logs, device identities, operator signing keys, firmware binaries and deployment configuration are intentionally not included in this repository.
