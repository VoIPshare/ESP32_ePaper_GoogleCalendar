#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <WebServer.h>
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
#define FW_VERSION "0.1.1"
#endif
#define uS_TO_S_FACTOR 1000000ULL

// ===================
// DISPLAY (RAM-SAFE BUFFER)
// ===================
using DisplayType = GxEPD2_4C<GxEPD2_750c_GDEM075F52, GxEPD2_750c_GDEM075F52::HEIGHT / 2>;
DisplayType* displayHandle = nullptr;
#define display (*displayHandle)


#define MAX_DAYS   31
#define MAX_EVENTS 10

int daysWithEvents[MAX_DAYS];
int daysCount = 0;

CalendarEvent events[MAX_EVENTS];
int eventCount = 0;
bool timeSynced = false;
DeviceConfig config;
bool hasStoredConfig = false;
WebServer server(80);
Preferences preferences;
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

  char calendarURL[256];
  snprintf(calendarURL, sizeof(calendarURL), "https://script.google.com/macros/s/%s/exec", config.googleScriptId.c_str());

  if (fetchCalendarData(
        calendarURL,
        daysWithEvents,
        daysCount,
        events,
        eventCount)) {
  }

  drawUI();

  // Optional: turn WiFi off to save RAM + power
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

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
    preferences.isKey("city") ||
    preferences.isKey("country") ||
    preferences.isKey("google_id") ||
    preferences.isKey("board_profile");

  config.wifiSsid = preferences.getString("wifi_ssid", "");
  config.wifiPass = preferences.getString("wifi_pass", "");
  config.city = preferences.getString("city", "");
  config.country = preferences.getString("country", "");
  config.timezone = preferences.getString("timezone", DEFAULT_DEVICE_TZ);
  config.googleScriptId = preferences.getString("google_id", "");
  config.otaVersionUrl = preferences.getString("ota_ver", "");
  config.otaFirmwareUrl = preferences.getString("ota_fw", "");
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

bool shouldCheckForOTA()
{
  if (!config.otaEnabled) return false;
  if (!timeSynced) return false;

  uint32_t now = (uint32_t)time(nullptr);
  if (lastOtaCheckEpoch != 0 && now >= lastOtaCheckEpoch &&
      (now - lastOtaCheckEpoch) < (12UL * 3600UL)) {
    return false;
  }

  return true;
}

String defaultOtaVersionUrl()
{
  if (config.boardProfile == "esp32c6") {
    return String(OTA_REPO_BASE) + "/version-esp32c6.txt";
  }
  return String(OTA_REPO_BASE) + "/version-esp32.txt";
}

String defaultOtaFirmwareUrl()
{
  if (config.boardProfile == "esp32c6") {
    return String(OTA_REPO_BASE) + "/ESP32_ePaper_GoogleCalendar-esp32c6.bin";
  }
  return String(OTA_REPO_BASE) + "/ESP32_ePaper_GoogleCalendar-esp32.bin";
}

