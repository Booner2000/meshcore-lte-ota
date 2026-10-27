# Update protocol

The firmware client uses three endpoints below the configured base URL followed by `/api/v1/` (default base path `/meshcoreupdate`): `manifest`, `firmware/<id>` and `report`. Devices identify themselves with a public MeshCore key and URL-encoded name. There is no device token. Unknown devices must start blocked; approval is per public key and artifact.

## Manifest

For installation, return a plain JSON object. Version .8 deliberately does not
verify a manifest signature or the firmware HTTPS server certificate. HTTPS is
still used, without redirects. This removes server-authenticity protection: a
party able to substitute both the manifest and image can also replace the hash.
SHA-256 and the other checks below protect against accidental corruption and
incompatible images, not a malicious replacement of both responses.

The top-level JSON has these fields:

| Field | Meaning |
| --- | --- |
| `schema` | `3` for new unsigned publications; legacy `2` wrappers remain readable via their top-level fields |
| `id` | 24 lowercase hexadecimal characters |
| `version` | Human-readable release label |
| `target` | `xiao-s3-wio-b2b-lte-v1` |
| `capability` | `lte-ota-v1` |
| `artifact_kind` | `firmware` |
| `size` | Exact app-image size, within the inactive partition |
| `sha256` | SHA-256 of the complete raw app-image file, lowercase hex |
| `release_counter` | Positive integer greater than the device's accepted release floor |

The server should allocate release counters transactionally and never reuse them. Preserve its release database when migrating the service. The client reads top-level metadata and ignores legacy signature fields. The firmware URL is derived locally from the validated ID. New uploads allocate counters even without a signing key.

The binary must contain `MESHCORE_LTE_OTA:xiao-s3-wio-b2b-lte-v1:2`. This guards against accidentally shipping ordinary MeshCore firmware. Also require `MESHCORE_LTE_UART_RECOVERY:v1` and `MESHCORE_WIFI_OTA:v1`. Markers prevent accidental omission, not malicious replacement.

## Transfer and commit

Serve `Range` requests with HTTP 206 and the requested byte count. The client requests 32 KiB ranges, reads them in 1 KiB pieces and hashes the complete stream. At most one extra range is allowed across one download. It writes only to the inactive app partition, validates the ESP image with the SDK, then independently hashes that partition over the raw file size.

Cancellation and all validation failures prevent boot selection. The modem HTTP context and prior sleep permission are restored before activation. Pending metadata is stored in the dedicated `lteota` NVS namespace; existing MeshCore identity and settings are not reformatted. Partition-table and bootloader updates are outside this protocol.

## First boot and reporting

The SDK and installed bootloader must support rollback. The Arduino early auto-confirmation is overridden. A pending image has a 60-second boot timer. It is confirmed only after the radio setup reaches the main loop, the modem answers AT and at least 15 seconds of healthy loop execution have passed. This local health check does not prove radio reachability from a distant administrator.

An unconfirmed reset triggers the ESP-IDF rollback path. The release floor is persisted before confirmation; an interrupted confirmation can therefore require a new release counter for a retry. No eFuses are modified.

`report` accepts device status, artifact ID and SHA-256. Distinguish bytes served, bytes verified and a successfully booted installation. Device reports are informational because public-key identity alone is spoofable. The confirmed application automatically queues one installation-report job, with bounded retries. `lte update report` can retry it later without another firmware download.

See the [ESP-IDF 4.4.7 OTA documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/ota.html) for pending-image and rollback states.

## Tested modem read behavior

On the tested A7670E firmware, explicit nonzero `HTTPREAD` offsets were observed to advance relative to the current read position. Requests at offsets 0, 1024 and 2048 returned image data starting at 0, 2048 and 5120 respectively. Treating them as absolute positions is unsafe. The client therefore selects READMODE=0 and issues sequential `AT+HTTPREAD=<count>` commands. An ERROR before a data header is retried locally at most three times with bounded backoff. A partial binary response, invalid length or invalid trailer aborts that read. The final SHA-256 and independent flash readback remain mandatory.

## UART recovery compatibility

Recovery-capable builds additionally require `MESHCORE_LTE_UART_RECOVERY:v1`
inside the SDK-verified image before activation. Keep this marker check when
updating MeshCore. An older LTE image that lacks recovery is rejected.
The publishing server should enforce both LTE and UART recovery markers for new
uploads; existing historical artifacts may remain in its audit history.
See [the staged migration](uart-recovery.md) before enabling higher UART rates.

WLAN-first builds also require `MESHCORE_WIFI_OTA:v1` in the verified image.
Both transports execute the same `UpdateSession` validation and installation
pipeline. See [WLAN updates](wifi-updates.md) for routing and configuration.

## Compatibility and protected WLAN configuration

Version .7 still requires a valid signed manifest. To migrate without USB, publish
one .8 bridge artifact with a legacy signature for .7. Existing signed records
remain untouched. Subsequent .8 updates use plain manifests and need no keys.
Never reuse or decrease release counters.

The WLAN-configuration service is a separate trust boundary: its device username
and password stay in private NVS. Both WLAN and LTE must verify its TLS CA,
hostname and date before sending credentials. Firmware requests never carry
these credentials. Changing firmware trust settings must not weaken this path.
