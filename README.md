# ESP32 Calendar Display

This project runs on an ESP32 and shows:

- a month view with highlighted calendar days that contain events
- the next 24 hours of Google Calendar events
- a short OpenWeather forecast
- battery voltage and firmware version

## Setup

1. Deploy the Apps Script from `google_script_read_cal.txt` to your Google account.
2. Build and flash `myCal_75_events.ino` to the board.
3. On first boot, connect to the Wi-Fi network `myCal-Setup`.
4. Open `http://192.168.4.1`.
5. Choose a hardware profile, then review or edit the pins if needed.
6. Choose the refresh interval for deep sleep wake-up.
7. Enter your Wi-Fi, Open-Meteo location, Google Script, and optional OTA URLs.
8. Save the form and let the device reboot.

Google Apps Script: [https://script.google.com/home/](https://script.google.com/home/)

## Configuration

Settings are now stored on the ESP32 using `Preferences`.

Required fields in the setup page:

- Wi-Fi SSID and password
- refresh interval: `6`, `12`, `18`, or `24` hours
- hardware profile and display pins
- city
- country code
- Google Apps Script ID

Optional fields:

- OTA version URL
- OTA firmware URL

Built-in hardware profiles:

- `esp32-waveshare`
- `esp32`
- `esp32c6`
- `custom`

The profile fills the default pins, and you can still override them in the form before saving.

Current preset pins:

- `esp32-waveshare`: `CS=15`, `DC=27`, `RST=26`, `BUSY=25`, `SCK=13`, `MOSI=14`, `DisplayPower=4`, `Battery=35`
- `esp32`: `CS=15`, `DC=27`, `RST=26`, `BUSY=25`, `SCK=13`, `MOSI=14`, `DisplayPower=4`, `Battery=35`
- `esp32c6`: `CS=1`, `DC=8`, `RST=14`, `BUSY=7`, `SCK=23`, `MOSI=22`, `DisplayPower=4`, `Battery=0`

The selected refresh interval controls the deep sleep timer between dashboard updates.

## OTA With GitHub

The GitHub workflow builds `esp32` and `esp32c6` firmware and publishes stable release asset names suitable for OTA.

Example OTA URLs:

- `https://github.com/<owner>/<repo>/releases/latest/download/version-esp32.txt`
- `https://github.com/<owner>/<repo>/releases/latest/download/myCal_75_events-esp32.bin`
- `https://github.com/<owner>/<repo>/releases/latest/download/version-esp32c6.txt`
- `https://github.com/<owner>/<repo>/releases/latest/download/myCal_75_events-esp32c6.bin`

You can paste those URLs directly into the setup page so the device can check GitHub Releases for updates.

## Weather Provider

The project now uses [Open-Meteo](https://api.open-meteo.com/).

Why this is a better fit here:

- no API key required
- simpler setup page
- easier sharing and flashing for other people
- daily forecast data matches an e-paper dashboard well

The device geocodes the configured city and country code through Open-Meteo, then loads a 2-day daily forecast for min temperature, max temperature, and weather code.

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
