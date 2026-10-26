#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

#include <GxEPD2_4C.h>
#include <epd4c/GxEPD2_750c_GDEM075F52.h>
#include "Bangers_Regular18pt7b.h"
#include "RobotoMono_Thin7pt7b.h"

#include "Geologica_Bold14pt8b.h"

#include "icons/icon_sunny.h"
#include "icons/icon_partlycloudy.h"
#include "icons/icon_cloudy.h"
#include "icons/icon_rainy.h"
#include "icons/icon_snowy.h"
#include "icons/icon_fog.h"
#include "icons/icon_rainandthunder.h"
#include "icons/icon_snow.h"

#include "myCal_75.h"

// ===================
// WIFI CONFIG
// ===================
const char* AP_NAME = "myCal-Setup";
const char* OTA_REPO_BASE = "https://github.com/VoIPshare/ESP32_ePaper_GoogleCalendar/releases/latest/download";
const char* DEFAULT_DEVICE_TZ = "EST5EDT,M3.2.0/2,M11.1.0/2";

#ifndef FW_VERSION
#define FW_VERSION "0.2.2"
#endif
#define uS_TO_S_FACTOR 1000000ULL

// ===================
// DISPLAY (RAM-SAFE BUFFER)
// ===================
using DisplayType = GxEPD2_4C<GxEPD2_750c_GDEM075F52, GxEPD2_750c_GDEM075F52::HEIGHT / 2>;
DisplayType* displayHandle = nullptr;
#define display (*displayHandle)


CalendarData cal;
bool calendarOk = false;
int calendarFailedFeeds = 0;

DayForecast forecast[FORECAST_DAYS];
bool weatherOk = false;

bool timeSynced = false;
DeviceConfig config;
bool hasStoredConfig = false;
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

static const char* const ICS_PREF_KEYS[ICS_MAX_FEEDS] = { "ics_url", "ics_url2", "ics_url3" };
RTC_DATA_ATTR uint32_t lastOtaCheckEpoch = 0;

// ===================
// SCREEN LAYOUT
// ===================
const int SCREEN_W = 800;
const int SCREEN_H = 480;

const int TOP_H  = 55;   
const int BODY_H = SCREEN_H - TOP_H;
const int COL_W  = SCREEN_W / 2;

// ===================
// SETUP
// ===================
void setup()
{
  Serial.begin(115200);

  loadConfig();
  initDisplayHardware();

  if (!isConfigComplete()) {
    startConfigPortal();
    return;
  }

  connectWiFi();
  initTime();
  if (shouldCheckForOTA()) {
    checkForOTA();
  }

  // Everything is fetched in this single wake, then WiFi goes off before the
  // (slow) display refresh, so the radio is on as briefly as possible.
  calendarOk = fetchCalendarIcs(config.icsUrls, ICS_MAX_FEEDS, cal, &calendarFailedFeeds);
  weatherOk = fetchForecast(config.weatherLocation, forecast, FORECAST_DAYS);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  drawUI();

  display.hibernate();

  if (config.pinDisplayPower >= 0) {
  digitalWrite(config.pinDisplayPower, LOW);
  }
  Serial.println("Going to sleep. Bye");
  esp_sleep_enable_timer_wakeup(sleepDurationUs());
  esp_deep_sleep_start();

}

void loop() {}

// ===================
// WIFI
// ===================
void connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSsid.c_str(), config.wifiPass.c_str());
  Serial.print("Connecting WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30  ) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if   (WiFi.status() != WL_CONNECTED) 
  {
      Serial.println("WiFi failed");

      // Wake up after xxx seconds
      esp_sleep_enable_timer_wakeup(sleepDurationUs());  //30ULL * 1000000ULL);
                                            //   esp_sleep_enable_timer_wakeup( sleep_us ); //30ULL * 1000000ULL);
      Serial.flush();
      esp_deep_sleep_start();
  }

  Serial.println(" OK");
}