bool isConfigComplete()
{
  return
    hasStoredConfig &&
    config.wifiSsid.length() &&
    config.wifiPass.length() &&
    config.city.length() &&
    config.country.length() &&
    config.googleScriptId.length();
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

String urlEncode(const String& input)
{
  String out;
  out.reserve(input.length() * 3);
  for (size_t i = 0; i < input.length(); ++i) {
    uint8_t c = input[i];
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += char(c);
      continue;
    }

    char encoded[4];
    snprintf(encoded, sizeof(encoded), "%%%02X", c);
    out += encoded;
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
<input id="wifi_pass" name="wifi_pass" value=")rawliteral";
  html += htmlEscape(config.wifiPass);
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
<label for="google_id">Google Script ID</label>
<input id="google_id" name="google_id" value=")rawliteral";
  html += htmlEscape(config.googleScriptId);
  html += R"rawliteral(">
<label for="city">City for Open-Meteo geocoding</label>
<input id="city" name="city" value=")rawliteral";
  html += htmlEscape(config.city);
  html += R"rawliteral(">
<label for="country">Country code</label>
<input id="country" name="country" value=")rawliteral";
  html += htmlEscape(config.country);
  html += R"rawliteral(">
<label for="timezone">Timezone (POSIX TZ string)</label>
<input id="timezone" name="timezone" value=")rawliteral";
  html += htmlEscape(config.timezone);
  html += R"rawliteral(">
<div class="note">Example: EST5EDT,M3.2.0/2,M11.1.0/2</div>
<label><input type="checkbox" name="ota_enabled" )rawliteral";
  html += config.otaEnabled ? "checked" : "";
  html += R"rawliteral(> Check GitHub updates every 12 hours from VoIPshare/ESP32_ePaper_GoogleCalendar</label>
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
  config.wifiPass = server.arg("wifi_pass");
  config.sleepHours = server.arg("sleep_hours").toInt();
  config.boardProfile = server.arg("board_profile");
  config.googleScriptId = server.arg("google_id");
  config.city = server.arg("city");
  config.country = server.arg("country");
  config.timezone = server.arg("timezone");
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

  config.otaVersionUrl = defaultOtaVersionUrl();
  config.otaFirmwareUrl = defaultOtaFirmwareUrl();

  preferences.begin("config", false);
  preferences.putString("wifi_ssid", config.wifiSsid);
  preferences.putString("wifi_pass", config.wifiPass);
  preferences.putInt("sleep_hours", config.sleepHours);
  preferences.putString("board_profile", config.boardProfile);
  preferences.putString("google_id", config.googleScriptId);
  preferences.putString("city", config.city);
  preferences.putString("country", config.country);
  preferences.putString("timezone", config.timezone);
  preferences.putBool("ota_enabled", config.otaEnabled);
  preferences.putString("ota_ver", config.otaVersionUrl);
  preferences.putString("ota_fw", config.otaFirmwareUrl);
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

  server.on("/", handleConfigRoot);
  server.on("/save", HTTP_POST, handleConfigSave);
  server.begin();

  drawConfigNeededScreen(AP_NAME, ip);

  while (true) {
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
    drawForecast(2, 5, GxEPD_BLACK);  // Example color
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
  for (int i = 0; i < daysCount; i++) {
    if (daysWithEvents[i] == day) return true;
  }
  return false;
}

bool parseIsoUtc(const String& iso, time_t& outEpoch) {
  int year, month, day, hour, minute, second;
  second = 0;

  int matched = sscanf(
    iso.c_str(),
    "%d-%d-%dT%d:%d:%d",
    &year, &month, &day, &hour, &minute, &second
  );

  if (matched < 5) {
    return false;
  }

  struct tm tmUtc = {};
  tmUtc.tm_year = year - 1900;
  tmUtc.tm_mon = month - 1;
  tmUtc.tm_mday = day;
  tmUtc.tm_hour = hour;
  tmUtc.tm_min = minute;
  tmUtc.tm_sec = second;

  char* previousTz = getenv("TZ");
  String previousTzValue = previousTz ? String(previousTz) : String("");

  setenv("TZ", "UTC0", 1);
  tzset();
  outEpoch = mktime(&tmUtc);

  if (previousTzValue.length()) {
    setenv("TZ", previousTzValue.c_str(), 1);
  } else {
    unsetenv("TZ");
  }
  tzset();

  return outEpoch != (time_t)-1;
}

String formatEventDateTimeLocal(const String &iso, bool includeEnd=false, const String &endIso="") {
  time_t startEpoch;
  if (!parseIsoUtc(iso, startEpoch)) {
    return includeEnd ? String("--:--") : String("-- --:--");
  }

  struct tm startLocal;
  localtime_r(&startEpoch, &startLocal);

  char buf[32];
  if (!includeEnd) {
    snprintf(
      buf,
      sizeof(buf),
      "%02d %02d:%02d",
      startLocal.tm_mday,
      startLocal.tm_hour,
      startLocal.tm_min
    );
  } else {
    time_t endEpoch;
    if (!parseIsoUtc(endIso, endEpoch)) {
      snprintf(buf, sizeof(buf), "%02d:%02d", startLocal.tm_hour, startLocal.tm_min);
      return String(buf);
    }

    struct tm endLocal;
    localtime_r(&endEpoch, &endLocal);
    snprintf(
      buf,
      sizeof(buf),
      "%02d:%02d-%02d:%02d",
      startLocal.tm_hour,
      startLocal.tm_min,
      endLocal.tm_hour,
      endLocal.tm_min
    );
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
  int daysInMonth = 31;
  if (month == 2) daysInMonth = (year % 4 == 0) ? 29 : 28;
  if (month == 4 || month == 6 || month == 9 || month == 11) daysInMonth = 30;

  int rowHeight = 56;

  // -----------------------------
  // Draw calendar days
  // -----------------------------
  display.setFont(&Bangers_Regular18pt7b);

  for (int d = 1; d <= daysInMonth; d++) {
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
  int listX = CAL_X + CAL_W + 20;
  int listY = CAL_Y + 40;
  int maxWidth = 380;
  int lineHeight = 30;
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&Geologica_Bold14pt8b);

  if (eventCount == 0) {
    display.setCursor(listX, listY);
    display.print("No events in the next 24h");
  } else {
    for (int i = 0; i < eventCount && i < 5; i++) {
      String when = events[i].allDay
        ? String("All day")
        : formatEventDateTimeLocal(events[i].start, true, events[i].end);

      String line = utf8ToLatin1(when + " " + events[i].title);
      listY = drawWrappedText(
        listX,
        listY,
        maxWidth,
        line,
        lineHeight
      );

      listY += lineHeight / 2;
    }
  }
}

// // ===================
// // EVENTS (RIGHT)
// // ===================


bool fetchCalendarData(
  const char* url,
  int daysWithEvents[],
  int &daysCount,
  CalendarEvent events[],
  int &eventCount
) {
  HTTPClient http;
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }
  Serial.println("READING");
  String payload = http.getString();
  // Serial.println( payload );
  http.end();

  // Adjust size if needed
  StaticJsonDocument<4096> doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.println("JSON parse failed");
    return false;
  }

  // -----------------------------
  // Days with events
  // -----------------------------
  daysCount = 0;
  JsonArray days = doc["daysWithEvents"];
  for (int d : days) {
    if (daysCount >= MAX_DAYS) {
      break;
    }
    daysWithEvents[daysCount++] = d;
  }

  // -----------------------------
  // Events next 24h
  // -----------------------------
  eventCount = 0;
  JsonArray evts = doc["next24hEvents"];
  for (JsonObject e : evts) {
    if (eventCount >= MAX_EVENTS) {
      break;
    }
    events[eventCount].title = e["title"].as<String>();
    events[eventCount].start = e["start"].as<String>();
    events[eventCount].end   = e["end"].as<String>();
    events[eventCount].allDay = e["allDay"];
    eventCount++;
  }
  return true;
}

