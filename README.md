# MeshCore WLAN / LTE OTA

Update extension for the Seeed XIAO ESP32-S3 with Wio-SX1262 and SIMCom A7670E. WLAN is preferred, with LTE as fallback; both use one checksum-validated update pipeline. This repository contains only the extension, portable tests and a small integration patch. MeshCore is fetched separately at the commit in `upstream.json`.

## Current status

**Version .8 changes only firmware authenticity checks.** Firmware HTTPS intentionally skips certificate verification and manifests no longer require signatures. The separate WLAN-credential service still requires device authentication and valid server certificates. Previous .7 hardware qualification covered signed WLAN installation, WLAN-first routing, bounded LTE fallback, flash readback, boot health confirmation, installation reporting and rollback. The changed .8 policy needs separate hardware qualification. See the [WLAN qualification](docs/wifi-updates.md#hardware-qualification-2026-10-27) and [earlier LTE report](docs/hardware-tests.md) for results and limits. Battery operation and supply switching are not qualified. Installation commands perform real OTA when `LTE_REAL_OTA=1`; the base environment retains simulation.

The operating web service and deployment configuration are not included in this initial firmware-only repository. The download client expects the manifest, firmware range and report endpoints under `/meshcoreupdate/api/v1/`.

## Hardware tested

| Signal | XIAO pin |
| --- | --- |
| UART TX to modem RX | D6 / GPIO43 |
| UART RX from modem TX | D7 / GPIO44 |
| Modem board external S | D1 / GPIO2 |

Use the correct UART voltage adaptation and common ground. The modem uses a separate 5 V supply. For the tested modem board, external S HIGH keeps it awake; LOW permits sleep after `AT+CSCLK=1`. This board already inverts S internally. The download keeps S HIGH and restores the prior sleep permission afterwards. Supply switching and PWRKEY are not implemented. UART defaults to 115200, 8N1.

## Configure and build

1. Copy `lib/LTEUpdate/src/LTEDeploymentConfig.example.h` to `LTEDeploymentConfig.h` in the same directory. Set your own HTTPS origin and APN.
2. Copy `TrustAnchor.example.h` to `TrustAnchor.h` and insert the root CA for the protected WLAN-configuration service. Firmware downloads do not use this trust anchor. `TrustedKey.h` is no longer required.
3. Run `python scripts/prepare_meshcore.py`. It clones the pinned upstream source into ignored `build/MeshCore`, checks and applies the two-file integration patch, then copies the module and build environments.
4. Inside `build/MeshCore`, run `pio run -e Xiao_S3_WIO_repeater_lte_hwtest`. This builds without uploading. This environment enables S on GPIO2. The other supplied environments leave S unassigned or disable the extension.

Local configuration files, firmware binaries and build directories are ignored. Never commit device keys, credentials, databases, logs or deployment configuration. Review staged files before publishing.

## CLI

- `lte modem test`, `lte modem status`, `lte modem io`
- `lte modem sleep`, `lte modem wake`
- `start lte update` — real OTA with `LTE_REAL_OTA=1`; otherwise simulation
- `start lte download` — real LTE download and checksum verification; consumes data
- `lte update report` — retry a download or persistent installation report without downloading again
- `lte ota status` — running slot, image state, pending health and release floor
- `lte update url` — show the current HTTPS base URL
- `lte update url https://updates.example.invalid/meshcoreupdate` — persist a new base URL in NVS
- `lte update url reset` — restore the build default
- `lte update status`, `lte update cancel`

Radio administration continues through MeshCore's existing admin authorization. Device requests use the public MeshCore key and name, without a device token. Such identity is spoofable and must not be treated as cryptographic device authentication.

See [the update protocol](docs/update-protocol.md) for the server contract and boot safety rules.

Use **`start update`** for automatic WLAN-first installation. The existing
`start lte update` explicitly forces LTE. See [WLAN provisioning and shared
administration](docs/wifi-updates.md) for commands, private NVS credentials and
configuration recovery. Neither WLAN passwords nor configuration-service device
credentials are built into the app image.

See [transfer measurements](docs/transfer-performance.md) for tested baud rates, data overhead, compression potential and the reset-recovery prerequisite for higher production speeds.

The [UART recovery and staged migration](docs/uart-recovery.md) describes bounded baud detection, baseline restoration and the valid fallback check before faster transfers. gzip remains disabled.

URL changes are admin commands, take effect for the next job, survive reboot and are rejected while a job or boot health check is active. The maximum base URL length is 112 characters; credentials, query strings, fragments and unsafe path characters are rejected. The firmware endpoint does not require a trusted CA or signing key. Changing it does not change the separately authenticated WLAN-configuration endpoint. Manifest, firmware and report routes all follow the configured base path. The URL extension has native regression coverage and is compiled; on-device persistence and a move to another HTTPS host still require hardware validation.

## Tests and maintenance

Run `python scripts/test_native.py --compiler g++` or add `--zig` when using the Zig compiler. These tests cover the state machine, AT/sleep handling, install transaction boundaries and the exact buffered HTTP reader with binary data and injected failures. They complement hardware tests; they do not prove remote radio reachability or battery behavior.

For an upstream update, change the pinned commit, review and refresh `integration/meshcore.patch`, then run native tests, a full firmware build and hardware verification. No old project history or operator configuration is included.

MeshCore integration code retains the upstream MIT license; see `LICENSE`.