void loadConfig()
{
  preferences.begin("config", true);

  hasStoredConfig =
    preferences.isKey("wifi_ssid") ||
    preferences.isKey("wifi_pass") ||
    preferences.isKey("weather_loc") ||
    preferences.isKey("ics_url") ||
    preferences.isKey("city") ||
    preferences.isKey("board_profile");

  config.wifiSsid = preferences.getString("wifi_ssid", "");
  config.wifiPass = preferences.getString("wifi_pass", "");
  config.weatherLocation = preferences.getString("weather_loc", "");
  if (!config.weatherLocation.length()) {
    // Settings saved by older firmware: separate city and country code fields.
    String city = preferences.getString("city", "");
    String country = preferences.getString("country", "");
    city.trim();
    country.trim();
    config.weatherLocation = country.length() == 2 ? city + ", " + country : city;
  }
  for (int i = 0; i < ICS_MAX_FEEDS; i++) {
    config.icsUrls[i] = preferences.getString(ICS_PREF_KEYS[i], "");
  }
  config.timezone = preferences.getString("timezone", DEFAULT_DEVICE_TZ);
  // Built from the release asset names, so a renamed asset doesn't leave old links in flash.
  config.otaVersionUrl = defaultOtaVersionUrl();
  config.otaFirmwareUrl = defaultOtaFirmwareUrl();
  config.boardProfile = preferences.getString("board_profile", "esp32-waveshare");
  config.otaEnabled = preferences.getBool("ota_enabled", false);
  config.sleepHours = preferences.getInt("sleep_hours", 12);
  config.epdCs = preferences.getInt("epd_cs", -1);
  config.epdDc = preferences.getInt("epd_dc", -1);
  config.epdRst = preferences.getInt("epd_rst", -1);
  config.epdBusy = preferences.getInt("epd_busy", -1);
  config.epdSck = preferences.getInt("epd_sck", -1);
  config.epdMosi = preferences.getInt("epd_mosi", -1);
  config.pinDisplayPower = preferences.getInt("disp_pwr", -1);
  config.batPin = preferences.getInt("bat_pin", -1);

  preferences.end();

  applyBoardProfile(config.boardProfile, !hasStoredConfig || config.epdCs < 0);
  if (config.sleepHours != 6 && config.sleepHours != 12 &&
      config.sleepHours != 18 && config.sleepHours != 24) {
    config.sleepHours = 12;
  }
  if (!config.timezone.length()) {
    config.timezone = DEFAULT_DEVICE_TZ;
  }
}

bool isKnownBoardProfile(const String& profile)
{
  return profile == "esp32" || profile == "esp32c6" || profile == "esp32-waveshare" || profile == "custom";
}

void applyBoardProfile(const String& requestedProfile, bool overwritePins)
{
  String profile = isKnownBoardProfile(requestedProfile) ? requestedProfile : String("esp32-waveshare");
  config.boardProfile = profile;

  if (!overwritePins && profile == "custom") {
    return;
  }

  if (profile == "esp32c6") {
    config.epdCs = 1;
    config.epdDc = 8;
    config.epdRst = 14;
    config.epdBusy = 7;
    config.epdSck = 23;
    config.epdMosi = 22;
    config.pinDisplayPower = 4;
    config.batPin = 0;
    return;
  }

  // The dashboard project uses the same preset for esp32 and Waveshare.
  config.epdCs = 15;
  config.epdDc = 27;
  config.epdRst = 26;
  config.epdBusy = 25;
  config.epdSck = 13;
  config.epdMosi = 14;
  config.pinDisplayPower = 4;
  config.batPin = 35;
}

void initDisplayHardware()
{
  if (config.pinDisplayPower >= 0) {
    pinMode(config.pinDisplayPower, OUTPUT);
    digitalWrite(config.pinDisplayPower, HIGH);
  }

  SPI.begin(config.epdSck, -1, config.epdMosi, config.epdCs);

  if (displayHandle) {
    delete displayHandle;
    displayHandle = nullptr;
  }

  displayHandle = new DisplayType(
    GxEPD2_750c_GDEM075F52(config.epdCs, config.epdDc, config.epdRst, config.epdBusy)
  );

  display.init(115200, true, 2, false);
  display.setRotation(0);
}

uint64_t sleepDurationUs()
{
  return (uint64_t)config.sleepHours * 3600ULL * uS_TO_S_FACTOR;
}

// Once a day. The deep sleep timer runs on an imprecise RTC clock, so a wake up to an hour
// early still counts; otherwise a 24 h refresh interval would only check every other day.
#define OTA_CHECK_INTERVAL_S (23UL * 3600UL)

bool shouldCheckForOTA()
{
  if (!config.otaEnabled) return false;
  if (!timeSynced) return false;

  uint32_t now = (uint32_t)time(nullptr);
  if (lastOtaCheckEpoch != 0 && now >= lastOtaCheckEpoch &&
      (now - lastOtaCheckEpoch) < OTA_CHECK_INTERVAL_S) {
    return false;
  }

  return true;
}

// Release asset names: <board>-version.txt and <board>-ota.bin (see the GitHub workflow).
// The board comes from the chip the firmware was built for, not the pin profile.
#if CONFIG_IDF_TARGET_ESP32C6
#define OTA_BOARD "esp32c6"
#else
#define OTA_BOARD "esp32"
#endif

String defaultOtaVersionUrl()
{
  return String(OTA_REPO_BASE) + "/" OTA_BOARD "-version.txt";
}

String defaultOtaFirmwareUrl()
{
  return String(OTA_REPO_BASE) + "/" OTA_BOARD "-ota.bin";
}

bool hasIcsFeeds()
{
  for (int i = 0; i < ICS_MAX_FEEDS; i++) {
    if (config.icsUrls[i].length()) return true;
  }
  return false;
}

bool isConfigComplete()
{
  return
    hasStoredConfig &&
    config.wifiSsid.length() &&
    config.weatherLocation.length() &&
    hasIcsFeeds();
}

String htmlEscape(const String& input)
{
  String out;
  out.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else out += c;
  }
  return out;
}

