# WLAN preferred, LTE fallback

The radio admin command `start update` tries WLAN first. `start wifi update`
requires WLAN; `start lte update` explicitly selects LTE. The equivalent download
tests are `start download`, `start wifi download` and `start lte download`.
`update status` and `update cancel` are aliases of the existing LTE status and
cancel commands. Status includes `via=wifi` or `via=lte`. `wifi update status`
reports whether a WLAN cache exists, its network count and configuration version;
it never displays passwords. Existing radio admin authorization is unchanged.

WLAN scan/association has a 20-second overall deadline, preferring stronger known
access points. Unknown or unreachable WLANs lead to one LTE fallback. A failed
wrong hardware, old release, wrong hash or blocked station aborts the update instead. The worker performs networking; the
repeater loop does not wait for association or HTTPS. A pre-existing station
connection is borrowed; an existing manual OTA access point is not seized.
The original `start ota` is still the separate upstream hotspot/manual uploader.

## One validation and installation pipeline

`UpdateSession` owns manifest parsing, target/version
checks, streaming SHA-256, inactive-slot writing and independent flash readback.
`A7670Http` and `WifiHttp` implement only their respective HTTP transports.
Both use the same endpoint, station public key, name, release floor and OTA
health/rollback mechanism. Since .8, firmware HTTPS skips certificate verification
and manifest signatures are not checked. The protected WLAN configuration path
still verifies certificates and requires its separate device credentials.

WLAN requests the image in one bounded HTTPS response and streams it through the
same 1024-byte buffer. It does not store the complete image in RAM. Response
length, byte range and identity content encoding are checked. Redirects and
gzip are disabled. LTE retains the qualified 32-KiB ranges and temporary UART
acceleration. Both transports retain whole-image and readback verification.
If a WLAN image is fully verified but only its report loses connectivity, the
staged image can be reported over LTE without downloading it again.

An activated image must retain `MESHCORE_WIFI_OTA:v1` as well as the LTE and UART
recovery capabilities inside the SDK-verified image. The publisher should check
all three markers before publishing. These markers prevent accidental capability
loss; they do not prove authenticity. Whole-image hashing still detects corruption.

## Shared WLAN administration

Credentials are provisioned locally into the `wifiota` NVS namespace. They are
not compiled into a distributable app image. A separate device login protects
access to the shared WLAN list; firmware download authorization remains based
on the existing station public key and per-artifact policy, without a token.
An operator's update page can link to the shared WLAN administration page,
using its existing users and permissions. Editing a shared WLAN affects that
shared list, rather than a separate MeshCore-only copy.

While connected, the device reads authenticated HTTPS
`<management-base>/device/config?version=<cached-version>` and acknowledges a
validated persisted version at `/device/ack`. Configuration schema 1 contains a
monotonic integer `version` and at most 16 unique networks, each with `ssid`,
`password`, and `security` (`open` or `wpa`). SSIDs are at most 32 bytes; WPA
passwords are 8–63 printable ASCII characters. Unchanged responses contain the
current version and `unchanged: true`. An unavailable configuration service
leaves the last validated cache usable.

If the cached WLANs fail, automatic mode can fetch this small configuration over
LTE first. A changed version permits one more WLAN attempt before transferring
the firmware through LTE. Firmware and configuration requests use separate
contexts; the private configuration Authorization header is neither logged nor
forwarded to the firmware endpoint. This can recover a device after all its
previous WLAN credentials have changed, provided LTE and the management login
remain usable.

## Local provisioning

Only the USB serial input accepts `!wifi <JSON>` followed by a carriage return.
In addition to schema/version/networks, the JSON supplies `management_url`,
`username` and `password` for that configuration service. Use an HTTPS example
such as `https://settings.example.invalid/wlan` in templates, and keep real
values in private operator files. The reply is only `WIFI_PROVISION_OK` or a
generic error; credentials are not echoed or returned over the radio CLI.

The reserved prefix is checked again at the actual serial read to close the
arrival race with the ordinary CLI. Provisioning prevents board sleep and new
update jobs. An incomplete frame times out after 15 seconds and discards late
bytes through its delimiter. No partial configuration is stored. NVS writes are
read back and only occur when the validated JSON changes.

Pace provisioning in small USB chunks, for example 48 bytes every 25 ms, because
the native USB receive ring is small and the radio loop remains active. End an
abandoned frame with a carriage return before retrying. Never provision by
ordinary echoed CLI commands or include operator credentials in Git history,
firmware headers, build flags, release images or diagnostic logs.

WLAN is switched off again after an owned session, including failed jobs. The
modem retains its previous sleep permission, and normal OTA boot checks still
probe its AT readiness to preserve LTE recovery. These AT probes are distinct
from downloading firmware over LTE. Battery/supply switching and measured sleep
current remain outside the externally powered hardware qualification.

## Hardware qualification, 2026-10-27

Version `1.18.0-lte-ota.7` passed on the existing externally powered XIAO/Wio/A7670E
setup. The final app is 1,361,216 bytes. No WLAN or configuration-service device
credentials were found in the app binary; those values were provisioned into NVS.

| Check | Observed result |
| --- | --- |
| Automatic WLAN preference | 64-KiB fixture verified via WLAN, modem UART counters unchanged |
| Shared configuration | Older local cache updated and acknowledged as three networks/version 7 |
| WLAN rescue | Invalid local WLANs recovered through a small LTE configuration request, then file transferred via WLAN |
| Actual LTE fallback | 64-KiB fixture verified via LTE when neither cached WLAN nor config refresh was available |
| Invalid signature/target/release floor | Rejected on hardware; no app activation or LTE fallback |
| Blocked artifact | WLAN returned HTTP_BLOCKED; modem unchanged |
| Fatal TLS peer alert | Rejected as TLS_CONFIG, with no fallback or activation |
| Wrong signed SHA-256 | Full valid image downloaded over WLAN, wrong signed hash rejected; old slot remained selected |
| SDK rollback | Previous LTE app intentionally left pending; automatic return in 64.56 s, WLAN cache preserved |
| USB provisioning failures | Incomplete frame timeout, late bytes not echoed, ordinary CLI recovered; real provisioning acknowledged |
| Signed WLAN OTA | One complete firmware response, signature/hash/flash checks, confirmed app0 boot, floor 10 |
| Installation report | Version reported through WLAN; identity and three-network cache preserved |
| Final cleanup | WLAN off, UART 115200, S LOW, station blocked again, own temporary fixtures removed |

Server timestamps show approximately 30 seconds from the test start marker to
verified reporting, and 60 seconds to installed reporting including reboot and
health confirmation. These are second-resolution server/host observations, not
a precise continuous USB timer. The host missed individual USB status replies
during testing; the original jobs continued and completion was independently
confirmed by read-only queries, OTA state and server reports. The update command
was not repeated. Continuous radio availability during every flash operation was
not established by those USB observations.

The TLS negative test reached a peer fatal alert before a client-side invalid
certificate-chain rejection. Certificate/hostname/date checking remains enabled
by the SDK's required CA verification; explicit certificate failure codes and the
SDK's generic socket/handshake-timeout code are separately covered by portable
tests. No check was disabled to obtain a successful update.

All 792 portable checks and 17 server test groups passed. Firmware was transferred
once for successful installation and once for the intentionally wrong-hash WLAN
test. The only LTE file payload in this WLAN qualification was the 64-KiB fallback
fixture; small configuration requests and protocol overhead are additional.
Test records distinguish transported data, verified data and confirmed installation.
