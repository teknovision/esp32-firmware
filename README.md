# ESP32 firmware (pull updates, signed)

This repo holds the ESPHome firmware for my ESP32 devices. The first device is Lighty, the room light controller (`devices/room-light.yaml`).

No file in this repo holds a secret. There is no Wi-Fi password, no OTA password and no API key in any file. Two secrets are used only at build time: the OTA signing key and the web page password (see "Signed updates" and "Accepted risks"). Each device gets its settings at runtime:

- Wi-Fi is entered once through a setup hotspot. The device keeps it across firmware updates.
- Device settings (for Lighty: the Hue bridge, key and ids) are typed on the device web page. They are kept in flash.

Updates are pulled, not pushed. A GitHub release builds the firmware and puts it on GitHub Pages. Each device checks its manifest every 6 hours and installs a new build on its own.

ESPHome version: 2026.9.1, framework esp-idf.

## Layout

- `packages/pull-update.yaml`: shared. Project name and version, the "Firmware" update entity, the "Auto update" switch, the "Check for update" button, http_request and OTA by http_request only.
- `packages/runtime-wifi.yaml`: shared. Wi-Fi with no network in the file, the open `<name>-setup` hotspot, the captive portal and the web page with its login (`web_server: ota: false`, `auth:`, `local: true`).
- `devices/<device>.yaml`: one file per device. The release workflow builds every file in this folder. Each device turns on signed updates and downgrade protection under `esp32: framework: advanced:`.
- `keys/signing_key.pem`: the private OTA signing key. Never in git (`keys/` and `*.pem` are ignored). Only on the build machine and in the GitHub secret `OTA_SIGNING_KEY`.
- `ota-public-key.pub`: the public half of that key (raw 64 bytes). Public, committed on purpose.
- `.github/workflows/release.yml`: builds, signs and publishes on each GitHub release.

## Signed updates

Every firmware is signed with the private key in `keys/signing_key.pem` (scheme `ecdsa_v1`, works on every ESP32 revision). The device holds the matching public key and installs only firmware signed with it. This also covers uploads through the setup hotspot.

No eFuse is burned. If the key is lost, flash a build with a new key once over USB (`esphome run devices/<device>.yaml`). The saved Wi-Fi and settings stay.

Create a key once:

```sh
espsecure generate-signing-key --version 1 keys/signing_key.pem
```

Keep a copy somewhere safe, outside this repo. Put the full PEM text in the GitHub secret `OTA_SIGNING_KEY`.

`ota-public-key.pub` is the public key the devices trust. The release workflow checks every build against it and stops if the build is signed with any other key, so a wrong `OTA_SIGNING_KEY` secret never reaches the devices. Create it from a new key:

```sh
espsecure extract-public-key --version 1 --keyfile keys/signing_key.pem ota-public-key.pub
```

Changing the key means a USB reflash of every device, because each device only accepts updates signed with the key it was built with.

After a failed or refused install (bad signature, older version) the device does not try again by itself, not even for a newer release, until it reboots or someone presses Install on the "Firmware" entity.

## Build locally

You need the key file at `keys/signing_key.pem` and a web password:

```sh
esphome -s web_password 'AbCd1234AbCd1234' config devices/<device>.yaml
esphome -s web_password 'AbCd1234AbCd1234' compile devices/<device>.yaml
```

A build without `web_password` fails on purpose. A local build has version `0.0.0`, so the next release always replaces it. The password must be 16 to 64 letters or digits, nothing else.

## Add a new ESP32 device

1. Copy an existing device file to `devices/<device>.yaml`. The file name becomes the folder on GitHub Pages.
2. Set these substitutions:
   - `name`: the device name, for example `desk-sensor`.
   - `project_name`: needs a dot, for example `teknovision.desk`.
   - `update_manifest_url`: `https://teknovision.github.io/esp32-firmware/firmware/<device>/manifest.json`.
   - `verify_ssl`: leave it out (default true) unless the device talks to a local server with a private certificate.
3. Include both packages:

   ```yaml
   packages:
     pull_update: !include ../packages/pull-update.yaml
     runtime_wifi: !include ../packages/runtime-wifi.yaml
   ```

4. Add signed updates and downgrade protection, the same as in `devices/room-light.yaml`:

   ```yaml
   esp32:
     variant: esp32
     framework:
       type: esp-idf
       advanced:
         signed_ota_verification:
           signing_scheme: ecdsa_v1
           signing_key: ../keys/signing_key.pem
         enable_ota_downgrade_protection: true
   ```

   `ecdsa_v1` only works on the original ESP32. Other chips need a different scheme.
5. Do not add `ssid`, `password`, `networks`, `ota: platform: esphome` or any `!secret`. Put anything private in a template `text` entity with `restore_value: true` and `optimistic: true`. Use `mode: password` for keys.
   Do not add an `api:` block to a device that holds a password text. The native API sends text values unmasked, so any LAN client could read the key.
6. Check it: `esphome -s web_password 'AbCd1234AbCd1234' config devices/<device>.yaml`, then the same with `compile`.

## First setup of a device