String buildSsidOptions()
{
  String options;
  WiFi.scanDelete();
  int networkCount = WiFi.scanNetworks(false, true);
  for (int i = 0; i < networkCount && i < 24; ++i) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) {
      continue;
    }

    options += "<option value=\"";
    options += htmlEscape(ssid);
    options += "\"";
    if (ssid == config.wifiSsid) {
      options += " selected";
    }
    options += ">";
    options += htmlEscape(ssid);
    options += "</option>";
  }
  if (!options.length()) {
    options = "<option value=\"\">No WiFi networks found</option>";
  }
  return options;
}

String buildBoardProfileOptions()
{
  struct ProfileOption {
    const char* value;
    const char* label;
  };

  const ProfileOption profiles[] = {
    {"esp32-waveshare", "ESP32 Waveshare"},
    {"esp32", "ESP32"},
    {"esp32c6", "ESP32-C6"},
    {"custom", "Custom"}
  };

  String options;
  for (const auto& profile : profiles) {
    options += "<option value=\"";
    options += profile.value;
    options += "\"";
    if (config.boardProfile == profile.value) {
      options += " selected";
    }
    options += ">";
    options += profile.label;
    options += "</option>";
  }
  return options;
}

String buildSleepOptions()
{
  const int values[] = {6, 12, 18, 24};
  String options;
  for (int value : values) {
    options += "<option value=\"";
    options += String(value);
    options += "\"";
    if (config.sleepHours == value) {
      options += " selected";
    }
    options += ">";
    options += String(value);
    options += " hours</option>";
  }
  return options;
}

String buildConfigPage()
{
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>myCal Setup</title>
<style>
body{font-family:Arial,Helvetica,sans-serif;background:#f5f1e8;margin:0;color:#222}
.wrap{max-width:560px;margin:24px auto;padding:20px}
.card{background:#fff;border:2px solid #222;border-radius:16px;padding:22px;box-shadow:8px 8px 0 #f0d84c}
h1{margin:0 0 8px;font-size:28px}
p{line-height:1.45}
label{display:block;font-weight:bold;margin:14px 0 6px}
input,select{width:100%;padding:12px;border:1px solid #999;border-radius:10px;box-sizing:border-box}
button{width:100%;padding:14px;margin-top:18px;border:0;border-radius:12px;background:#222;color:#fff;font-size:16px;font-weight:bold}
.note{background:#fff8d8;padding:10px 12px;border-radius:10px;margin:12px 0}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
</style>
<script>
const profiles = {
  "esp32-waveshare": {epd_cs:15,epd_dc:27,epd_rst:26,epd_busy:25,epd_sck:13,epd_mosi:14,disp_pwr:4,bat_pin:35},
  "esp32": {epd_cs:15,epd_dc:27,epd_rst:26,epd_busy:25,epd_sck:13,epd_mosi:14,disp_pwr:4,bat_pin:35},
  "esp32c6": {epd_cs:1,epd_dc:8,epd_rst:14,epd_busy:7,epd_sck:23,epd_mosi:22,disp_pwr:4,bat_pin:0}
};
function applyProfile() {
  const sel = document.getElementById("board_profile").value;
  if (!profiles[sel]) return;
  const pins = profiles[sel];
  Object.keys(pins).forEach((id) => {
    const field = document.getElementById(id);
    if (field) field.value = pins[id];
  });
}
</script>
</head>
<body>
<div class="wrap">
<div class="card">
<h1>myCal Setup</h1>
<p>Configure WiFi, weather, calendar, and optional OTA links for this display.</p>
<form action="/save" method="POST">
<label for="wifi_ssid">WiFi network</label>
<select id="wifi_ssid" name="wifi_ssid">
)rawliteral";

  html += buildSsidOptions();
  html += R"rawliteral(
</select>
<button type="button" onclick="location.reload()">Rescan WiFi</button>
<label for="wifi_pass">WiFi password</label>
<input id="wifi_pass" name="wifi_pass" type="password" placeholder=")rawliteral";
  html += config.wifiPass.length() ? "Leave empty to keep the saved password" : "WiFi password";
  html += R"rawliteral(">
<label for="sleep_hours">Refresh interval</label>
<select id="sleep_hours" name="sleep_hours">
)rawliteral";
  html += buildSleepOptions();
  html += R"rawliteral(
</select>
<label for="board_profile">Hardware profile</label>
<select id="board_profile" name="board_profile" onchange="applyProfile()">
)rawliteral";
  html += buildBoardProfileOptions();
  html += R"rawliteral(
</select>
<div class="grid">
<div><label for="epd_cs">EPD CS</label><input id="epd_cs" name="epd_cs" type="number" value=")rawliteral";
  html += String(config.epdCs);
  html += R"rawliteral("></div>
<div><label for="epd_dc">EPD DC</label><input id="epd_dc" name="epd_dc" type="number" value=")rawliteral";
  html += String(config.epdDc);
  html += R"rawliteral("></div>
<div><label for="epd_rst">EPD RST</label><input id="epd_rst" name="epd_rst" type="number" value=")rawliteral";
  html += String(config.epdRst);
  html += R"rawliteral("></div>
<div><label for="epd_busy">EPD BUSY</label><input id="epd_busy" name="epd_busy" type="number" value=")rawliteral";
  html += String(config.epdBusy);
  html += R"rawliteral("></div>
<div><label for="epd_sck">EPD SCK</label><input id="epd_sck" name="epd_sck" type="number" value=")rawliteral";
  html += String(config.epdSck);
  html += R"rawliteral("></div>
<div><label for="epd_mosi">EPD MOSI</label><input id="epd_mosi" name="epd_mosi" type="number" value=")rawliteral";
  html += String(config.epdMosi);
  html += R"rawliteral("></div>
<div><label for="disp_pwr">Display power</label><input id="disp_pwr" name="disp_pwr" type="number" value=")rawliteral";
  html += String(config.pinDisplayPower);
  html += R"rawliteral("></div>
<div><label for="bat_pin">Battery ADC</label><input id="bat_pin" name="bat_pin" type="number" value=")rawliteral";
  html += String(config.batPin);
  html += R"rawliteral("></div>
</div>
<label>Calendar iCal URLs (up to 3, merged)</label>
)rawliteral";
  for (int i = 0; i < ICS_MAX_FEEDS; i++) {
    html += "<input name=\"";
    html += ICS_PREF_KEYS[i];
    html += "\" value=\"";
    html += htmlEscape(config.icsUrls[i]);
    html += "\" placeholder=\"https://calendar.google.com/calendar/ical/.../basic.ics\" style=\"margin-bottom:8px\">\n";
  }
  html += R"rawliteral(
<div class="note">Google Calendar: Settings &rarr; your calendar &rarr; Integrate calendar &rarr; public or secret address in iCal format. Any other .ics link (Outlook, iCloud, Nextcloud) works too.</div>
<label for="weather_location">Weather location</label>
<input id="weather_location" name="weather_location" value=")rawliteral";
  html += htmlEscape(config.weatherLocation);
  html += R"rawliteral(" placeholder="London, GB   or   51.51,-0.13">
<div class="note">City name (optionally followed by a 2-letter country code) or latitude,longitude. Forecast from Open-Meteo, no API key needed.</div>
<label for="timezone">Timezone (POSIX TZ string)</label>
<input id="timezone" name="timezone" value=")rawliteral";
  html += htmlEscape(config.timezone);
  html += R"rawliteral(" placeholder="EST5EDT,M3.2.0/2,M11.1.0/2">
<div class="note">Examples: EST5EDT,M3.2.0/2,M11.1.0/2 (US Eastern), CET-1CEST,M3.5.0,M10.5.0/3 (Central Europe), GMT0BST,M3.5.0/1,M10.5.0 (UK).</div>
<label><input type="checkbox" name="ota_enabled" )rawliteral";
  html += config.otaEnabled ? "checked" : "";
  html += R"rawliteral(> Check GitHub updates once a day from VoIPshare/ESP32_ePaper_GoogleCalendar</label>
<button type="submit">Save configuration</button>
</form>
</div>
</div>
</body>
</html>
)rawliteral";

  return html;
}

