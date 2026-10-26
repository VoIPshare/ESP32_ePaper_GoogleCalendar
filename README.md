# ESP32 Calendar Display

A low-power calendar for an ESP32 and a 7.5" **4-color** ePaper display (black, white, red, yellow; GDEM075F52, 800×480). It shows:

- a month view: today in a red circle, days with events in yellow circles
- the events of the next 24 hours (time in red, title in black), merged from up to 3 calendars
- a 3-day Open-Meteo forecast in the yellow top bar
- battery voltage, firmware version and the time of the last update

The ESP32 reads your calendars (iCal `.ics` links) and the weather directly: no Google Apps Script, server or API key is needed.

## Battery use

The display wakes once per refresh interval (6, 12, 18 or 24 hours), connects to Wi-Fi, downloads the calendars and the forecast, turns Wi-Fi off, redraws the screen and goes back to deep sleep. A city name is geocoded only once and the coordinates are kept across deep sleep. If Wi-Fi is not available, the board goes straight back to sleep until the next interval.

## Setup

1. Flash the firmware: a prebuilt release (flash `esp32-full.bin` or `esp32c6-full.bin` at address `0x0`), or build it yourself (see below).
2. On first boot the screen shows "Configuration required". Join the Wi-Fi network `myCal-Setup`.
3. The setup page opens by itself (captive portal). If it doesn't, browse to `http://192.168.4.1`.
4. Fill in the form and press **Save configuration**. The device restarts and draws the calendar.

### The setup page

