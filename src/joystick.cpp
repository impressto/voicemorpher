#include "globals.h"

int readJoystickAxis(int pin)
{
  int value = analogRead(pin);
  if (value < JOY_LOW_THRESHOLD) return -1;
  if (value > JOY_HIGH_THRESHOLD) return 1;
  return 0;
}

float readJoystickXIntensity()
{
  int value = analogRead(JOY_X_PIN);
  const int maxValue = 4095;
  if (value < 0) value = 0;
  if (value > maxValue) value = maxValue;
  return value / (float)maxValue;
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