void handleConfigRoot()
{
  server.send(200, "text/html", buildConfigPage());
}

void handleConfigSave()
{
  config.wifiSsid = server.arg("wifi_ssid");
  // An empty password keeps the saved one, so reopening setup doesn't require retyping it.
  if (server.arg("wifi_pass").length() > 0) {
    config.wifiPass = server.arg("wifi_pass");
  }
  config.sleepHours = server.arg("sleep_hours").toInt();
  config.boardProfile = server.arg("board_profile");
  for (int i = 0; i < ICS_MAX_FEEDS; i++) {
    config.icsUrls[i] = server.arg(ICS_PREF_KEYS[i]);
    config.icsUrls[i].trim();
  }
  config.weatherLocation = server.arg("weather_location");
  config.weatherLocation.trim();
  config.timezone = server.arg("timezone");
  config.timezone.trim();
  config.otaEnabled = server.hasArg("ota_enabled");
  config.epdCs = server.arg("epd_cs").toInt();
  config.epdDc = server.arg("epd_dc").toInt();
  config.epdRst = server.arg("epd_rst").toInt();
  config.epdBusy = server.arg("epd_busy").toInt();
  config.epdSck = server.arg("epd_sck").toInt();
  config.epdMosi = server.arg("epd_mosi").toInt();
  config.pinDisplayPower = server.arg("disp_pwr").toInt();
  config.batPin = server.arg("bat_pin").toInt();

  if (config.boardProfile != "custom") {
    applyBoardProfile(config.boardProfile, true);
  }
  if (!config.timezone.length()) {
    config.timezone = DEFAULT_DEVICE_TZ;
  }

  preferences.begin("config", false);
  preferences.putString("wifi_ssid", config.wifiSsid);
  preferences.putString("wifi_pass", config.wifiPass);
  preferences.putInt("sleep_hours", config.sleepHours);
  preferences.putString("board_profile", config.boardProfile);
  for (int i = 0; i < ICS_MAX_FEEDS; i++) {
    preferences.putString(ICS_PREF_KEYS[i], config.icsUrls[i]);
  }
  preferences.putString("weather_loc", config.weatherLocation);
  preferences.putString("timezone", config.timezone);
  // Settings of older firmware, replaced by the iCal links and weather_loc.
  preferences.remove("google_id");
  preferences.remove("city");
  preferences.remove("country");
  preferences.putBool("ota_enabled", config.otaEnabled);
  preferences.remove("ota_ver");
  preferences.remove("ota_fw");
  preferences.putInt("epd_cs", config.epdCs);
  preferences.putInt("epd_dc", config.epdDc);
  preferences.putInt("epd_rst", config.epdRst);
  preferences.putInt("epd_busy", config.epdBusy);
  preferences.putInt("epd_sck", config.epdSck);
  preferences.putInt("epd_mosi", config.epdMosi);
  preferences.putInt("disp_pwr", config.pinDisplayPower);
  preferences.putInt("bat_pin", config.batPin);
  preferences.end();

  server.send(200, "text/html",
    "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>Saved</title></head><body style=\"font-family:Arial,Helvetica,sans-serif;display:flex;justify-content:center;align-items:center;height:100vh\">"
    "<div><h2>Configuration saved</h2><p>Restarting the device...</p></div></body></html>");
  delay(1200);
  ESP.restart();
}

