#pragma once
#include <Arduino.h>
#include <time.h>

// ===================
// CALENDAR (iCal / .ics feeds, read directly by the ESP32)
// ===================

#define MAX_DAYS        31
#define MAX_EVENTS      10
#define ICS_MAX_FEEDS    3
#define CAL_TITLE_MAX  128

struct CalendarEvent {
  char title[CAL_TITLE_MAX];
  time_t start;
  time_t end;
  bool allDay;
};

struct CalendarData {
  int daysWithEvents[MAX_DAYS];   // days of the current month (from today) with an event
  int daysCount;
  CalendarEvent events[MAX_EVENTS]; // events of the next 24 hours, sorted by start time
  int eventCount;
};

// Downloads up to ICS_MAX_FEEDS iCal feeds (empty entries are skipped) and merges them into `cal`,
// e.g. Google Calendar's public or secret address in iCal format.
// The local timezone (TZ) must already be set: it is used for floating / TZID times and day boundaries.
// Returns true if at least one feed was read; `failed` (optional) gets the number of feeds that failed.
bool fetchCalendarIcs(const String urls[], int urlCount, CalendarData& cal, int* failed = nullptr);

int daysInMonth(int year, int month);
