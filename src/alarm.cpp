#include "globals.h"

void setupTimeSync()
{
  if (!g_wifi_connected) return;
  configTime(g_tz_offset * 3600L, 0, NTP_SERVER1, NTP_SERVER2);
}

static int           s_lastFiredMinuteOfDay = -1;
static unsigned long s_snoozeUntilMs        = 0;

static bool fireAlarm()
{
  if (!isWiFiConnected()) {
    Serial.println("Alarm fired but WiFi unavailable, skipping");
    return false;
  }
  Serial.println("Alarm fired - starting Web Radio");
  bool snoozed = runRadioMenu(true);
  if (snoozed) {
    s_snoozeUntilMs = millis() + 5UL * 60UL * 1000UL;
    Serial.println("Snooze activated - 5 minutes");
    drawStatus("Snooze", "5 minutes...");
    delay(1500);
  } else {
    s_snoozeUntilMs = 0;
  }
  drawMenu();
  return true;
}

bool checkAlarmClock()
{
  // Snooze timer (independent of alarm minute-of-day guard)
  if (s_snoozeUntilMs > 0 && millis() >= s_snoozeUntilMs) {
    s_snoozeUntilMs = 0;
    return fireAlarm();
  }

  if (!g_alarm_enabled || !isWiFiConnected()) return false;

  struct tm ti;
  if (!getLocalTime(&ti, 0)) return false;
  if (ti.tm_year < (2020 - 1900)) return false;

  int nowMin   = ti.tm_hour * 60 + ti.tm_min;
  int alarmMin = g_alarm_hour * 60 + g_alarm_min;

  int minsPast = (nowMin - alarmMin + 1440) % 1440;
  if (minsPast <= 1 && s_lastFiredMinuteOfDay != alarmMin)
  {
    s_lastFiredMinuteOfDay = alarmMin;
    return fireAlarm();
  }
  return false;
}

// ===== Alarm Clock settings screen =====

static const int     ALARM_ROW_COUNT = 4;
static const int16_t ALARM_ROWS_Y    = 70;

static void drawNowLine()
{
  tft.fillRect(0, 42, TFT_W, 20, C_BG);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 44);

  struct tm ti;
  if (getLocalTime(&ti, 0) && ti.tm_year >= (2020 - 1900))
  {
    char buf[24];
    snprintf(buf, sizeof(buf), "Now: %02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    tft.print(buf);
  }
  else
  {
    tft.print("Now: not synced");
  }
}

static void drawAlarmRow(int i, int sel)
{
  int16_t y = ALARM_ROWS_Y + i * ITEM_H;
  char label[24];
  switch (i)
  {
    case 0: snprintf(label, sizeof(label), "Hour: %02d", g_alarm_hour); break;
    case 1: snprintf(label, sizeof(label), "Minute: %02d", g_alarm_min); break;
    case 2: snprintf(label, sizeof(label), "Alarm: %s", g_alarm_enabled ? "ON" : "OFF"); break;
    case 3: snprintf(label, sizeof(label), "TZ: UTC%+d", g_tz_offset); break;
  }

  tft.setTextSize(2);
  if (i == sel)
  {
    fillGradH(0, y, TFT_W, ITEM_H - 1, 0, 130, 190, 0, 55, 120);
    tft.fillRect(0, y, 4, ITEM_H - 1, TFT_CYAN);
    tft.setTextColor(TFT_WHITE);
  }
  else
  {
    uint16_t rc = (i & 1) ? tft.color565(12, 15, 38) : C_BG;
    tft.fillRect(0, y, TFT_W, ITEM_H - 1, rc);
    tft.setTextColor(0xDEFB);
  }
  tft.drawFastHLine(0, y + ITEM_H - 1, TFT_W, tft.color565(20, 25, 55));
  tft.setCursor(12, y + 4);
  tft.print(label);
}