void drawConfigNeededScreen(const char* apName, const IPAddress& ip)
{
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawTopBar();
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&Geologica_Bold14pt8b);
    drawCenteredText(0, 90, SCREEN_W, 40, "Configuration required", &Geologica_Bold14pt8b, GxEPD_RED);
    drawCenteredText(0, 150, SCREEN_W, 40, "Connect to WiFi:", &Geologica_Bold14pt8b, GxEPD_BLACK);
    drawCenteredText(0, 190, SCREEN_W, 40, apName, &Geologica_Bold14pt8b, GxEPD_BLACK);
    drawCenteredText(0, 250, SCREEN_W, 40, "Open in your browser:", &Geologica_Bold14pt8b, GxEPD_BLACK);

    char ipBuf[32];
    snprintf(ipBuf, sizeof(ipBuf), "http://%s", ip.toString().c_str());
    drawCenteredText(0, 290, SCREEN_W, 40, ipBuf, &Geologica_Bold14pt8b, GxEPD_RED);
    drawCenteredText(0, 360, SCREEN_W, 40, "Save the form to start the dashboard.", &Geologica_Bold14pt8b, GxEPD_BLACK);
  } while (display.nextPage());
}

void startConfigPortal()
{
  Serial.println("Starting configuration portal");
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false, true);
  bool started = WiFi.softAP(AP_NAME);
  Serial.println(started ? "AP started" : "AP failed");

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(ip);

  // Captive portal: answer every DNS lookup with our IP so phones/laptops pop up the setup page.
  dnsServer.start(53, "*", ip);

  server.on("/", handleConfigRoot);
  server.on("/save", HTTP_POST, handleConfigSave);
  // OS connectivity checks (Android /generate_204, Apple /hotspot-detect.html, Windows /connecttest.txt)
  // and any other URL get redirected to the setup page.
  server.onNotFound([]() {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  drawConfigNeededScreen(AP_NAME, ip);

  while (true) {
    dnsServer.processNextRequest();
    server.handleClient();
    delay(10);
  }
}

// ===================
// TIME (NTP)
// ===================
void initTime()
{
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setConfiguredTimezone();

  struct tm timeinfo;
  timeSynced = getLocalTime(&timeinfo, 10000);
  Serial.println(timeSynced ? "Time synced" : "Time sync failed");
}

// ===================
// DRAW UI
// ===================
void drawUI()
{
  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    drawTopBar();
    drawCalendar();
    drawForecast(2, 5);
    drawStatus();
  } while (display.nextPage());
}

// ===================
// TOP BAR
// ===================
void drawTopBar()
{
  display.fillRect(0, 0, SCREEN_W, TOP_H, GxEPD_YELLOW);
}

// ===================
// CALENDAR (LEFT)
// ===================
// ===================
// CALENDAR (LEFT, EXPANDED)
// ===================
const int CAL_W = 400;  // width of calendar
const int CAL_H = BODY_H; // use full body height
const int CAL_X = 0;     // start at left
const int CAL_Y = TOP_H;

void setConfiguredTimezone() {
  setenv("TZ", config.timezone.length() ? config.timezone.c_str() : DEFAULT_DEVICE_TZ, 1);
  tzset();
}


bool hasEventOnDay(int day) {
  for (int i = 0; i < cal.daysCount; i++) {
    if (cal.daysWithEvents[i] == day) return true;
  }
  return false;
}

// "14:00-15:30", "All day"; events starting on a later day get a "Tmrw" prefix.
String formatEventTime(const CalendarEvent& ev, const struct tm& today)
{
  struct tm s, e;
  localtime_r(&ev.start, &s);
  localtime_r(&ev.end, &e);
  bool later = s.tm_year > today.tm_year ||
               (s.tm_year == today.tm_year && s.tm_yday > today.tm_yday);

  char buf[32];
  if (ev.allDay) {
    snprintf(buf, sizeof(buf), "%s", later ? "Tmrw" : "All day");
  } else {
    snprintf(buf, sizeof(buf), "%s%02d:%02d-%02d:%02d",
             later ? "Tmrw " : "", s.tm_hour, s.tm_min, e.tm_hour, e.tm_min);
  }
  return String(buf);
}

