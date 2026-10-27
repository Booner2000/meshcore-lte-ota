# Signed update protocol

The firmware client uses three endpoints below the configured base URL followed by `/api/v1/` (default base path `/meshcoreupdate`): `manifest`, `firmware/<id>` and `report`. Devices identify themselves with a public MeshCore key and URL-encoded name. There is no device token. Unknown devices must start blocked; approval is per public key and artifact.

## Manifest

For installation, return a JSON object containing `signed_payload` (a JSON string) and `signature` (128 lowercase hex characters). The signature is Ed25519 over the exact UTF-8 bytes of `signed_payload`, without a trailing newline and without hashing it separately. The client pins the operator's public signing key in its ignored `TrustedKey.h`.

The signed JSON has these fields:

| Field | Meaning |
| --- | --- |
| `schema` | `2` |
| `id` | 24 lowercase hexadecimal characters |
| `version` | Human-readable release label |
| `target` | `xiao-s3-wio-b2b-lte-v1` |
| `capability` | `lte-ota-v1` |
| `artifact_kind` | `firmware` |
| `size` | Exact app-image size, within the inactive partition |
| `sha256` | SHA-256 of the complete raw app-image file, lowercase hex |
| `release_counter` | Positive integer greater than the device's accepted release floor |

The server should allocate release counters transactionally and never reuse them. Preserve its release database when migrating the service. Fields outside `signed_payload` are not trusted for installation. The firmware URL is derived locally from the authenticated ID.

The binary must contain `MESHCORE_LTE_OTA:xiao-s3-wio-b2b-lte-v1:2`. This guards against accidentally shipping ordinary MeshCore firmware. It does not replace the signing key's trust boundary or a review of what gets signed.

## Transfer and commit

Serve `Range` requests with HTTP 206 and the requested byte count. The client requests 32 KiB ranges, reads them in 1 KiB pieces and hashes the complete stream. At most one extra range is allowed across one download. It writes only to the inactive app partition, validates the ESP image with the SDK, then independently hashes that partition over the raw file size.

Cancellation and all validation failures prevent boot selection. The modem HTTP context and prior sleep permission are restored before activation. Pending metadata is stored in the dedicated `lteota` NVS namespace; existing MeshCore identity and settings are not reformatted. Partition-table and bootloader updates are outside this protocol.

## First boot and reporting

The SDK and installed bootloader must support rollback. The Arduino early auto-confirmation is overridden. A pending image has a 60-second boot timer. It is confirmed only after the radio setup reaches the main loop, the modem answers AT and at least 15 seconds of healthy loop execution have passed. This local health check does not prove radio reachability from a distant administrator.

An unconfirmed reset triggers the ESP-IDF rollback path. The release floor is persisted before confirmation; an interrupted confirmation can therefore require a new release counter for a retry. No eFuses are modified.

`report` accepts device status, artifact ID and SHA-256. Distinguish bytes served, bytes verified and a successfully booted installation. Device reports are informational because public-key identity alone is spoofable. The confirmed application automatically queues one installation-report job, with bounded retries. `lte update report` can retry it later without another firmware download.

See the [ESP-IDF 4.4.7 OTA documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/system/ota.html) for pending-image and rollback states.

## Tested modem read behavior

On the tested A7670E firmware, explicit nonzero `HTTPREAD` offsets were observed to advance relative to the current read position. Requests at offsets 0, 1024 and 2048 returned image data starting at 0, 2048 and 5120 respectively. Treating them as absolute positions is unsafe. The client therefore selects READMODE=0 and issues sequential `AT+HTTPREAD=<count>` commands. An ERROR before a data header is retried locally at most three times with bounded backoff. A partial binary response, invalid length or invalid trailer aborts that read. The final signed SHA-256 and independent flash readback remain mandatory.

## UART recovery compatibility

Recovery-capable builds additionally require `MESHCORE_LTE_UART_RECOVERY:v1`
inside the SDK-verified image before activation. Keep this marker check when
updating MeshCore. A signed older LTE image that lacks recovery is rejected.
The publishing server should enforce both LTE and UART recovery markers for new
uploads; existing historical artifacts may remain in its audit history.
See [the staged migration](uart-recovery.md) before enabling higher UART rates.
