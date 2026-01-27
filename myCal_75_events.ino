#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

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

#include "my_secrets.h"
#include "myCal_75.h"

// ===================
// WIFI CONFIG
// ===================
const char* ssid     = SECRET_SSID;
const char* password = SECRET_PASS;

// ===================
// E-PAPER PINS
// ===================

#define EPD_CS    15
#define EPD_DC    27
#define EPD_RST   26
#define EPD_BUSY  25
#define EPD_SCK   13
#define EPD_MOSI  14
#define PIN_IO4   4  // display power enable
#define BAT_PIN   35

#define FW_VERSION "0.1.1"
#define uS_TO_S_FACTOR 1000000ULL
#define SLEEP_1H (3600ULL * uS_TO_S_FACTOR)
#define SLEEP_2H (7200ULL * uS_TO_S_FACTOR)

struct CalendarEvent {
  String title;
  String start;
  String end;
  bool allDay;
};

struct DayForecast {
  float minTemp =  999;
  float maxTemp = -999;
  String icon;
  bool valid = false;
};

const char* versionURL  = URL_VERSION;
const char* firmwareURL = URL_FW;

// ===================
// DISPLAY (RAM-SAFE BUFFER)
// ===================
GxEPD2_4C< GxEPD2_750c_GDEM075F52, GxEPD2_750c_GDEM075F52::HEIGHT/2> display( GxEPD2_750c_GDEM075F52(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY) );


#define MAX_DAYS   31
#define MAX_EVENTS 10

int daysWithEvents[MAX_DAYS];
int daysCount = 0;

CalendarEvent events[MAX_EVENTS];
int eventCount = 0;

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

  // Power on display
  pinMode(PIN_IO4, OUTPUT);
  digitalWrite(PIN_IO4, HIGH);

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);

  display.init(115200, true, 2, false);
  display.setRotation(0);

  connectWiFi();
  initTime();

  char calendarURL[256];
  snprintf(calendarURL, sizeof(calendarURL), "https://script.google.com/macros/s/%s/exec", GOOGLE_SCRIPT_ID);

  if (fetchCalendarData(
        calendarURL,
        daysWithEvents,
        daysCount,
        events,
        eventCount)) {

    Serial.println("Days with events:");
    for (int i = 0; i < daysCount; i++) {
      Serial.println(daysWithEvents[i]);
    }

    Serial.println("\nNext 24h events:");
    for (int i = 0; i < eventCount; i++) {
      Serial.printf(
        "- %s (%s → %s)\n",
        events[i].title.c_str(),
        events[i].start.c_str(),
        events[i].end.c_str()
      );
    }
  }

  drawUI();

  // Optional: turn WiFi off to save RAM + power
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  display.hibernate();

  digitalWrite(PIN_IO4, LOW);
  Serial.println("Going to sleep. Bye");
  esp_sleep_enable_timer_wakeup( SLEEP_2H );
  esp_deep_sleep_start();

}

void loop() {}

// ===================
// WIFI
// ===================
void connectWiFi()
{
  WiFi.begin(ssid, password);
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
      esp_sleep_enable_timer_wakeup(SLEEP_2H);  //30ULL * 1000000ULL);
                                            //   esp_sleep_enable_timer_wakeup( sleep_us ); //30ULL * 1000000ULL);
      Serial.flush();
      esp_deep_sleep_start();
  }

  Serial.println(" OK");
}

