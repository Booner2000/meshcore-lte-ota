# MeshCore LTE OTA

Experimental LTE update extension for the Seeed XIAO ESP32-S3 with Wio-SX1262 and SIMCom A7670E. This repository contains only the extension, portable tests and a small integration patch. MeshCore is fetched separately at the commit in `upstream.json`.

## Current status

**Not ready for unattended deployment.** Modem diagnostics, sleep/wake, HTTPS streaming and SHA-256 download verification are implemented. Downloads are discarded. `start lte update` currently runs a simulation; it does not install firmware. The flash writer, signed update validation and application health based rollback remain to be implemented and tested.

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
2. Copy `TrustAnchor.example.h` to `TrustAnchor.h` and insert the appropriate root CA PEM. Certificate provisioning on a fresh modem still requires verification.
3. Run `python scripts/prepare_meshcore.py`. It clones the pinned upstream source into ignored `build/MeshCore`, checks and applies the two-file integration patch, then copies the module and build environments.
4. Inside `build/MeshCore`, run `pio run -e Xiao_S3_WIO_repeater_lte_hwtest`. This builds without uploading. This environment enables S on GPIO2. The other supplied environments leave S unassigned or disable the extension.

Local configuration files, firmware binaries and build directories are ignored. Never commit device keys, credentials, databases, logs or deployment configuration. Review staged files before publishing.

## CLI

- `lte modem test`, `lte modem status`, `lte modem io`
- `lte modem sleep`, `lte modem wake`
- `start lte update` — simulation only
- `start lte download` — real LTE download and checksum verification; consumes data
- `lte update report` — retry reporting a verified download cached in RAM
- `lte update status`, `lte update cancel`

Radio administration continues through MeshCore's existing admin authorization. Device requests use the public MeshCore key and name, without a device token. Such identity is spoofable and must not be treated as cryptographic device authentication.

## Tests and maintenance

Run `python scripts/test_native.py --compiler g++` or add `--zig` when using the Zig compiler. These portable tests cover the state machine and AT/sleep handling; they do not establish OTA safety or test the modem HTTPS implementation on hardware.

For an upstream update, change the pinned commit, review and refresh `integration/meshcore.patch`, then run native tests, a full firmware build and hardware verification. No old project history or operator configuration is included.

MeshCore integration code retains the upstream MIT license; see `LICENSE`.
