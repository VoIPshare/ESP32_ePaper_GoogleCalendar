// ===================
// FUNCTIONS
// ===================
void connectWiFi();
void initTime();
void drawUI();
void drawTopBar();
void drawCalendar();
void setTimezoneEST();

int dayIndex(time_t) ;
void extractTodayTomorrow(JsonDocument& ,DayForecast& , DayForecast& ,int );
bool fetchForecast(DayForecast& tday, DayForecast& );
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
