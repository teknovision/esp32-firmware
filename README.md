# ESP32 firmware (pull updates, no secrets)

This repo holds the ESPHome firmware for my ESP32 devices. The first device is Lighty, the room light controller (`devices/room-light.yaml`).

The firmware contains no secrets. There is no Wi-Fi password, no OTA password and no API key in any file. Each device gets its settings at runtime:

- Wi-Fi is entered once through a setup hotspot. The device keeps it across firmware updates.
- Device settings (for Lighty: the Hue bridge, key and ids) are typed on the device web page. They are kept in flash.

Updates are pulled, not pushed. A GitHub release builds the firmware and puts it on GitHub Pages. Each device checks its manifest every 6 hours and installs a new build on its own.

ESPHome version: 2026.9.1, framework esp-idf.

## Layout

- `packages/pull-update.yaml`: shared. Project name and version, the "Firmware" update entity, the "Auto update" switch, the "Check for update" button, http_request and OTA by http_request only.
- `packages/runtime-wifi.yaml`: shared. Wi-Fi with no network in the file, the open `<name>-setup` hotspot, the captive portal, and `web_server: ota: false`.
- `devices/<device>.yaml`: one file per device. The release workflow builds every file in this folder.
- `.github/workflows/release.yml`: builds and publishes on each GitHub release.

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

4. Do not add `ssid`, `password`, `networks`, `ota: platform: esphome` or any `!secret`. Put anything private in a template `text` entity with `restore_value: true` and `optimistic: true`. Use `mode: password` for keys.
   Do not add an `api:` block to a device that holds a password text. The native API sends text values unmasked, so any LAN client could read the key.
5. Check it: `esphome config devices/<device>.yaml`, then `esphome compile devices/<device>.yaml`.

## First setup of a device

1. Flash once over USB: `esphome run devices/<device>.yaml` and pick the USB port.
2. The device finds no Wi-Fi and opens the hotspot `<name>-setup` (no password). Join it with a phone. The setup page opens by itself, or go to http://192.168.4.1.
3. Pick your Wi-Fi and enter its password. The device restarts and joins it.
4. Open `http://<name>.local`. Fill in the config texts in the Configuration section.
   For Lighty: "Hue bridge" (IP or host name), "Hue key" (the Hue application key), "Hue room id" (the grouped_light id of the room), "Hue Up light id" and "Hue plug light id". Until the bridge and key are filled in, Lighty sends nothing to Hue and logs one warning. Fill in the bridge before the key.
   Changing "Hue bridge" later clears "Hue key" on purpose. Enter the key again after every bridge change.
5. From now on the device updates itself.

Moving Lighty from the old `home-sensors/esphome/light-sensor.yaml`: the old firmware had Wi-Fi in the file, so after the first flash of this firmware Lighty has no saved Wi-Fi. Do steps 2 to 4 once. Sliders and switches keep their values because the entity names did not change. The old file stays as the fallback.

## Release a new version

1. Commit and push the change to `main`.
2. On GitHub, create a release with a tag like `v2026.11.24-1`. The tag becomes the firmware version.
3. The workflow builds every device and publishes `firmware/<device>/manifest.json` and the `.ota.bin` next to it.
4. Devices pull it within 6 hours. To update now, press "Check for update" on the device page. "Auto update" is on by default. Any LAN user can turn it off or press Install on the "Firmware" entity. With "Auto update" off, the device only reports the update.

The device installs any version whose text differs from its own. It does not check "newer". You can start the workflow by hand (Actions, Release firmware, Run workflow) with an existing tag. It always builds the code of that tag, never the branch, and stops with an error if the tag does not exist. Running it with an old tag rolls the devices back to that version.

One-time GitHub setup: Settings, Pages, Source "GitHub Actions".

## Accepted risks

- Open setup hotspot. With no saved Wi-Fi, or after the saved Wi-Fi has been down for 10 minutes (`wifi_ap_timeout`), the device opens `<name>-setup` with no password. While it is up, anyone nearby can join it and set a different Wi-Fi or upload their own firmware through the captive portal. Such firmware can read everything stored on the device, including the saved home Wi-Fi password and the Hue key. Someone who wants to force this could jam or deauthenticate the home Wi-Fi. Turning on PMF (802.11w, protected management frames) or WPA3 on the home router makes that much harder.
- Unverified TLS while `verify_ssl` is false. Lighty needs this for the Hue bridge certificate. ESPHome has one switch for the whole device, so the firmware download from GitHub Pages is not certificate-checked either. The MD5 in the manifest only proves the file arrived whole, not who made it. Someone who can intercept the device traffic could serve their own firmware.
- LAN users see the web page. Anyone on the home network can open `http://<name>.local`, read the values and change settings, including "Auto update". The "Hue key" is hidden on the web page (shown as `********`, never sent to the browser). There is no native API, so no other way to read it over the network. Pointing "Hue bridge" at another host clears the key first, so it cannot be sent there.

## Before upgrading ESPHome past 2027.1

Stored values are found by a key built from the entity name (texts also use min_length, max_length and pattern). ESPHome plans to change how these keys are built in 2027.1.0 (`get_preference_hash` is deprecated in `esphome/core/entity_base.h`). After upgrading past 2027.1, check on each device that sliders, switches and config texts still hold their values. Retype them if they are empty. For the same reason, never rename an entity or change a text's length limits.
