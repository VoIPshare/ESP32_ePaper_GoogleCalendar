#pragma once
#include "calendar.h"
#include "weather.h"

// ===================
// FUNCTIONS
// ===================

struct DeviceConfig {
  String wifiSsid;
  String wifiPass;
  String weatherLocation;          // "City", "City, CC" or "lat,lon"
  String icsUrls[ICS_MAX_FEEDS];   // iCal feeds, merged
  String timezone;
  String otaVersionUrl;
  String otaFirmwareUrl;
  String boardProfile;
  bool otaEnabled;
  int sleepHours;
  int epdCs;
  int epdDc;
  int epdRst;
  int epdBusy;
  int epdSck;
  int epdMosi;
  int pinDisplayPower;
  int batPin;
};

void connectWiFi();
void initTime();
void drawUI();
void drawTopBar();
void drawCalendar();
void drawForecast(int, int);
void loadConfig();
bool isConfigComplete();
bool hasIcsFeeds();
void applyBoardProfile(const String&, bool);
bool isKnownBoardProfile(const String&);
void initDisplayHardware();
uint64_t sleepDurationUs();
bool shouldCheckForOTA();
String defaultOtaVersionUrl();
String defaultOtaFirmwareUrl();
void startConfigPortal();
void drawConfigNeededScreen(const char* apName, const IPAddress& ip);
String buildConfigPage();
String buildBoardProfileOptions();
String buildSleepOptions();
void handleConfigRoot();
void handleConfigSave();
void setConfiguredTimezone();
String formatEventTime(const CalendarEvent&, const struct tm&);

int compareVersions(const String& , const String& );
void checkForOTA();

const uint8_t*  getWeatherIcon(int);
uint16_t getWeatherIconColor(int);
void drawIcon(int , int ,int , int ,const uint8_t* ,const uint16_t );
uint16_t textWidth(const String &);
String ellipsize(String, int);
int drawWrappedText(int ,int ,int ,const String &,int, int, int );
String utf8ToLatin1(const String& );
float readBatteryVoltageAvg(int );
void drawStatus();

void drawCenteredText( int16_t , int16_t , int16_t , int16_t , const char* , const GFXfont* , uint16_t );