void drawForecast(int x, int y, uint16_t color) {
    DayForecast today, tomorrow, dayAfterTomorrow;
    // const char* labels[] = {"Today", "Tomorrow", "+2"};
    DayForecast* forecasts[] = {&today, &tomorrow, &dayAfterTomorrow};
    const int blockWidth = 180;

    display.setFont(&Geologica_Bold14pt8b);

    if (!fetchForecast(today, tomorrow, dayAfterTomorrow)) {
        display.setTextColor(GxEPD_RED);
        display.setCursor(x, y + 32);
        display.print("Weather unavailable");
        display.setTextColor(GxEPD_BLACK);
        return;
    }

    for (int i = 0; i < 3; ++i) {
        int blockX = x + (i * blockWidth);
        // display.setCursor(blockX, y + 12);
        // display.print(labels[i]);

        if (forecasts[i]->valid) {
            drawIcon(
                blockX, y,
                48, 48,
                getWeatherIconFromOW(forecasts[i]->icon),
                getWeatherIconColor(forecasts[i]->icon)
            );

            String tempText = utf8ToLatin1(
                String((int)forecasts[i]->minTemp) + "° / " + String((int)forecasts[i]->maxTemp) + "°"
            );
            display.setCursor(blockX + 55, y + 30);
            display.print(tempText);
        } else {
            display.setCursor(blockX, y + 30);
            display.print("No forecast");
        }
    }
}