void drawCalendar()
{
  struct tm timeinfo;

  int16_t tbx, tby;
  uint16_t tbw, tbh;
  String txt;

  if (!getLocalTime(&timeinfo, 1000)) {
    display.setFont(&Geologica_Bold14pt8b);
    display.setTextColor(GxEPD_RED);
    display.setCursor(CAL_X + 20, CAL_Y + 60);
    display.print("Time unavailable");
    display.setTextColor(GxEPD_BLACK);
    return;
  }

  int year  = timeinfo.tm_year + 1900;
  int month = timeinfo.tm_mon + 1;

  int y = CAL_Y + 50;

  // -----------------------------
  // Days of week header
  // -----------------------------
  const char* days[] = {"Mo","Tu","We","Th","Fr","Sa","Su"};
  int colWidth = CAL_W / 7;

  display.setTextColor(GxEPD_RED);
  display.setFont(&Bangers_Regular18pt7b);

  for (int i = 0; i < 7; i++) {
    display.setCursor(CAL_X + i * colWidth + 10, y);
    display.print(days[i]);
  }

  display.setTextColor(GxEPD_BLACK);
  y += 50;

  // -----------------------------
  // First weekday of month
  // -----------------------------
  struct tm firstDay = {0};
  firstDay.tm_year = year - 1900;
  firstDay.tm_mon  = month - 1;
  firstDay.tm_mday = 1;
  mktime(&firstDay);

  int firstWeekday = firstDay.tm_wday; // Sunday = 0
  int col = (firstWeekday == 0) ? 6 : firstWeekday - 1;
  int row = 0;

  // -----------------------------
  // Days in month
  // -----------------------------
  int monthDays = daysInMonth(year, month);

  int rowHeight = 56;

  // -----------------------------
  // Draw calendar days
  // -----------------------------
  display.setFont(&Bangers_Regular18pt7b);

  for (int d = 1; d <= monthDays; d++) {
    int x = CAL_X + col * colWidth + 10;
    int yy = y + row * rowHeight;

    bool isToday = (d == timeinfo.tm_mday);
    bool hasEvent = hasEventOnDay(d);

    // Event day circle (outline)
    // Today circle (filled, on top)
    
    if (isToday) {
      display.drawCircle(x + 16, yy - 12, 27, GxEPD_BLACK);
      display.fillCircle(x + 16, yy - 12, 26, GxEPD_RED);
      display.setTextColor(GxEPD_WHITE);
    } else if (hasEvent) {
      display.drawCircle(x + 16, yy - 12, 27, GxEPD_BLACK);
      display.fillCircle(x + 16, yy - 12, 26, GxEPD_YELLOW);
      display.setTextColor(GxEPD_BLACK);
    }
    else {
      display.setTextColor(GxEPD_BLACK);
    }

  txt = String(d);

  // Get text bounds
  display.getTextBounds(txt, 0, 0, &tbx, &tby, &tbw, &tbh);

  // Circle center
  int cx = x + 16;
  int cy = yy - 12;

  // Compute centered cursor position
  int tx = cx - (tbw / 2);
  int ty = cy + (tbh / 2);  // baseline correction

  display.setCursor(tx, ty);
  display.print(txt);


    col++;
    if (col > 6) {
      col = 0;
      row++;
    }
  }

  // -----------------------------
  // Next 24h events list
  // -----------------------------
  // Time in red, title in black.
  int listX = CAL_X + CAL_W + 20;
  int listY = CAL_Y + 40;
  int maxWidth = SCREEN_W - listX - 15;
  int lineHeight = 30;
  int listBottom = SCREEN_H - 45;   // keep clear of the status line
  display.setFont(&Geologica_Bold14pt8b);

  if (!calendarOk) {
    display.setTextColor(GxEPD_RED);
    display.setCursor(listX, listY);
    display.print("Calendar unavailable");
  } else if (cal.eventCount == 0) {
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(listX, listY);
    display.print("No events in the next 24h");
  } else {
    int shown = 0;
    for (int i = 0; i < cal.eventCount && listY <= listBottom; i++, shown++) {
      String when = formatEventTime(cal.events[i], timeinfo);

      display.setTextColor(GxEPD_RED);
      display.setCursor(listX, listY);
      display.print(when);
      // Text bounds drop trailing spaces, so use where the cursor ended up, plus a gap.
      int titleIndent = display.getCursorX() - listX + 12;

      display.setTextColor(GxEPD_BLACK);
      listY = drawWrappedText(
        listX,
        listY,
        maxWidth,
        utf8ToLatin1(String(cal.events[i].title)),
        lineHeight,
        titleIndent,
        2
      );

      listY += lineHeight / 2;
    }

    if (shown < cal.eventCount) {
      char more[24];
      snprintf(more, sizeof(more), "+%d more", cal.eventCount - shown);
      display.setTextColor(GxEPD_RED);
      display.setCursor(listX, min(listY, SCREEN_H - 30));
      display.print(more);
    }
  }
  display.setTextColor(GxEPD_BLACK);
}