| Field | What to enter |
| --- | --- |
| **WiFi network / password** | Your Wi-Fi. Leave the password empty to keep the saved one. |
| **Refresh interval** | How long the board sleeps between updates: `6`, `12`, `18` or `24` hours. |
| **Hardware profile** | Your board's wiring (see the table below). Pick **Custom** to enter your own pins. |
| **Calendar iCal URLs** | Up to 3 `.ics` links, merged into one calendar. See [Getting your Google Calendar link](#getting-your-google-calendar-link). |
| **Weather location** | A city, optionally with a 2-letter country code (`London, GB`), or `latitude,longitude` (`51.51,-0.13`). |
| **Timezone** | A POSIX TZ string, e.g. `EST5EDT,M3.2.0/2,M11.1.0/2` (US Eastern), `CET-1CEST,M3.5.0,M10.5.0/3` (Central Europe), `GMT0BST,M3.5.0/1,M10.5.0` (UK). [List of TZ strings](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv). |
| **GitHub updates** | Optional: check this repository's releases for new firmware, at most every 12 hours (at a wake-up). |

At least one iCal URL and a weather location are required. Settings are stored in the board's flash (`Preferences`) and survive firmware updates. Settings saved by older firmware (city + country code) are converted automatically; the old Google Script ID is no longer used, so add an iCal link once after updating.

### Getting your Google Calendar link

1. Open [Google Calendar](https://calendar.google.com) on a computer and go to **Settings**.
2. Under **Settings for my calendars**, pick the calendar.
3. Open **Integrate calendar** and copy one of these:
   - **Public address in iCal format**: works only if the calendar is public.
   - **Secret address in iCal format**: works for a private calendar. Treat it like a password: anyone with it can read your calendar.

Any other service that publishes an `.ics` link (Outlook, iCloud, Nextcloud, …) works too. Recurring events (daily, weekly, monthly, yearly, with `INTERVAL`, `UNTIL`, `COUNT`, `BYDAY` and excluded dates) and cancelled events are handled.

If one of several calendars can't be read, the others are still shown and the status line says `1 calendar feed failed` in red. If none can be read, the event list shows `Calendar unavailable`.

## Wiring

Use the profile that matches your board, then wire the display to these pins:

| Profile | EPD CS | EPD DC | EPD RST | EPD BUSY | EPD SCK | EPD MOSI | Display Power | Battery ADC |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `esp32-waveshare` | 15 | 27 | 26 | 25 | 13 | 14 | 4 | 35 |
| `esp32` | 15 | 27 | 26 | 25 | 13 | 14 | 4 | 35 |
| `esp32c6` | 1 | 8 | 14 | 7 | 23 | 22 | 4 | 0 |

The profile fills in the default pins, and you can still change them in the form before saving.

## Building the firmware

With [arduino-cli](https://arduino.github.io/arduino-cli/) (ESP32 core 3.3.12, libraries listed in [sketch.yaml](sketch.yaml)):

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 --board-options PartitionScheme=default .
```

Keep the `default` partition scheme: it has the two app slots that OTA updates need.

## OTA with GitHub

The workflow in [.github/workflows/compile.yml](.github/workflows/compile.yml):

- compiles the `esp32` and `esp32c6` firmware on every push and pull request (a build check, no release);
- when run manually (**Actions → Build Firmware → Run workflow**), publishes a GitHub release named after `FW_VERSION` in [ESP32_ePaper_GoogleCalendar.ino](ESP32_ePaper_GoogleCalendar.ino). It refuses a version that was already released, so bump `FW_VERSION` first.

A device only installs a version newer than its own, and only from the latest **stable** release.

### Beta versions

Betas live on the `beta` branch and are published as GitHub **pre-releases**. GitHub never marks a pre-release as "latest", so devices updating over the air don't see it: only people who flash it by hand get it.

To publish a beta:

1. On the `beta` branch, set a pre-release version, e.g. `#define FW_VERSION "0.2.0-beta.1"`, and push.
2. Run the workflow and pick the `beta` branch in **Use workflow from**. It creates the pre-release `firmware-0.2.0-beta.1`.

To try a beta, download `esp32-full.bin` (or `esp32c6-full.bin`) from the pre-release on the [Releases page](https://github.com/VoIPshare/ESP32_ePaper_GoogleCalendar/releases) and flash it at `0x0`. Your settings are kept unless you erase the flash. A beta sorts below its stable version (`0.2.0-beta.1` < `0.2.0`), so with GitHub updates enabled the device moves to `0.2.0` by itself when it is released, and never goes back to an older stable version.

To release the beta as stable, merge `beta` into `main`, set `FW_VERSION` to `0.2.0` and run the workflow on `main`. Stable versions can only be released from `main`.

Each release contains, per board (`esp32`, `esp32c6`):

| File | Use |
| --- | --- |
| `<board>-full.bin` | Complete image for flashing over USB at address `0x0` (first install, or to try a beta) |
| `<board>-ota.bin` | Downloaded by the device for over-the-air updates |
| `<board>-version.txt` | The release version, checked by the device before downloading |

The device builds its update links from these names, for the chip the firmware was built for:

- `https://github.com/VoIPshare/ESP32_ePaper_GoogleCalendar/releases/latest/download/esp32-version.txt`
- `https://github.com/VoIPshare/ESP32_ePaper_GoogleCalendar/releases/latest/download/esp32-ota.bin`
- `https://github.com/VoIPshare/ESP32_ePaper_GoogleCalendar/releases/latest/download/esp32c6-version.txt`
- `https://github.com/VoIPshare/ESP32_ePaper_GoogleCalendar/releases/latest/download/esp32c6-ota.bin`

Firmware before 0.2.0 used other file names and could not follow GitHub's download redirects, so those devices need to be flashed over USB once.

## Project structure

| File | Purpose |
| --- | --- |
| `ESP32_ePaper_GoogleCalendar.ino` | Setup page, Wi-Fi, deep sleep, OTA and drawing |
| `calendar.cpp` / `calendar.h` | Streams and parses the iCal feeds (recurrences, exclusions), merges them |
| `weather.cpp` / `weather.h` | Open-Meteo geocoding (cached) and the daily forecast |
| `myCal_75.h` | Configuration structure and prototypes |
| `icons/` | 48×48 weather icons |

## Fonts

The font headers are not guaranteed to be redistributable, so you may want to generate your own.

Example:

```bash
fontconvert Geologica-Bold.ttf 14 32 255 > Geologica_Bold14pt8b.h
```

The character range was adjusted to include accented characters.

## Notes

- Only one battery holder is needed in the 3D model.
- The ESP32 slot is designed for the Lolin D32.
- Deep sleep current is about 80 uA on the Lolin D32.
- With the original Waveshare ESP32 board, deep sleep was closer to 0.5 mA even after removing the power LED.
