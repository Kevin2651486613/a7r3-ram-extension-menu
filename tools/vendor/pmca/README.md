# Built-in Minimal `pmca` Subset (`ma1co/Sony-PMCA-RE`)

This directory vendors a **minimal MIT-licensed subset** of [`ma1co/Sony-PMCA-RE`](https://github.com/ma1co/Sony-PMCA-RE) (`Copyright (c) 2015 ma1co`, see [`LICENSE-MIT.txt`](LICENSE-MIT.txt)) so users of `a7r3-ram-extension-menu` do **not** need to clone `Sony-PMCA-RE` separately or install APK/firmware/TLS dependencies (`asn1crypto`, `AxmlParserPY`, `ecdsa`, `PyYAML`, `tlslite-ng`, `six`).

## Included Scope

- `pmca/util/__init__.py` — binary struct packing helpers
- `pmca/usb/*` — Sony USB protocol, Senser handshake/auth (`pycryptodomex`), and `libusb` / optional Windows MSC / macOS USB drivers
- `pmca/platform/backend/senser.py` — `SenserPlatformBackend` for `/tmp_bt` file transfers & service terminal transport
- `pmca/commands/usb.py` — trimmed to `UsbDriverList`, `importDriver`, `listDevices`, and `getDevice`

## Intentionally Omitted Upstream Modules

Upstream `apk`, `appstore`, `backup`, `commands/{backup,market}`, `firmware`, `installer`, `io`, `marketclient`, `marketserver`, `platform/{android,backup,properties,tweaks,backend/usb}`, `shell`, `spk`, `ui`, `util/http.py`, and `xpd` are omitted.