void runAlarmClockMenu()
{
  while (isJoystickButtonPressed()) delay(10);

  tft.fillScreen(C_BG);
  drawHeader("Alarm Clock");
  drawNowLine();
  for (int i = 0; i < ALARM_ROW_COUNT; ++i) drawAlarmRow(i, 0);
  drawHints("Y: field   X: adjust", "Btn: done");

  int sel = 0;
  unsigned long lastMoveMs  = 0;
  unsigned long lastClockMs = millis();

  while (true)
  {
    unsigned long now = millis();

    if (now - lastClockMs >= 1000)
    {
      lastClockMs = now;
      drawNowLine();
    }

    int y = readJoystickAxis(JOY_Y_PIN);
    int x = readJoystickAxis(JOY_X_PIN);

    if ((y != 0 || x != 0) && now - lastMoveMs > 200)
    {
      lastMoveMs = now;
      if (y != 0)
      {
        int prevSel = sel;
        sel = (sel + (y < 0 ? -1 : 1) + ALARM_ROW_COUNT) % ALARM_ROW_COUNT;
        drawAlarmRow(prevSel, sel);
        drawAlarmRow(sel, sel);
      }
      else // x != 0: adjust the currently selected field
      {
        switch (sel)
        {
          case 0:
            g_alarm_hour = (g_alarm_hour + (x > 0 ? 1 : -1) + 24) % 24;
            g_prefs.putInt("alarm_hr", g_alarm_hour);
            break;
          case 1:
            g_alarm_min = (g_alarm_min + (x > 0 ? 1 : -1) + 60) % 60;
            g_prefs.putInt("alarm_min", g_alarm_min);
            break;
          case 2:
            g_alarm_enabled = !g_alarm_enabled;
            g_prefs.putBool("alarm_en", g_alarm_enabled);
            break;
          case 3:
            g_tz_offset += (x > 0 ? 1 : -1);
            if (g_tz_offset < -12) g_tz_offset = -12;
            if (g_tz_offset > 14)  g_tz_offset = 14;
            g_prefs.putInt("tz_offset", g_tz_offset);
            setupTimeSync();
            break;
        }
        drawAlarmRow(sel, sel);
      }
    }

    if (isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);
      return;
    }

    delay(10);
  }
}

// ===== Clock screensaver =====

void runClockScreensaver()
{
  tft.fillScreen(TFT_BLACK);

  static const char *const DAYS[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  static const char *const MONTHS[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                        "Jul","Aug","Sep","Oct","Nov","Dec"};
  int lastMin = -1;

  while (true)
  {
    if (checkAlarmClock()) return;  // alarm fired, radio played, exit screensaver

    if (readJoystickAxis(JOY_X_PIN) != 0 ||
        readJoystickAxis(JOY_Y_PIN) != 0 ||
        isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);
      return;
    }

    struct tm ti;
    bool synced = getLocalTime(&ti, 0) && ti.tm_year >= (2020 - 1900);
    int currentMin = synced ? (ti.tm_hour * 60 + ti.tm_min) : -2;

    if (currentMin != lastMin)
    {
      lastMin = currentMin;
      tft.fillScreen(TFT_BLACK);

      if (synced)
      {
        // HH:MM — textSize 7 = 42px/char wide, 56px tall; "HH:MM" = 210px wide
        char timeBuf[6];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", ti.tm_hour, ti.tm_min);
        tft.setTextSize(7);
        tft.setTextColor(TFT_RED);
        tft.setCursor((TFT_W - 210) / 2, 72);
        tft.print(timeBuf);

        // Day + date — textSize 2 = 12px/char wide, 16px tall
        char dateBuf[20];
        snprintf(dateBuf, sizeof(dateBuf), "%s %s %d",
                 DAYS[ti.tm_wday], MONTHS[ti.tm_mon], ti.tm_mday);
        tft.setTextSize(2);
        tft.setTextColor(tft.color565(120, 30, 30));  // dim red, no blue
        tft.setCursor((TFT_W - (int)strlen(dateBuf) * 12) / 2, 148);
        tft.print(dateBuf);

        // Alarm indicator at bottom
        if (g_alarm_enabled)
        {
          char alarmBuf[20];
          snprintf(alarmBuf, sizeof(alarmBuf), "Alarm: %02d:%02d", g_alarm_hour, g_alarm_min);
          tft.setTextSize(2);
          tft.setTextColor(tft.color565(180, 60, 60));  // warm red, no blue
          tft.setCursor((TFT_W - (int)strlen(alarmBuf) * 12) / 2, 208);
          tft.print(alarmBuf);
        }
      }
      else
      {
        tft.setTextSize(7);
        tft.setTextColor(COL_GRAY);
        tft.setCursor((TFT_W - 210) / 2, 92);
        tft.print("--:--");
      }
    }

    delay(50);
  }
}
