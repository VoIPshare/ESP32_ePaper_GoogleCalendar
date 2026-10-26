#include "weather.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ctype.h>

// Location handling adapted from ESP32-eInk-Dashboard (directFetch.cpp).

#define LOCATION_MAX 65

// Coordinates resolved from a city name survive deep sleep, so geocoding runs only once.
RTC_DATA_ATTR static float cachedLat = 0.0f;
RTC_DATA_ATTR static float cachedLon = 0.0f;
RTC_DATA_ATTR static char cachedLocation[LOCATION_MAX] = "";

static void urlEncode(const char* in, char* out, size_t cap) {
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  for (; *in && o + 4 < cap; in++) {
    unsigned char c = (unsigned char)*in;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out[o++] = (char)c;
    } else {
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 0x0F];
    }
  }
  out[o] = '\0';
}

static bool httpGetJson(const char* url, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[Weather] HTTP %d for %s\n", code, url);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[Weather] JSON error %s\n", err.c_str());
    return false;
  }
  return true;
}

static bool resolveLocation(const char* location, float& lat, float& lon) {
  if (location[0] == '\0') return false;

  if (sscanf(location, "%f , %f", &lat, &lon) == 2) return true;

  if (strcmp(cachedLocation, location) == 0) {
    lat = cachedLat;
    lon = cachedLon;
    return true;
  }

  // "City" or "City, CC" where CC is a 2-letter country code.
  char city[LOCATION_MAX];
  char country[3] = "";
  snprintf(city, sizeof(city), "%s", location);
  char* comma = strchr(city, ',');
  if (comma) {
    *comma = '\0';
    const char* cc = comma + 1;
    while (*cc == ' ') cc++;
    if (strlen(cc) == 2) {
      country[0] = (char)toupper((unsigned char)cc[0]);
      country[1] = (char)toupper((unsigned char)cc[1]);
      country[2] = '\0';
    }
  }
  for (int i = (int)strlen(city) - 1; i >= 0 && city[i] == ' '; i--) city[i] = '\0';

  char encoded[LOCATION_MAX * 3];
  urlEncode(city, encoded, sizeof(encoded));

  char url[320];
  snprintf(url, sizeof(url),
           "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&format=json%s%s",
           encoded, country[0] ? "&countryCode=" : "", country);

  JsonDocument doc;
  if (!httpGetJson(url, doc)) return false;

  JsonObject r = doc["results"][0];
  if (r.isNull()) {
    Serial.printf("[Weather] Location not found: %s\n", location);
    return false;
  }
  lat = r["latitude"] | 0.0f;
  lon = r["longitude"] | 0.0f;

  cachedLat = lat;
  cachedLon = lon;
  snprintf(cachedLocation, sizeof(cachedLocation), "%s", location);
  Serial.printf("[Weather] %s -> %.4f,%.4f\n", location, lat, lon);
  return true;
}

bool fetchForecast(const String& location, DayForecast days[], int count) {
  for (int i = 0; i < count; i++) days[i] = DayForecast();

  String loc = location;
  loc.trim();
  float lat, lon;
  if (!resolveLocation(loc.c_str(), lat, lon)) return false;

  char url[320];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min"
           "&forecast_days=%d&timezone=auto",
           lat, lon, count);

  JsonDocument doc;
  if (!httpGetJson(url, doc)) return false;

  JsonArray codes = doc["daily"]["weather_code"];
  JsonArray tmax = doc["daily"]["temperature_2m_max"];
  JsonArray tmin = doc["daily"]["temperature_2m_min"];
  if (codes.isNull()) return false;

  for (int i = 0; i < count && i < (int)codes.size(); i++) {
    days[i].code = codes[i] | 0;
    days[i].maxTemp = tmax[i] | 0.0f;
    days[i].minTemp = tmin[i] | 0.0f;
    days[i].valid = true;
  }
  return days[0].valid;
}