1. Flash once over USB: `esphome -s web_password 'AbCd1234AbCd1234' run devices/<device>.yaml` and pick the USB port. The key file must be at `keys/signing_key.pem` and be the same key as in the GitHub secret `OTA_SIGNING_KEY`, or the device refuses the released updates. Use the same password as in the GitHub secret `WEB_PASSWORD`, so the login stays the same after the first update.
2. The device finds no Wi-Fi and opens the hotspot `<name>-setup` (no password). Join it with a phone. The setup page opens by itself, or go to http://192.168.4.1.
3. Pick your Wi-Fi and enter its password. The device restarts and joins it.
4. Open `http://<name>.local` and log in (user `admin`, the web password). Fill in the config texts in the Configuration section.
   For Lighty: "Hue bridge" (IP or host name), "Hue key" (the Hue application key), "Hue room id" (the grouped_light id of the room), "Hue Up light id" and "Hue plug light id". Until the bridge and key are filled in, Lighty sends nothing to Hue and logs one warning. Fill in the bridge before the key.
   Changing "Hue bridge" later clears "Hue key" on purpose. Enter the key again after every bridge change.
5. From now on the device updates itself.

Moving Lighty from the old `home-sensors/esphome/light-sensor.yaml`: the old firmware had Wi-Fi in the file, so after the first flash of this firmware Lighty has no saved Wi-Fi. Do steps 2 to 4 once. Sliders and switches keep their values because the entity names did not change. The old file stays as the fallback.

## Release a new version

1. Commit and push the change to `main`.
2. On GitHub, create a release with a tag like `v2026.11.24-1`: `v`, the date as `year.month.day`, then optionally `-` and 1 to 3 digits. Any other tag stops the workflow, also one without the leading `v`. The firmware version comes from the tag: drop the `v`, turn `-` into `.`. So `v2026.11.24-1` becomes `2026.11.24.1`.
3. The workflow builds and signs every device, checks the signature against `ota-public-key.pub` and publishes `firmware/<device>/manifest.json` and the `.ota.bin` next to it.
4. Devices pull it within 6 hours. To update now, press "Check for update" on the device page. "Auto update" is on by default. Anyone who can log in to the page can turn it off or press Install on the "Firmware" entity. With "Auto update" off, the device only reports the update.

The device installs a newer version and refuses an older one (downgrade protection). The same version can be installed again. To roll back, release the old code under a new, higher tag. You can start the workflow by hand (Actions, Release firmware, Run workflow) with an existing tag. It always builds the code of that tag, never the branch, and stops with an error if the tag does not exist.

One-time GitHub setup:

- Settings, Pages, Source "GitHub Actions".
- Settings, Secrets and variables, Actions: add `OTA_SIGNING_KEY` (the PEM text of `keys/signing_key.pem`) and `WEB_PASSWORD` (the web page password, 16 to 64 letters or digits). The workflow stops if either is empty or the password has any other character.

## Accepted risks

- Open setup hotspot. The device opens `<name>-setup` with no password at once when no Wi-Fi is saved, or after the saved Wi-Fi has been down for 10 minutes (`wifi_ap_timeout`). While it is up it is open. Only the Wi-Fi setup pages (including `/wifisave`) skip the web login, so anyone nearby can join it and set a different Wi-Fi. A device moved to a foreign Wi-Fi sends its next Hue command there, so the Hue key can leak. Kept on purpose (owner decision 26 Nov 2026): a Wi-Fi change needs no cable. A firmware upload on the hotspot is behind the web login and must also be signed with our key. Someone who wants to force the hotspot could jam or deauthenticate the home Wi-Fi. Turning on PMF (802.11w, protected management frames) or WPA3 on the home router makes that much harder.
- Unverified TLS while `verify_ssl` is false. Lighty needs this for the Hue bridge certificate. ESPHome has one switch for the whole device, so no download is certificate-checked. The firmware is safe anyway: the device checks its signature. The Hue key is not: a device on the LAN that pretends to be the Hue bridge can receive the Hue key with the next Hue command.
- Web login in a public firmware. The page asks for a login (digest). The password is compiled into the firmware, and the firmware file is public, so anyone who downloads and takes it apart can read the password. Someone who can sniff the LAN can also replay a captured digest login. The login keeps out casual LAN users and websites, not a targeted attacker. Anyone with the password can read the values and change settings, including "Auto update". The "Hue key" is hidden on the web page (shown as `********`, never sent to the browser). There is no native API, so no other way to read it over the network. Pointing "Hue bridge" at another host clears the key first, so it cannot be sent there.
- Lost signing key. Without `keys/signing_key.pem` no new firmware can be installed over the air. Each device then needs one USB flash with a new key. Keep a safe copy of the key.

## Before upgrading ESPHome past 2027.1

Stored values are found by a key built from the entity name (texts also use min_length, max_length and pattern). ESPHome plans to change how these keys are built in 2027.1.0 (`get_preference_hash` is deprecated in `esphome/core/entity_base.h`). After upgrading past 2027.1, check on each device that sliders, switches and config texts still hold their values. Retype them if they are empty. For the same reason, never rename an entity or change a text's length limits.
