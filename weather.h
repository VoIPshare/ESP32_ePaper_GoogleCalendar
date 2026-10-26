#pragma once
#include <Arduino.h>

// ===================
// WEATHER (Open-Meteo, no API key)
// ===================

#define FORECAST_DAYS 3

struct DayForecast {
  float minTemp = 0;
  float maxTemp = 0;
  int code = 0;          // WMO weather code
  bool valid = false;
};

// `location` is "City", "City, CC" (2-letter country code) or "latitude,longitude".
// A city is geocoded once; the coordinates are kept in RTC memory across deep sleep.
// Fills `days[0..count-1]` (today first); returns true if today's forecast was read.
bool fetchForecast(const String& location, DayForecast days[], int count);
