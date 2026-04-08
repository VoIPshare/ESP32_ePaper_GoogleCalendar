// ===================
// FUNCTIONS
// ===================

struct DeviceConfig {
  String wifiSsid;
  String wifiPass;
  String city;
  String country;
  String googleScriptId;
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

void connectWiFi();
void initTime();
void drawUI();
void drawTopBar();
void drawCalendar();
void drawForecast(int, int, uint16_t);
void loadConfig();
bool isConfigComplete();
void applyBoardProfile(const String&, bool);
bool isKnownBoardProfile(const String&);
void initDisplayHardware();
uint64_t sleepDurationUs();
bool shouldCheckForOTA();
void startConfigPortal();
void drawConfigNeededScreen(const char* apName, const IPAddress& ip);
String buildConfigPage();
String buildBoardProfileOptions();
String buildSleepOptions();
String urlEncode(const String&);
void handleConfigRoot();
void handleConfigSave();
void setTimezoneEST();
bool parseIsoUtc(const String&, time_t&);
String formatEventDateTimeEST(const String&, bool, const String&);

int dayIndex(time_t) ;
int dayIndexWithOffset(time_t, int);
void extractTodayTomorrow(JsonDocument& ,DayForecast& , DayForecast& ,int );
bool fetchForecast(DayForecast&, DayForecast&, DayForecast&);
bool fetchCoordinates(float&, float&);
int compareVersions(const String& , const String& );
void checkForOTA();

bool fetchCalendarData(const char* ,int [],int &,CalendarEvent [],int &);

const uint8_t*  getWeatherIconFromOW(const String& );

uint16_t getWeatherIconColor(const String& );
void drawIcon(int , int ,int , int ,const uint8_t* ,const uint16_t );
int drawWrappedText(int ,int ,int ,const String &,int );
String utf8ToLatin1(const String& );
float readBatteryVoltageAvg(int );
void drawStatus();

void drawCenteredText( int16_t , int16_t , int16_t , int16_t , const char* , const GFXfont* , uint16_t );