bool fetchForecast(DayForecast& today, DayForecast& tomorrow, DayForecast& dayAfterTomorrow) {
    bool status = false;
    float latitude = 0.0f;
    float longitude = 0.0f;
    if (!fetchCoordinates(latitude, longitude)) {
        return false;
    }

    char forecastURL[384];
    snprintf(
        forecastURL,
        sizeof(forecastURL),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&daily=temperature_2m_max,temperature_2m_min,weather_code&forecast_days=3&timezone=auto",
        latitude,
        longitude
    );

    WiFiClientSecure client;
    client.setInsecure(); // for testing only

    HTTPClient http;
    http.begin(client, forecastURL);

    int code = http.GET();
    Serial.print("HTTP code: "); Serial.println(code);

    if(code == 200) {
        DynamicJsonDocument doc(12000);
        String payload = http.getString();
        DeserializationError err = deserializeJson(doc, payload);
        
        if(err) {
            Serial.print("JSON parse failed: "); Serial.println(err.c_str());
            Serial.println(payload);
        } else {
            JsonArray mins = doc["daily"]["temperature_2m_min"].as<JsonArray>();
            JsonArray maxs = doc["daily"]["temperature_2m_max"].as<JsonArray>();
            JsonArray codes = doc["daily"]["weather_code"].as<JsonArray>();

            if (mins.size() > 0 && maxs.size() > 0 && codes.size() > 0) {
                today.minTemp = mins[0].as<float>();
                today.maxTemp = maxs[0].as<float>();
                today.icon = String(codes[0].as<int>());
                today.valid = true;
                status = true;
            }

            if (mins.size() > 1 && maxs.size() > 1 && codes.size() > 1) {
                tomorrow.minTemp = mins[1].as<float>();
                tomorrow.maxTemp = maxs[1].as<float>();
                tomorrow.icon = String(codes[1].as<int>());
                tomorrow.valid = true;
            }

            if (mins.size() > 2 && maxs.size() > 2 && codes.size() > 2) {
                dayAfterTomorrow.minTemp = mins[2].as<float>();
                dayAfterTomorrow.maxTemp = maxs[2].as<float>();
                dayAfterTomorrow.icon = String(codes[2].as<int>());
                dayAfterTomorrow.valid = true;
            }
        }
    } else {
        Serial.println("HTTP GET failed");
    }
    http.end();

    return status;
}

bool fetchCoordinates(float& latitude, float& longitude) {
    String city = config.city;
    city.trim();
    city = urlEncode(city);

    String country = config.country;
    country.trim();
    country = urlEncode(country);

    char url[384];
    snprintf(
        url,
        sizeof(url),
        "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json%s%s",
        city.c_str(),
        country.length() ? "&countryCode=" : "",
        country.length() ? country.c_str() : ""
    );

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("Geocoding HTTP error: %d\n", code);
        http.end();
        return false;
    }

    DynamicJsonDocument doc(12000);
    String payload = http.getString();
    DeserializationError err = deserializeJson(doc, payload);
    http.end();
    if (err) {
        Serial.print("Geocoding JSON parse failed: ");
        Serial.println(err.c_str());
        Serial.println(payload);
        return false;
    }

    JsonArray results = doc["results"].as<JsonArray>();
    if (results.isNull() || results.size() == 0) {
        Serial.println("No geocoding results");
        return false;
    }

    latitude = results[0]["latitude"].as<float>();
    longitude = results[0]["longitude"].as<float>();
    return true;
}


int compareVersions(const String& v1, const String& v2)
{
  int i1 = 0, i2 = 0;

  while (i1 < v1.length() || i2 < v2.length()) {

    long num1 = 0;
    long num2 = 0;

    // Parse number from v1
    while (i1 < v1.length() && v1[i1] != '.') {
      if (isDigit(v1[i1]))
        num1 = num1 * 10 + (v1[i1] - '0');
      i1++;
    }

    // Parse number from v2
    while (i2 < v2.length() && v2[i2] != '.') {
      if (isDigit(v2[i2]))
        num2 = num2 * 10 + (v2[i2] - '0');
      i2++;
    }

    if (num1 < num2) return -1;
    if (num1 > num2) return 1;

    // Skip dots
    i1++;
    i2++;
  }

  return 0; // equal
}

void checkForOTA() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!config.otaVersionUrl.length() || !config.otaFirmwareUrl.length()) return;

  HTTPClient http;
  http.begin(config.otaVersionUrl);

  int httpCode = http.GET();
  if (httpCode != 200) {
    http.end();
    return;
  }

  String newVersion = http.getString();
  newVersion.trim();
  http.end();
  lastOtaCheckEpoch = (uint32_t)time(nullptr);

  if ( compareVersions(newVersion, FW_VERSION) == 0 ) {
    Serial.println("Firmware is up to date");
    return;
  }

  Serial.printf("New firmware available: %s\n", newVersion.c_str());

    WiFiClientSecure client;
    client.setInsecure(); 

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