// ===================
// TIME (NTP)
// ===================
void initTime()
{
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  delay(2000);
  setTimezoneEST();
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

void setTimezoneEST() {
  // Eastern Time with daylight saving
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
  tzset();
}


bool hasEventOnDay(int day) {
  for (int i = 0; i < daysCount; i++) {
    if (daysWithEvents[i] == day) return true;
  }
  return false;
}

String formatEventDateTimeEST(const String &iso, bool includeEnd=false, const String &endIso="") {
  int y, mo, d, h, mi;
  sscanf(iso.c_str(), "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi);

  // ------------------------
  // Determine EST/EDT offset
  // ------------------------
  int offset = -5; // EST base
  bool isDST = false;

  // Simple DST rule (approximation): 2nd Sunday March → 1st Sunday Nov
  if (mo > 3 && mo < 11) isDST = true;        // Apr–Oct
  if (mo == 3 && d >= 8) isDST = true;        // March 2nd Sunday onward
  if (mo == 11 && d < 8) isDST = true;        // Nov 1st Sunday before end
  if (isDST) offset = -4;

  // Apply offset
  h += offset;
  if (h < 0) { h += 24; d -= 1; }
  if (h >= 24) { h -= 24; d += 1; }

  char buf[32];
  if (!includeEnd) {
    snprintf(buf, sizeof(buf), "%02d %02d:%02d", d, h, mi);
  } else {
    // parse end time
    int y2, mo2, d2, h2, mi2;
    sscanf(endIso.c_str(), "%d-%d-%dT%d:%d", &y2, &mo2, &d2, &h2, &mi2);
    // apply offset to end time
    h2 += offset;
    if (h2 < 0) { h2 += 24; d2 -= 1; }
    if (h2 >= 24) { h2 -= 24; d2 += 1; }

    snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d", h, mi, h2, mi2);
  }

  return String(buf);
}

void drawCalendar()
{
  struct tm timeinfo;
  while(!getLocalTime(&timeinfo)) {
    Serial.println("Waiting for time...");
    delay(500);
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

    display.setCursor(x, yy);
    display.print(d);

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

  for (int i = 0; i < eventCount && i < 5; i++) {
    String when = formatEventDateTimeEST(events[i].start, true, events[i].end);

    String line = utf8ToLatin1(when + " " + events[i].title);
    listY = drawWrappedText(
      listX,
      listY,
      maxWidth,
      line,
      lineHeight
    );

    listY+=lineHeight/2;
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
  Serial.println( payload );
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
    daysWithEvents[daysCount++] = d;
  }

  // -----------------------------
  // Events next 24h
  // -----------------------------
  eventCount = 0;
  JsonArray evts = doc["next24hEvents"];
  for (JsonObject e : evts) {
    events[eventCount].title = e["title"].as<String>();
    events[eventCount].start = e["start"].as<String>();
    events[eventCount].end   = e["end"].as<String>();
    events[eventCount].allDay = e["allDay"];
    eventCount++;
  }
  return true;
}

void drawForecast(int x, int y, uint16_t color) {
    DayForecast today, tomorrow;

    if (!fetchForecast(today, tomorrow)) return;
    
    // TODAY
    drawIcon(
        x, y,
        48, 48,
        getWeatherIconFromOW(today.icon),
        getWeatherIconColor(today.icon)
    );

    display.setFont(&Geologica_Bold14pt8b);

    String tempText =  utf8ToLatin1(String((int)today.minTemp) + "° / " + String((int)today.maxTemp) + "°");

    display.setCursor(x+55, y+32);
    display.print(tempText);
    
    // TOMORROW 
    x+=200;
    drawIcon(
        x, y,
        48, 48,
        getWeatherIconFromOW(tomorrow.icon),
        getWeatherIconColor(tomorrow.icon)
    );

    tempText =  utf8ToLatin1(String((int)tomorrow.minTemp) + "° / " + String((int)tomorrow.maxTemp) + "°");

    display.setCursor(x+55, y+32);
    display.print(tempText);
}


bool fetchForecast(DayForecast& today, DayForecast& tomorrow) {
    bool status=false;
    char forecastURL[256];
    snprintf(forecastURL, sizeof(forecastURL),
         "https://api.openweathermap.org/data/2.5/forecast?q=%s,%s&units=metric&cnt=16&appid=%s", CITY, COUNTRY, APIKEY);

    WiFiClientSecure client;
    client.setInsecure(); // for testing only

    HTTPClient http;
    http.begin(client, forecastURL);

    int code = http.GET();
    Serial.print("HTTP code: "); Serial.println(code);

    if(code == 200) {
        WiFiClient* stream = http.getStreamPtr();
        DynamicJsonDocument doc(20000);
        DeserializationError err = deserializeJson(doc, http.getStream());
        
        if(err) {
            Serial.print("JSON parse failed: "); Serial.println(err.c_str());
        } else {
            Serial.println("JSON parsed successfully!");
            int timezoneOffset = doc["city"]["timezone"];  
            extractTodayTomorrow(doc, today, tomorrow, timezoneOffset);
            status=true;
        }
    } else {
        Serial.println("HTTP GET failed");
    }
    http.end();

    return status;
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

  HTTPClient http;
  http.begin(versionURL);

  int httpCode = http.GET();
  if (httpCode != 200) {
    http.end();
    return;
  }

  String newVersion = http.getString();
  newVersion.trim();
  http.end();

  if ( compareVersions(newVersion, FW_VERSION) == 0 ) {
    Serial.println("Firmware is up to date");
    return;
  }

  Serial.printf("New firmware available: %s\n", newVersion.c_str());

    WiFiClientSecure client;
    client.setInsecure(); 

  t_httpUpdate_return ret = httpUpdate.update(client, firmwareURL);

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
    if (icon.startsWith("01")) return ICON_SUNNY_48;
    if (icon.startsWith("02")) return ICON_PARTLYCLOUDY_48;
    if (icon.startsWith("03") || icon.startsWith("04")) return ICON_CLOUDY_48;
    if (icon.startsWith("09") || icon.startsWith("10")) return ICON_RAINY_48;
    if (icon.startsWith("11")) return ICON_RAINY_48; //thunder
    if (icon.startsWith("13")) return ICON_SNOW_48;
    if (icon.startsWith("50")) return ICON_FOG_48;
    return ICON_SUNNY_48;
}

uint16_t getWeatherIconColor(const String& icon) {
    if (icon.startsWith("01")) return GxEPD_YELLOW;      // sunny
    if (icon.startsWith("11")) return GxEPD_RED;         // thunder
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


void extractTodayTomorrow(JsonDocument& doc,
                           DayForecast& today,
                           DayForecast& tomorrow,
                           int timezoneOffset)   // seconds
{
    Serial.println("ExtractTodayTomorrow");

    // Convert "now" to LOCAL time
    time_t nowUTC   = time(nullptr);
    time_t nowLocal = nowUTC;// + timezoneOffset;
    int todayIdx    = dayIndex(nowLocal);
    Serial.println(todayIdx);

    for (JsonObject item : doc["list"].as<JsonArray>()) {

        // Convert forecast time to LOCAL time
        time_t tUTC   = item["dt"];
        time_t tLocal = tUTC + timezoneOffset;

        // Serial.println(tLocal);
        int idx       = dayIndex(tLocal);
        // Serial.println(idx);

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
    analogReadResolution(12);  // 0–4095
    analogSetPinAttenuation(BAT_PIN, ADC_11db);

    long sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(BAT_PIN);
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
  setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
  tzset();
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