void drawForecast(int x, int y) {
    const int blockWidth = 180;

    display.setFont(&Geologica_Bold14pt8b);

    if (!weatherOk) {
        display.setTextColor(GxEPD_RED);
        display.setCursor(x, y + 32);
        display.print("Weather unavailable");
        display.setTextColor(GxEPD_BLACK);
        return;
    }

    for (int i = 0; i < FORECAST_DAYS; ++i) {
        int blockX = x + (i * blockWidth);

        if (forecast[i].valid) {
            drawIcon(
                blockX, y,
                48, 48,
                getWeatherIcon(forecast[i].code),
                getWeatherIconColor(forecast[i].code)
            );

            String tempText = utf8ToLatin1(
                String((int)lroundf(forecast[i].minTemp)) + "° / " + String((int)lroundf(forecast[i].maxTemp)) + "°"
            );
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(blockX + 55, y + 30);
            display.print(tempText);
        } else {
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(blockX, y + 30);
            display.print("No forecast");
        }
    }
}


// Compares "1.2.3" style versions. A pre-release suffix ("0.2.0-beta.1") sorts below the
// same version without it, so a beta tester moves on to the stable release when it is out.
// Returns -1, 0 or 1.
int compareVersions(const String& v1, const String& v2)
{
  int dash1 = v1.indexOf('-');
  int dash2 = v2.indexOf('-');
  String core1 = dash1 >= 0 ? v1.substring(0, dash1) : v1;
  String core2 = dash2 >= 0 ? v2.substring(0, dash2) : v2;

  int i1 = 0, i2 = 0;
  while (i1 < core1.length() || i2 < core2.length()) {
    long num1 = 0;
    long num2 = 0;

    while (i1 < core1.length() && core1[i1] != '.') {
      if (isDigit(core1[i1])) num1 = num1 * 10 + (core1[i1] - '0');
      i1++;
    }
    while (i2 < core2.length() && core2[i2] != '.') {
      if (isDigit(core2[i2])) num2 = num2 * 10 + (core2[i2] - '0');
      i2++;
    }

    if (num1 < num2) return -1;
    if (num1 > num2) return 1;

    // Skip dots
    i1++;
    i2++;
  }

  if (dash1 < 0 && dash2 < 0) return 0;
  if (dash1 < 0) return 1;    // stable is newer than its pre-release
  if (dash2 < 0) return -1;
  int c = v1.substring(dash1 + 1).compareTo(v2.substring(dash2 + 1));
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

void checkForOTA() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!config.otaVersionUrl.length() || !config.otaFirmwareUrl.length()) return;

  // GitHub "releases/latest/download" links redirect to the asset of the latest stable
  // release; pre-releases (betas) are never "latest", so devices don't pick them up.
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, config.otaVersionUrl);

  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("OTA version check: HTTP %d\n", httpCode);
    http.end();
    return;
  }

  String newVersion = http.getString();
  newVersion.trim();
  http.end();
  lastOtaCheckEpoch = (uint32_t)time(nullptr);

  if (!newVersion.length() || !isDigit(newVersion[0]) || newVersion.length() > 32) {
    Serial.println("OTA version check: unexpected reply");
    return;
  }

  // Only move forward: a beta build doesn't go back to an older stable release.
  if ( compareVersions(newVersion, FW_VERSION) <= 0 ) {
    Serial.println("Firmware is up to date");
    return;
  }

  Serial.printf("New firmware available: %s\n", newVersion.c_str());

  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  t_httpUpdate_return ret = httpUpdate.update(client, config.otaFirmwareUrl);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA failed: %s\n",
        httpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("No update available");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("Update successful, rebooting...");
      break;
  }
}

const uint8_t* getWeatherIcon(int code) {
    if (code == 0 || code == 1) return ICON_SUNNY_48;
    if (code == 2) return ICON_PARTLYCLOUDY_48;
    if (code == 3) return ICON_CLOUDY_48;
    if (code == 45 || code == 48) return ICON_FOG_48;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return ICON_RAINY_48;
    if (code >= 71 && code <= 77) return ICON_SNOW_48;
    if (code >= 85 && code <= 86) return ICON_SNOW_48;
    if (code >= 95) return ICON_RAINANDTHUNDER_48;
    return ICON_SUNNY_48;
}

// The forecast sits on the yellow top bar, so the sun is drawn red to stay visible.
uint16_t getWeatherIconColor(int code) {
    if (code == 0 || code == 1) return GxEPD_RED;
    if (code >= 95) return GxEPD_RED;
    return GxEPD_BLACK;
}

void drawIcon(
    int x, int y,
    int w, int h,
    const uint8_t* icon,
    const uint16_t color
) {
    int bytesPerRow = (w + 7) / 8;
    for (int iy = 0; iy < h; iy++) {
        for (int ix = 0; ix < w; ix++) {
            int byteIndex = iy * bytesPerRow + (ix >> 3);
            uint8_t byte = pgm_read_byte(&icon[byteIndex]);

            // MSB FIRST (most common!)
            if (byte & (0x80 >> (ix & 7))) {
                display.drawPixel(
                    x + ix,
                    y + iy,
                    color);
            }
        }
    }
}