const uint8_t*  getWeatherIconFromOW(const String& icon) {
  Serial.println("getWeatherIconFromOW");
  Serial.println( icon );
    int code = icon.toInt();
    if (code == 0 || code == 1) return ICON_SUNNY_48;
    if (code == 2) return ICON_PARTLYCLOUDY_48;
    if (code == 3) return ICON_CLOUDY_48;
    if (code == 45 || code == 48) return ICON_FOG_48;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return ICON_RAINY_48;
    if (code >= 71 && code <= 77) return ICON_SNOW_48;
    if (code >= 85 && code <= 86) return ICON_SNOW_48;
    if (code >= 95) return ICON_RAINY_48;
    return ICON_SUNNY_48;
}

uint16_t getWeatherIconColor(const String& icon) {
    int code = icon.toInt();
    if (code == 0 || code == 1) return GxEPD_YELLOW;
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


int dayIndexWithOffset(time_t utc, int timezoneOffset) {
    time_t localEpoch = utc + timezoneOffset;
    struct tm localTm;
    gmtime_r(&localEpoch, &localTm);
    return localTm.tm_yday;
}

void extractTodayTomorrow(JsonDocument& doc,
                           DayForecast& today,
                           DayForecast& tomorrow,
                           int timezoneOffset)   // seconds
{
    Serial.println("ExtractTodayTomorrow");

    // Convert "now" to LOCAL time
    time_t nowUTC   = time(nullptr);
    int todayIdx    = dayIndexWithOffset(nowUTC, timezoneOffset);
    Serial.println(todayIdx);

    for (JsonObject item : doc["list"].as<JsonArray>()) {

        time_t tUTC   = item["dt"];
        int idx       = dayIndexWithOffset(tUTC, timezoneOffset);

        DayForecast* d = nullptr;

        if (idx == todayIdx)            d = &today;
        else if (idx == todayIdx + 1)   d = &tomorrow;
        else continue;

        float tmin = item["main"]["temp_min"];
        float tmax = item["main"]["temp_max"];

        d->minTemp = min(d->minTemp, tmin);
        d->maxTemp = max(d->maxTemp, tmax);

        if (!d->valid) {
            d->icon  = item["weather"][0]["icon"].as<String>();
            d->valid = true;
        }
    }
}


int dayIndex(time_t t) {
    struct tm* tm = localtime(&t);
    return tm->tm_yday;
};

int drawWrappedText(
  int x,
  int y,
  int maxWidth,
  const String &text,
  int lineStep   // baseline-to-baseline, e.g. 22
) {
  String line = "";
  int cursorY = y;

  display.setCursor(x, cursorY);

  int i = 0;
  while (i < text.length()) {

    // get next word
    int start = i;
    while (i < text.length() && text[i] != ' ') i++;
    String word = text.substring(start, i);

    // measure candidate line
    int16_t x1, y1;
    uint16_t w, h;
    String candidate = line.length() ? line + " " + word : word;
    display.getTextBounds(candidate, x, cursorY, &x1, &y1, &w, &h);

    if (w > maxWidth && line.length()) {
      // print current line
      display.print(line);

      // move to next baseline
      cursorY += lineStep;
      display.setCursor(x, cursorY);

      line = word;
    } else {
      line = candidate;
    }

    i++; // skip space
  }

  // print last line
  if (line.length()) {
    display.print(line);
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
  Serial.println(out);
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
  Serial.println("DrawStatus");
  float vbat = readBatteryVoltageAvg(6);

   // float temperature = 23.56;
  char buf[34];

  snprintf(buf, sizeof(buf), "Bat: %.2f V", vbat);

  display.setCursor(700,460);
  display.print( buf );

  snprintf(buf, sizeof(buf), "V: %s",FW_VERSION);
  display.setCursor(5,460);
  display.print( buf );

  struct tm t;
  setConfiguredTimezone();
  if (getLocalTime(&t)) {
        Serial.println("Time");
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
