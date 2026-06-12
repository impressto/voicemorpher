#include "globals.h"

int readJoystickAxis(int pin)
{
  int value = analogRead(pin);
  if (value < JOY_LOW_THRESHOLD) return -1;
  if (value > JOY_HIGH_THRESHOLD) return 1;
  return 0;
}

// Piecewise map around the calibrated rest position: [jx_min,jx_center]
// -> [0,0.5], [jx_center,jx_max] -> [0.5,1]. This gives each direction of
// travel a full 0..0.5 swing even if the stick's spring center sits close
// to one end of its raw ADC range (uncalibrated defaults reduce to the old
// linear value/4095 mapping).
float readJoystickXIntensity()
{
  int value = analogRead(JOY_X_PIN);

  if (value <= g_jx_min) return 0.0f;
  if (value >= g_jx_max) return 1.0f;
  if (value < g_jx_center)
    return 0.5f * (float)(value - g_jx_min) / (float)(g_jx_center - g_jx_min);
  return 0.5f + 0.5f * (float)(value - g_jx_center) / (float)(g_jx_max - g_jx_center);
}

bool isJoystickButtonPressed()
{
  return digitalRead(JOY_BTN_PIN) == LOW;
}

// Returns distance in cm, or -1.0 on timeout/no echo.
float readHCSR04cm()
{
  digitalWrite(HC_SR04_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(HC_SR04_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(HC_SR04_TRIG_PIN, LOW);
  long dur = pulseIn(HC_SR04_ECHO_PIN, HIGH, 10000);
  if (dur == 0) return -1.0f;
  return dur / 58.0f;
}

// File-scope state for handleJoystickMenu
static int lastJoystickY = 0;
static unsigned long lastJoystickMoveMs = 0;

void handleJoystickMenu()
{
  int y = readJoystickAxis(JOY_Y_PIN);
  unsigned long now = millis();

  if (y != lastJoystickY)
  {
    Serial.printf("JOY read y=%d menu=%d\n", y, currentMenu);
    lastJoystickY = y;
  }

  if (y != 0 && now - lastJoystickMoveMs > 200)
  {
    currentMenu = (currentMenu + MENU_ROOT_COUNT + (y < 0 ? -1 : 1)) % MENU_ROOT_COUNT;
    drawMenu();
    lastJoystickMoveMs = now;
  }

  if (y == 0)
  {
    lastJoystickY = 0;
  }

  if (isJoystickButtonPressed())
  {
    Serial.println("JOY button pressed");
    runMenuAction(currentMenu);
    delay(300);
  }
}

// Calibrates the joystick X axis for readJoystickXIntensity(): step 1
// captures the spring-rest "center" reading, step 2 sweeps the full
// left/right range. Saves jx_min/jx_center/jx_max to prefs.
void calibrateJoystickX()
{
  // Step 1: capture rest/center position
  tft.fillScreen(C_BG);
  fillGradH(0, 0, TFT_W, 36, 0, 55, 140, 0, 15, 65);
  tft.drawFastHLine(0, 36, TFT_W, TFT_CYAN);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.print("Joy Cal X");
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(1);
  tft.setCursor(8, 50);
  tft.print("Step 1/2: center");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(8, 80);
  tft.print("Let go of the stick so X");
  tft.setCursor(8, 95);
  tft.print("returns to rest,");
  tft.setCursor(8, 110);
  tft.print("then press button.");
  tft.setTextColor(COL_GRAY);
  tft.setCursor(4, TFT_H - 12);
  tft.print("Btn: capture center");

  while (!isJoystickButtonPressed()) delay(10);
  while (isJoystickButtonPressed())  delay(10);
  int xCenter = analogRead(JOY_X_PIN);

  // Step 2: sweep full left/right range
  tft.fillScreen(C_BG);
  fillGradH(0, 0, TFT_W, 36, 0, 55, 140, 0, 15, 65);
  tft.drawFastHLine(0, 36, TFT_W, TFT_CYAN);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.print("Joy Cal X");
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(1);
  tft.setCursor(8, 50);
  tft.print("Step 2/2: range");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(8, 80);
  tft.print("Push X fully left & right,");
  tft.setCursor(8, 95);
  tft.print("then press button.");
  tft.setTextColor(COL_GRAY);
  tft.setCursor(4, TFT_H - 12);
  tft.print("Btn: done");

  const int16_t CB_X = 8, CB_Y = 145, CB_W = TFT_W - 16, CB_H = 22;
  tft.drawRect(CB_X - 1, CB_Y - 1, CB_W + 2, CB_H + 2, tft.color565(40, 50, 100));

  int xMin = xCenter, xMax = xCenter, lastBarFill = -1;

  while (!isJoystickButtonPressed())
  {
    int x = analogRead(JOY_X_PIN);
    if (x < xMin) xMin = x;
    if (x > xMax) xMax = x;

    int sweepPx = (int)((float)(xMax - xMin) / 4095.0f * CB_W);
    if (sweepPx != lastBarFill)
    {
      lastBarFill = sweepPx;
      tft.fillRect(CB_X, CB_Y, CB_W, CB_H, tft.color565(10, 12, 30));
      int startX = (int)((float)xMin / 4095.0f * CB_W);
      tft.fillRect(CB_X + startX, CB_Y, sweepPx, CB_H, TFT_CYAN);
    }
    delay(20);
  }
  while (isJoystickButtonPressed()) delay(10);

  if (xMax <= xMin) { xMin = 100; xMax = 3900; }

  int margin = (xMax - xMin) / 20;
  xMin = max(0,    xMin - margin);
  xMax = min(4095, xMax + margin);

  // Keep center strictly inside (min, max) so both halves of the piecewise
  // map in readJoystickXIntensity() have non-zero range.
  if (xCenter <= xMin) xCenter = xMin + 1;
  if (xCenter >= xMax) xCenter = xMax - 1;

  g_jx_min    = xMin;
  g_jx_center = xCenter;
  g_jx_max    = xMax;
  g_prefs.putInt("jx_min", xMin);
  g_prefs.putInt("jx_center", xCenter);
  g_prefs.putInt("jx_max", xMax);

  drawStatus("X Cal saved!", "");
  delay(1000);
}