uint16_t textWidth(const String &text)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

// Shortens `text` and appends "..." so it fits in `width` pixels.
String ellipsize(String text, int width)
{
  text.trim();
  while (text.length() && textWidth(text + "...") > width) {
    text.remove(text.length() - 1);
    text.trim();
  }
  return text + "...";
}

// Word-wraps `text` over at most `maxLines` lines; the last line ends with "..." if the
// text doesn't fit. The first line starts `firstIndent` pixels to the right of x (after a
// label on the same line). Returns the baseline below the last line drawn.
int drawWrappedText(
  int x,
  int y,
  int maxWidth,
  const String &text,
  int lineStep,   // baseline-to-baseline, e.g. 22
  int firstIndent,
  int maxLines
) {
  String line = "";
  int cursorY = y;
  int indent = firstIndent;
  int lines = 0;

  auto printLine = [&](const String &s) {
    display.setCursor(x + indent, cursorY);
    display.print(s);
    cursorY += lineStep;
    indent = 0;
    lines++;
  };

  int i = 0;
  while (i < text.length()) {

    // get next word
    int start = i;
    while (i < text.length() && text[i] != ' ') i++;
    String word = text.substring(start, i);
    i++; // skip space
    if (!word.length()) continue;

    String candidate = line.length() ? line + " " + word : word;
    if (textWidth(candidate) <= maxWidth - indent) {
      line = candidate;
      continue;
    }

    if (lines >= maxLines - 1) {
      // last allowed line: cut the rest of the text here
      printLine(ellipsize(line + " " + text.substring(start), maxWidth - indent));
      return cursorY;
    }

    if (line.length()) {
      printLine(line);
      line = word;
    } else if (indent > 0) {
      // not even the first word fits after the label: start below it
      cursorY += lineStep;
      indent = 0;
      lines++;
      line = word;
    } else {
      line = candidate;   // a single word wider than the line
    }
  }

  // print last line
  if (line.length()) {
    printLine(textWidth(line) > maxWidth - indent ? ellipsize(line, maxWidth - indent) : line);
  } else if (lines == 0) {
    cursorY += lineStep;
  }

  return cursorY;
}

String utf8ToLatin1(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (c < 128) {
        // ASCII
        out += char(c);
    } else if (c == 0xC3 && i + 1 < s.length()) {
        // Latin-1 accented letters (Á, é, ô…)
        uint8_t next = s[++i];
        out += char(next + 64); // maps é à ô etc.
    } else if (c == 0xC2 && i + 1 < s.length()) {
        // Other Latin-1 symbols (like °)
        uint8_t next = s[++i];
        out += char(next);
    } else {
        // unknown or unsupported, just skip or replace with ?
        i++; // skip next byte
        out += '?';
    }
  }
  return out;
}

float readBatteryVoltageAvg(int samples = 20) {
    if (config.batPin < 0) {
        return 0.0f;
    }
    analogReadResolution(12);  // 0–4095
    analogSetPinAttenuation(config.batPin, ADC_11db);

    long sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(config.batPin);
        delay(5);
    }
    float raw = sum / (float)samples;
    return (raw / 4095.0) * 3.3 * 2.0;
}

void drawCenteredText(
  int16_t x, int16_t y,
  int16_t w, int16_t h,
  const char* text,
  const GFXfont* font,
  uint16_t color
){
  display.setFont(font);
  display.setTextColor(color);

  int16_t tbx, tby;
  uint16_t tbw, tbh;

  display.getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);

  int16_t cx = x + (w - tbw) / 2;
  int16_t cy = y + (h + tbh) / 2;  // baseline-aware

  display.setCursor(cx, cy);
  display.print(text);
}

void drawStatus()
{
  display.setFont(&RobotoMono_Thin7pt7b);
  display.setTextColor(GxEPD_BLACK);
  float vbat = readBatteryVoltageAvg(6);

   // float temperature = 23.56;
  char buf[34];

  snprintf(buf, sizeof(buf), "Bat: %.2f V", vbat);

  display.setCursor(700,460);
  display.print( buf );

  snprintf(buf, sizeof(buf), "V: %s",FW_VERSION);
  display.setCursor(5,460);
  display.print( buf );

  if (calendarOk && calendarFailedFeeds > 0) {
    snprintf(buf, sizeof(buf), "%d calendar feed%s failed", calendarFailedFeeds, calendarFailedFeeds > 1 ? "s" : "");
    display.setTextColor(GxEPD_RED);
    display.setCursor(90,460);
    display.print( buf );
    display.setTextColor(GxEPD_BLACK);
  }

  struct tm t;
  setConfiguredTimezone();
  if (getLocalTime(&t)) {
        snprintf(buf, sizeof(buf),
           "Last: %04d-%02d-%02d %02d:%02d",
           t.tm_year + 1900,
           t.tm_mon + 1,
           t.tm_mday,
           t.tm_hour,
           t.tm_min);
           display.setCursor(300,460);
           display.print( buf );
  }
}
