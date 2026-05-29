#include <Arduino.h>
#include <TFT_eSPI.h>
#include "menu_icons.h"
#include <LittleFS.h>
using namespace fs;
#include <Preferences.h>
#include "driver/i2s.h"
#include "config.h"

static Preferences g_prefs;

// Pin definitions (separate pins for each device for easier debugging)
// INMP441 (microphone - RX)
#define I2S_RX_BCK 4      // BCLK
#define I2S_RX_WS 5       // LRCLK/WS
#define I2S_RX_SD 6       // SD (data out -> input)

// MAX98357A (amplifier - TX)
#define I2S_TX_BCK 47     // BCLK
#define I2S_TX_WS 45      // LRCLK
#define I2S_TX_SD 38      // DIN (data in)
#define AMP_SD 21         // amplifier shutdown/enable pin (AMP_SD)

// ST7789V TFT display — pins defined in include/User_Setup.h
#define TFT_W    320   // logical width after setRotation(1)
#define TFT_H    240   // logical height after setRotation(1)
#define ITEM_H    26   // menu row height (9 × 26 = 234 fits in 240px)
#define COL_GRAY 0x7BEF

// Waveform display geometry — set WAVEFORM_HEIGHT in src/config.h to resize.
#define WF_X   8
#define WF_Y   38
#define WF_W   (TFT_W - 16)
#define WF_H   WAVEFORM_HEIGHT
#define WF_CY  (WF_Y + WF_H / 2)

// Joystick pins
#define JOY_X_PIN 17
#define JOY_Y_PIN 20
#define JOY_BTN_PIN 35
#define JOY_LOW_THRESHOLD 1200
#define JOY_HIGH_THRESHOLD 2800

// Audio parameters
const int SAMPLE_RATE = 11025;
const int BITS_PER_SAMPLE = 16;
const int CHANNELS = 1; // mono

// Recording buffer: 10s at 11025 Hz = 220500 bytes (same RAM as 5s at 22050 Hz)
static int active_sample_count = SAMPLE_RATE * 5;
static int g_max_record_secs = 10;
static int16_t *record_buffer = nullptr;
static bool g_has_recording = false;

// Playback volume config
float playback_gain = DEFAULT_PLAYBACK_GAIN; // adjust default volume in src/config.h
  
// Menu and UI
TFT_eSPI tft = TFT_eSPI();

enum MenuItem
{
  // Root menu items (0 to MENU_ROOT_COUNT-1)
  MENU_RECORD,
  MENU_PLAY,
  MENU_EFFECTS,
  MENU_PASSTHROUGH,
  MENU_STORAGE,
  MENU_VOLUME,
  MENU_ROOT_COUNT,

  // Storage sub-menu items (MENU_ROOT_COUNT to MENU_STORAGE_COUNT-1)
  MENU_LONG_REC = MENU_ROOT_COUNT,
  MENU_LONG_PLAY,
  MENU_STORAGE_COUNT,

  // Effects sub-menu items (MENU_STORAGE_COUNT to MENU_COUNT-1)
  MENU_REVERSE = MENU_STORAGE_COUNT,
  MENU_PITCH,
  MENU_ECHO,
  MENU_RINGMOD,
  MENU_STUTTER,
  MENU_TREMOLO,
  MENU_HAUNTED,
  MENU_ALIEN,
  MENU_MONSTER,
  MENU_CHORUS,
  MENU_COUNT
};

// FontAwesome 4.x glyph codepoints encoded as UTF-8 literals
#define FA_BOLT      "\xEF\x83\xA7"  // U+F0E7
#define FA_MIC       "\xEF\x84\xB0"  // U+F130
#define FA_PLAY      "\xEF\x81\x8B"  // U+F04B
#define FA_MAGIC     "\xEF\x83\x90"  // U+F0D0
#define FA_PHONES    "\xEF\x80\xA5"  // U+F025
#define FA_SAVE      "\xEF\x83\x87"  // U+F0C7
#define FA_FOLDER    "\xEF\x81\xBC"  // U+F07C
#define FA_VOLUME    "\xEF\x80\xA8"  // U+F028
#define FA_BACK      "\xEF\x81\x8A"  // U+F04A
#define FA_MUSIC     "\xEF\x80\x81"  // U+F001
#define FA_ARROWS    "\xEF\x81\x87"  // U+F047
#define FA_CIRCLE    "\xEF\x84\x8C"  // U+F10C
#define FA_FFWD      "\xEF\x81\x90"  // U+F050
#define FA_BARCHART  "\xEF\x82\x80"  // U+F080
#define FA_MOON      "\xEF\x86\x86"  // U+F186
#define FA_ROCKET    "\xEF\x84\xB5"  // U+F135
#define FA_BUG       "\xEF\x86\x88"  // U+F188
#define FA_GROUP     "\xEF\x83\x80"  // U+F0C0
#define FA_UP        "\xEF\x81\xB7"  // U+F077
#define FA_DOWN      "\xEF\x81\xB8"  // U+F078

static const char *menuLabels[] = {
  // Root menu items
  "Record",
  "Play",
  "Effects",
  "Live FX",
  "Storage",
  "Volume",
  // Storage sub-menu items
  "Stored Rec",
  "Stored Play",
  // Effects sub-menu items
  "Reverse",
  "Pitch",
  "Echo",
  "Ring Mod",
  "Stutter",
  "Tremolo",
  "Haunted",
  "Alien",
  "Monster",
  "Chorus",
};

int currentMenu = 0;
int lastMenu = -1;
static int lastJoystickY = 0;
static unsigned long lastJoystickMoveMs = 0;

// Waveform playback display state
static bool    g_waveform_visible = false;
static int32_t g_wf_total         = 0;       // 0 = use active_sample_count as denominator
static int     g_wf_last_px       = -1;
static bool    g_wf_use_peaks     = false;   // true when rendering from g_wf_peaks (Stored Play)
static int16_t g_wf_peaks[TFT_W] = {};      // peak sample per display column for Stored Play
static int     g_wf_peak          = 32768;   // actual peak amplitude; normalises the display height

// I2S ports
const i2s_port_t I2S_RX_PORT = I2S_NUM_0;
const i2s_port_t I2S_TX_PORT = I2S_NUM_1;

void drawMenu();
void drawStatus(const char *line1, const char *line2 = nullptr);
void handleJoystickMenu();
void runMenuAction(int item);

void recordToBuffer();
void playBufferSimple();
void passthrough();
void passthroughWithEffect(int fx);
void playReverse();
void playResample(float speed);
void playEcho(float delaySec, float decay);
void playRingMod(float freq);
void playStutter(float chunkSec, int repeats);
void playTremolo(float rate);
void playHaunted(float delaySec, float decay);
void playAlien(float speed, float ringFreq);
void playMonster(float speed, float delaySec, float decay);
void playChorus(float rate, float depth);
static int16_t applyPlaybackGain(int16_t sample);
static void stopTxAndFlush();
static void playRecordingDoneBlips();

static int readJoystickAxis(int pin)
{
  int value = analogRead(pin);
  if (value < JOY_LOW_THRESHOLD) return -1;
  if (value > JOY_HIGH_THRESHOLD) return 1;
  return 0;
}

static float readJoystickXIntensity()
{
  int value = analogRead(JOY_X_PIN);
  const int maxValue = 4095;
  if (value < 0) value = 0;
  if (value > maxValue) value = maxValue;
  return value / (float)maxValue;
}

static bool isJoystickButtonPressed()
{
  return digitalRead(JOY_BTN_PIN) == LOW;
}

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

// ── Gradient helpers ────────────────────────────────────────────────────────

// Top→bottom gradient fill over a rectangle
static void fillGradH(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint8_t r1, uint8_t g1, uint8_t b1,
                       uint8_t r2, uint8_t g2, uint8_t b2)
{
  for (int16_t i = 0; i < h; ++i)
  {
    float t = (h > 1) ? (float)i / (h - 1) : 0.0f;
    uint8_t r = (uint8_t)(r1 + ((int16_t)r2 - r1) * t);
    uint8_t g = (uint8_t)(g1 + ((int16_t)g2 - g1) * t);
    uint8_t b = (uint8_t)(b1 + ((int16_t)b2 - b1) * t);
    tft.drawFastHLine(x, y + i, w, tft.color565(r, g, b));
  }
}

// Left→right gradient fill over a rectangle
static void fillGradV(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint8_t r1, uint8_t g1, uint8_t b1,
                       uint8_t r2, uint8_t g2, uint8_t b2)
{
  for (int16_t i = 0; i < w; ++i)
  {
    float t = (w > 1) ? (float)i / (w - 1) : 0.0f;
    uint8_t r = (uint8_t)(r1 + ((int16_t)r2 - r1) * t);
    uint8_t g = (uint8_t)(g1 + ((int16_t)g2 - g1) * t);
    uint8_t b = (uint8_t)(b1 + ((int16_t)b2 - b1) * t);
    tft.drawFastVLine(x + i, y, h, tft.color565(r, g, b));
  }
}

static const uint16_t C_BG = 0x0821; // dark navy background

// Colored header bar with white title (y 0–36, separator line at y=36)
static void drawHeader(const char *title)
{
  fillGradH(0, 0, TFT_W, 36, 0, 55, 140, 0, 15, 65);
  tft.fillRect(0, 0, 4, 36, TFT_CYAN);
  tft.drawFastHLine(0, 36, TFT_W, TFT_CYAN);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print(title);
}

// Gray hint text at the bottom of a sub-menu screen
static void drawHints(const char *line1, const char *line2 = nullptr)
{
  int16_t hy = TFT_H - (line2 ? 34 : 20);
  tft.fillRect(0, hy - 4, TFT_W, TFT_H - (hy - 4), C_BG);
  tft.drawFastHLine(0, hy - 5, TFT_W, tft.color565(25, 30, 65));
  tft.setTextSize(1);
  tft.setTextColor(COL_GRAY);
  tft.setCursor(4, hy);
  tft.print(line1);
  if (line2)
  {
    tft.setCursor(4, hy + 14);
    tft.print(line2);
  }
}

// Progress bar with rounded frame and green→yellow gradient fill
static void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, float level)
{
  tft.drawRoundRect(x, y, w, h, 3, TFT_WHITE);
  tft.fillRoundRect(x + 1, y + 1, w - 2, h - 2, 2, tft.color565(12, 15, 38));
  int16_t fw = (int16_t)(level * (w - 2));
  if (fw > 0)
    fillGradV(x + 1, y + 1, fw, h - 2, 0, 210, 100, 200, 200, 0);
}

// ── Main menu ────────────────────────────────────────────────────────────────

void drawMenu()
{
  tft.fillScreen(C_BG);
  tft.setTextSize(2);

  for (int i = 0; i < MENU_ROOT_COUNT; ++i)
  {
    int16_t y = i * ITEM_H;
    uint16_t iconColor;
    if (i == currentMenu)
    {
      fillGradH(0, y, TFT_W, ITEM_H - 1, 0, 130, 190, 0, 55, 120);
      tft.fillRect(0, y, 4, ITEM_H - 1, TFT_CYAN);
      tft.setTextColor(TFT_WHITE);
      iconColor = TFT_WHITE;
    }
    else
    {
      uint16_t rc = (i & 1) ? tft.color565(12, 15, 38) : C_BG;
      tft.fillRect(0, y, TFT_W, ITEM_H - 1, rc);
      tft.setTextColor(0xDEFB);
      iconColor = 0xDEFB;
    }
    tft.drawFastHLine(0, y + ITEM_H - 1, TFT_W, tft.color565(20, 25, 55));
    tft.drawBitmap(6, y + 5, MENU_ICONS[i], 16, 16, iconColor);
    tft.setCursor(28, y + 5);
    tft.print(menuLabels[i]);
  }
}

void drawStatus(const char *line1, const char *line2)
{
  tft.fillScreen(C_BG);
  // Thin accent strip at top
  fillGradH(0, 0, TFT_W, 4, 0, 200, 220, 0, 80, 150);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(4, 90);
  tft.print(line1);
  if (line2)
  {
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(4, 120);
    tft.print(line2);
  }
}

// Redraws one pixel column of the waveform (used for initial draw and cursor erase).
static void drawWfColumn(int px)
{
  int x = WF_X + 1 + px;
  tft.drawFastVLine(x, WF_Y + 1, WF_H - 2, C_BG);
  tft.drawPixel(x, WF_CY, tft.color565(20, 30, 60));

  const int innerW = WF_W - 2;
  int16_t s;
  if (g_wf_use_peaks)
  {
    s = g_wf_peaks[px];
  }
  else
  {
    int sIdx = (int)((int64_t)px * active_sample_count / innerW);
    if (sIdx >= active_sample_count) sIdx = active_sample_count - 1;
    s = record_buffer[sIdx];
  }
  int amp = (int)s * (WF_H / 2 - 2) / g_wf_peak;
  int y0 = WF_CY, y1 = WF_CY - amp;
  if (y1 < WF_Y + 1) y1 = WF_Y + 1;
  if (y1 > WF_Y + WF_H - 2) y1 = WF_Y + WF_H - 2;
  uint16_t wc = tft.color565(0, 180, 220);
  if (y1 <= y0) tft.drawFastVLine(x, y1, y0 - y1 + 1, wc);
  else          tft.drawFastVLine(x, y0, y1 - y0 + 1, wc);
}

static void drawWaveformScreen(const char *title)
{
  g_wf_last_px = -1;

  // Normalise to actual peak so quiet recordings still fill the box.
  // Floor at 1000 (~3 % of full scale) so near-silence isn't stretched to noise.
  {
    int peak = 1;
    if (g_wf_use_peaks)
    {
      for (int px = 0; px < WF_W - 2; ++px)
        if (abs(g_wf_peaks[px]) > peak) peak = abs(g_wf_peaks[px]);
    }
    else
    {
      for (int i = 0; i < active_sample_count; ++i)
        if (abs(record_buffer[i]) > peak) peak = abs(record_buffer[i]);
    }
    g_wf_peak = peak > 1000 ? peak : 1000;
  }

  tft.fillScreen(C_BG);
  fillGradH(0, 0, TFT_W, 36, 0, 55, 140, 0, 15, 65);
  tft.drawFastHLine(0, 36, TFT_W, TFT_CYAN);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.print(title);

  tft.drawRect(WF_X, WF_Y, WF_W, WF_H, tft.color565(40, 50, 100));
  tft.drawFastHLine(WF_X + 1, WF_CY, WF_W - 2, tft.color565(20, 30, 60));

  for (int px = 0; px < WF_W - 2; ++px)
    drawWfColumn(px);
}

// Scans the already-open LittleFS file to fill g_wf_peaks, then seeks back to
// the start of sample data so the caller can immediately begin playback.
static void fillStoredWaveformBuf(File &f, int32_t totalSamples)
{
  const int innerW = WF_W - 2;
  memset(g_wf_peaks, 0, sizeof(g_wf_peaks));
  if (totalSamples <= 0) return;

  const int CHUNK = 256;
  int16_t chunk[CHUNK];
  int32_t pos = 0;

  while (pos < totalSamples)
  {
    int toRead = (int)min((int32_t)CHUNK, totalSamples - pos);
    int got = (int)(f.read((uint8_t *)chunk, toRead * sizeof(int16_t)) / sizeof(int16_t));
    if (got == 0) break;
    for (int i = 0; i < got; ++i)
    {
      int px = (int)((int64_t)(pos + i) * innerW / totalSamples);
      if (px >= innerW) px = innerW - 1;
      if (abs(chunk[i]) > abs(g_wf_peaks[px]))
        g_wf_peaks[px] = chunk[i];
    }
    pos += got;
  }

  f.seek(sizeof(int32_t));  // rewind to start of sample data for playback
}

static void drawWaveformPlayhead(int samplePos)
{
  const int innerW = WF_W - 2;
  int32_t total = g_wf_total > 0 ? g_wf_total : (int32_t)active_sample_count;
  int px = (int)((int64_t)samplePos * innerW / total);
  if (px < 0) px = 0;
  if (px >= innerW) px = innerW - 1;
  if (px == g_wf_last_px) return;
  if (g_wf_last_px >= 0) drawWfColumn(g_wf_last_px);
  tft.drawFastVLine(WF_X + 1 + px, WF_Y + 1, WF_H - 2, TFT_WHITE);
  g_wf_last_px = px;
}

// Shows a sub-menu to select recording duration (1 to maxSecs seconds).
// Joystick X adjusts in 1-second steps; button confirms.
static int showDurationSubMenu(int currentSecs, int maxSecs = g_max_record_secs)
{
  int secs = currentSecs < 1 ? 1 : (currentSecs > maxSecs ? maxSecs : currentSecs);
  unsigned long lastMoveMs = 0;

  const int16_t BX = 8, BY = 46, BW = TFT_W - 16, BH = 28;
  int prevSecs = -1;

  // Static parts drawn once
  tft.fillScreen(C_BG);
  drawHeader("Record Duration");
  drawHints("< X: adjust >", "Btn: record");

  while (true)
  {
    // Only redraw bar + value when duration changes
    if (secs != prevSecs)
    {
      prevSecs = secs;
      float lvl = (maxSecs > 1) ? (float)(secs - 1) / (maxSecs - 1) : 1.0f;
      drawProgressBar(BX, BY, BW, BH, lvl);

      tft.fillRect(BX, BY + BH + 8, BW, 22, C_BG);
      char valBuf[32];
      snprintf(valBuf, sizeof(valBuf), "Duration: %ds", secs);
      tft.setTextColor(TFT_CYAN);
      tft.setTextSize(2);
      tft.setCursor(BX, BY + BH + 10);
      tft.print(valBuf);
    }

    int x = readJoystickAxis(JOY_X_PIN);
    unsigned long now = millis();

    if (x != 0 && now - lastMoveMs > 200)
    {
      secs += x;
      if (secs < 1) secs = 1;
      if (secs > maxSecs) secs = maxSecs;
      lastMoveMs = now;
    }

    if (isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);
      return secs;
    }

    delay(20);
  }
}

// Persistent effect levels (0.0–1.0) remembered between plays
// neutral (1.0x) sits at level = (1.0 - 0.3) / (2.5 - 0.3) ≈ 0.318
static float s_pitchLevel   = 0.318f;
static float s_echoLevel    = 0.5f;
static float s_ringmodLevel = 0.5f;
static float s_stutterLevel = 0.5f;
static float s_tremoloLevel = 0.3f;
static float s_hauntedLevel = 0.5f;
static float s_alienLevel   = 0.5f;
static float s_monsterLevel = 0.5f;
static float s_chorusLevel   = 0.5f;
// volume: maps [0,1] to gain [0.5, 10.0]; 0.579 ≈ DEFAULT_PLAYBACK_GAIN=6.0
static float s_volumeLevel   = (DEFAULT_PLAYBACK_GAIN - 0.5f) / 9.5f;
static int   g_long_rec_secs = 60;

// Shows a full-screen sub-menu for adjusting a single effect parameter.
// Joystick X moves the level left/right in 5% steps; button confirms and returns the level.
// current is in [0,1]; minVal/maxVal are the display range with the given unit string.
static float showLevelSubMenu(const char *title, const char *paramLabel,
                               float current, float minVal, float maxVal, const char *unit)
{
  float level = current;
  unsigned long lastMoveMs = 0;
  while (isJoystickButtonPressed()) delay(10);

  const int16_t BX = 8, BY = 46, BW = TFT_W - 16, BH = 28;
  float prevLevel = -1.0f;

  // Static parts drawn once
  tft.fillScreen(C_BG);
  drawHeader(title);
  drawHints("< >: adjust   Y: back", "Btn: play");

  while (true)
  {
    // Only redraw bar + value when level changes
    if (level != prevLevel)
    {
      prevLevel = level;
      float actualVal = minVal + level * (maxVal - minVal);
      drawProgressBar(BX, BY, BW, BH, level);

      tft.fillRect(BX, BY + BH + 8, BW, 22, C_BG);
      char valBuf[32];
      if (unit[0] == 'x')
        snprintf(valBuf, sizeof(valBuf), "%s: %.2f%s", paramLabel, actualVal, unit);
      else
        snprintf(valBuf, sizeof(valBuf), "%s: %.0f%s", paramLabel, actualVal, unit);
      tft.setTextColor(TFT_CYAN);
      tft.setTextSize(2);
      tft.setCursor(BX, BY + BH + 10);
      tft.print(valBuf);
    }

    int x = readJoystickAxis(JOY_X_PIN);
    int y = readJoystickAxis(JOY_Y_PIN);
    unsigned long now = millis();

    if (x != 0 && now - lastMoveMs > 150)
    {
      level += x * 0.05f;
      if (level < 0.0f) level = 0.0f;
      if (level > 1.0f) level = 1.0f;
      lastMoveMs = now;
    }

    if (y != 0) return -1.0f;

    if (isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);
      return level;
    }

    delay(20);
  }
}

// Joystick X cycles through live effect choices; button confirms.
static int showPassthroughFxSubMenu()
{
  static const char *choices[] = { "Plain", "Echo", "Star Fighter", "Tremolo", "Chorus", "Distort", "Telephone", "Pitch Up", "Pitch Dn" };
  const int NUM_CHOICES = 9;
  int sel = 0;
  unsigned long lastMoveMs = 0;

  int prevSel = -1;
  tft.fillScreen(C_BG);
  drawHeader("Passthrough FX");
  drawHints("< X: choose >", "Btn: start");

  while (true)
  {
    if (sel != prevSel)
    {
      prevSel = sel;
      tft.fillRect(4, 50, TFT_W - 8, 22, C_BG);
      tft.setTextColor(TFT_CYAN);
      tft.setTextSize(2);
      tft.setCursor(8, 100);
      char valBuf[32];
      snprintf(valBuf, sizeof(valBuf), "FX: %s", choices[sel]);
      // Fill old text area, then draw new
      tft.fillRect(4, 90, TFT_W - 8, 22, C_BG);
      tft.setCursor(8, 90);
      tft.print(valBuf);
    }

    int x = readJoystickAxis(JOY_X_PIN);
    unsigned long now = millis();
    if (x != 0 && now - lastMoveMs > 200)
    {
      sel = (sel + x + NUM_CHOICES) % NUM_CHOICES;
      lastMoveMs = now;
    }
    if (isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);
      return sel;
    }
    delay(20);
  }
}

static int showLongPlayFxSubMenu()
{
  static const char *choices[] = { "Plain", "Echo", "Star Fghtr", "Tremolo", "Chorus", "Pitch Up", "Pitch Dn" };
  const int NUM_CHOICES = 7;
  int sel = 0;
  unsigned long lastMoveMs = 0;
  while (isJoystickButtonPressed()) delay(10);

  int prevSel = -1;
  tft.fillScreen(C_BG);
  drawHeader("Stored Play FX");
  drawHints("< X: choose >", "Btn: play");

  while (true)
  {
    if (sel != prevSel)
    {
      prevSel = sel;
      tft.fillRect(4, 90, TFT_W - 8, 22, C_BG);
      tft.setTextColor(TFT_CYAN);
      tft.setTextSize(2);
      tft.setCursor(8, 90);
      char valBuf[32];
      snprintf(valBuf, sizeof(valBuf), "FX: %s", choices[sel]);
      tft.print(valBuf);
    }

    int x = readJoystickAxis(JOY_X_PIN);
    unsigned long now = millis();
    if (x != 0 && now - lastMoveMs > 200)
    {
      sel = (sel + x + NUM_CHOICES) % NUM_CHOICES;
      lastMoveMs = now;
    }
    if (isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);
      return sel;
    }
    delay(20);
  }
}

static const char *autoSlotPath() { return "/rec_auto.pcm"; }

static const char *longSlotPath(int slot)
{
  static const char *paths[2] = { "/longrec1.pcm", "/longrec2.pcm" };
  return (slot >= 1 && slot <= 2) ? paths[slot - 1] : paths[0];
}

// Returns chosen slot (1–3) or -1 if the user pushes Y to go back.
static int showSlotSubMenu(const char *action, const char *(*pathFn)(int), int maxSlots)
{
  while (isJoystickButtonPressed()) delay(10);
  int slot = 1;
  unsigned long lastMoveMs = 0;

  int prevSlot = -1;
  char hdrBuf[32];
  snprintf(hdrBuf, sizeof(hdrBuf), "%s  1/%d", action, maxSlots);

  tft.fillScreen(C_BG);
  drawHeader(hdrBuf);
  char btnLine[32];
  snprintf(btnLine, sizeof(btnLine), "Btn: %s", action);
  drawHints("< >: pick   Y: back", btnLine);

  while (true)
  {
    if (slot != prevSlot)
    {
      prevSlot = slot;
      // Update header slot number
      snprintf(hdrBuf, sizeof(hdrBuf), "%s  %d/%d", action, slot, maxSlots);
      fillGradH(0, 0, TFT_W, 36, 0, 55, 140, 0, 15, 65);
      tft.fillRect(0, 0, 4, 36, TFT_CYAN);
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(2);
      tft.setCursor(10, 10);
      tft.print(hdrBuf);

      File f = LittleFS.open(pathFn(slot), "r");
      char status[32];
      if (f)
      {
        int32_t n = 0;
        f.read((uint8_t *)&n, sizeof(n));
        f.close();
        snprintf(status, sizeof(status), "%ds recording", n / SAMPLE_RATE);
      }
      else
      {
        snprintf(status, sizeof(status), "empty");
      }
      tft.fillRect(4, 50, TFT_W - 8, 22, C_BG);
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(2);
      tft.setCursor(8, 60);
      tft.print(status);
    }

    int x = readJoystickAxis(JOY_X_PIN);
    int y = readJoystickAxis(JOY_Y_PIN);
    unsigned long now = millis();

    if (x != 0 && now - lastMoveMs > 200)
    {
      slot += x;
      if (slot < 1) slot = maxSlots;
      if (slot > maxSlots) slot = 1;
      lastMoveMs = now;
    }

    if (y != 0) return -1;

    if (isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);
      return slot;
    }
    delay(20);
  }
}

static void saveRecordingAuto()
{
  const char *path = autoSlotPath();
  File f = LittleFS.open(path, "w");
  if (!f) return;

  int32_t count = active_sample_count;
  f.write((uint8_t *)&count, sizeof(count));

  const int CHUNK = 256;
  for (int i = 0; i < active_sample_count; i += CHUNK)
  {
    int n = min(CHUNK, active_sample_count - i);
    f.write((uint8_t *)(record_buffer + i), n * sizeof(int16_t));
  }
  f.close();
}

static bool loadRecordingAuto()
{
  const char *path = autoSlotPath();
  File f = LittleFS.open(path, "r");
  if (!f) return false;

  int32_t count = 0;
  if (f.read((uint8_t *)&count, sizeof(count)) != sizeof(count) ||
      count <= 0 || count > SAMPLE_RATE * g_max_record_secs)
  {
    f.close(); return false;
  }

  size_t want = count * sizeof(int16_t);
  size_t got  = f.read((uint8_t *)record_buffer, want);
  f.close();

  if (got < want) return false;

  active_sample_count = count;
  g_has_recording = true;
  return true;
}

static void recordToLittleFS(int durationSecs, const char *path)
{
  if (!LittleFS.begin(true)) { drawStatus("FS Error", "LittleFS failed"); delay(1500); return; }

  size_t needed = sizeof(int32_t) + (size_t)durationSecs * SAMPLE_RATE * sizeof(int16_t);
  // Remove old recording first so its blocks are counted as free space
  if (LittleFS.exists(path)) LittleFS.remove(path);
  size_t avail  = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (avail < needed)
  {
    char msg[32];
    snprintf(msg, sizeof(msg), "Need %luKB, have %luKB", (unsigned long)needed/1024, (unsigned long)avail/1024);
    drawStatus("Not enough space", msg);
    delay(2000);
    return;
  }

  File f = LittleFS.open(path, "w");
  if (!f) { drawStatus("File error", "Cannot open"); delay(1500); return; }

  int32_t plannedSamples = durationSecs * SAMPLE_RATE;
  f.write((uint8_t *)&plannedSamples, sizeof(plannedSamples));

  const int STAGE = 512;
  int16_t stage[STAGE];
  int32_t written = 0;
  unsigned long startMs = millis();

  while (written < plannedSamples)
  {
    // Draw progress every ~0.5s of audio
    if (written % (SAMPLE_RATE / 2) < STAGE)
    {
      int elapsed = (int)(written / SAMPLE_RATE);
      {
        const int16_t BX = 8, BY = 46, BW = TFT_W - 16, BH = 28;
        tft.fillScreen(C_BG);
        drawHeader("Stored Rec");
        drawProgressBar(BX, BY, BW, BH, (float)written / plannedSamples);
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%ds / %ds", elapsed, durationSecs);
        tft.setTextColor(TFT_CYAN);
        tft.setTextSize(2);
        tft.setCursor(BX, BY + BH + 10);
        tft.print(timeBuf);
        drawHints("Btn: stop early");
      }
    }

    int toRead = min((int32_t)STAGE, plannedSamples - written);
    size_t bytesRead = 0;
    i2s_read(I2S_RX_PORT, stage, toRead * sizeof(int16_t), &bytesRead, portMAX_DELAY);
    int samplesRead = bytesRead / sizeof(int16_t);
    if (samplesRead > 0)
    {
      f.write((uint8_t *)stage, samplesRead * sizeof(int16_t));
      written += samplesRead;
    }

    if (isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);
      break;
    }
    yield();
  }

  f.close();

  // Fix the header if we stopped early
  if (written < plannedSamples)
  {
    File f2 = LittleFS.open(path, "r+");
    if (f2) { f2.seek(0); f2.write((uint8_t *)&written, sizeof(written)); f2.close(); }
  }

  playRecordingDoneBlips();
  char result[32];
  snprintf(result, sizeof(result), "%ds saved", written / SAMPLE_RATE);
  drawStatus("Long rec done!", result);
  delay(1200);
}

// fx: 0=plain 1=echo 2=star fighter 3=tremolo 4=chorus
static void playFromLittleFSWithEffect(int fx, const char *path)
{
  if (!LittleFS.begin(true)) { drawStatus("FS Error", "LittleFS failed"); delay(1500); return; }

  File f = LittleFS.open(path, "r");
  if (!f) { drawStatus("No long rec", "Record first"); delay(1500); return; }

  int32_t headerSamples = 0;
  if (f.read((uint8_t *)&headerSamples, sizeof(headerSamples)) != sizeof(headerSamples) || headerSamples <= 0)
  {
    f.close(); drawStatus("Bad file", "Corrupt"); delay(1500); return;
  }
  int32_t fileSamples = (f.size() - sizeof(int32_t)) / sizeof(int16_t);
  int32_t totalSamples = min(headerSamples, fileSamples);
  if (g_waveform_visible)
  {
    g_wf_total = totalSamples;
    fillStoredWaveformBuf(f, totalSamples);
    g_wf_use_peaks = true;
    drawWaveformScreen("Stored Play");
  }

  // Pitch Up — granular synthesis, duration-preserving (early return)
  if (fx == 5)
  {
    int32_t played = 0;
    const int   PB_LEN   = 2048;  // ring buffer (~186ms)
    const int   PB_GRAIN = 441;   // jump distance (~40ms)
    const int   PB_FADE  = 48;    // crossfade length
    const int   PB_GAP   = 100;   // min read-to-write gap
    const float PB_RATE  = 1.5f;  // pitch factor (1 fifth up)

    int16_t *ring = (int16_t *)calloc(PB_LEN, sizeof(int16_t));
    if (!ring)
    {
      // fallback: plain playback
      int16_t pb[128];
      while (played < totalSamples)
      {
        int32_t rem = totalSamples - played;
        int n = (int)(rem < 128 ? rem : 128);
        int got = (int)(f.read((uint8_t *)pb, n * sizeof(int16_t)) / sizeof(int16_t));
        if (got == 0) break;
        for (int i = 0; i < got; ++i) pb[i] = applyPlaybackGain(pb[i]);
        size_t bw = 0;
        i2s_write(I2S_TX_PORT, pb, got * sizeof(int16_t), &bw, portMAX_DELAY);
        played += got;
        if (g_waveform_visible) drawWaveformPlayhead((int)played);
        if (isJoystickButtonPressed()) break;
      }
      f.close();
      stopTxAndFlush();
      return;
    }

    // File staging
    const int FS = 128;
    int16_t   fstage[FS];
    int       fstageIdx = 0, fstageLen = 0;
    int32_t   fileConsumed = 0;

    int32_t pitchWPos  = 0;
    float   pitchRPos  = 0.0f;
    float   pitchRPos2 = 0.0f;
    int     pitchFade  = 0;

    // Pre-fill ring buffer before starting output
    while (pitchWPos < PB_GRAIN + PB_GAP && fileConsumed < totalSamples)
    {
      if (fstageIdx >= fstageLen)
      {
        int32_t _rem = totalSamples - fileConsumed;
        int want = (int)(_rem < FS ? _rem : FS);
        fstageLen = (int)(f.read((uint8_t *)fstage, want * sizeof(int16_t)) / sizeof(int16_t));
        fstageIdx = 0;
        if (fstageLen == 0) break;
      }
      ring[pitchWPos % PB_LEN] = fstage[fstageIdx++];
      pitchWPos++;
      fileConsumed++;
    }

    int16_t outBuf[FS];
    int     outIdx   = 0;
    bool    cancelled = false;

    while (played < totalSamples && !cancelled)
    {
      // Keep ring buffer filled at least PB_GRAIN + PB_GAP ahead of read pointer
      while ((pitchWPos - (int)pitchRPos) < PB_GRAIN + PB_GAP && fileConsumed < totalSamples)
      {
        if (fstageIdx >= fstageLen)
        {
          int32_t _rem = totalSamples - fileConsumed;
        int want = (int)(_rem < FS ? _rem : FS);
          fstageLen = (int)(f.read((uint8_t *)fstage, want * sizeof(int16_t)) / sizeof(int16_t));
          fstageIdx = 0;
          if (fstageLen == 0) break;
        }
        ring[pitchWPos % PB_LEN] = fstage[fstageIdx++];
        pitchWPos++;
        fileConsumed++;
      }

      // Generate one output sample with linear interpolation
      float outF;
      if (pitchFade > 0)
      {
        float alpha = (float)pitchFade / PB_FADE;
        int i0 = (int)pitchRPos  % PB_LEN;
        float fr1 = pitchRPos  - floorf(pitchRPos);
        float s1  = (1.0f - fr1) * ring[i0] + fr1 * ring[(i0 + 1) % PB_LEN];
        int j0 = (int)pitchRPos2 % PB_LEN;
        float fr2 = pitchRPos2 - floorf(pitchRPos2);
        float s2  = (1.0f - fr2) * ring[j0] + fr2 * ring[(j0 + 1) % PB_LEN];
        outF = (1.0f - alpha) * s1 + alpha * s2;  // old pos fades out
        pitchRPos2 += PB_RATE;
        pitchFade--;
      }
      else
      {
        int i0 = (int)pitchRPos % PB_LEN;
        float fr = pitchRPos - floorf(pitchRPos);
        outF = (1.0f - fr) * ring[i0] + fr * ring[(i0 + 1) % PB_LEN];
      }

      pitchRPos += PB_RATE;

      // Jump read pointer back one grain when it catches the write pointer
      if ((pitchWPos - (int)pitchRPos) < PB_GAP)
      {
        pitchRPos2 = pitchRPos;
        pitchRPos -= (float)PB_GRAIN;
        pitchFade  = PB_FADE;
      }

      // Normalize absolute positions periodically to prevent float precision drift
      if (pitchWPos > PB_LEN * 16)
      {
        int sub = (pitchWPos / PB_LEN - 8) * PB_LEN;
        pitchWPos -= sub;
        pitchRPos  -= (float)sub;
        pitchRPos2 -= (float)sub;
      }

      int32_t out = (int32_t)outF;
      if (out > INT16_MAX) out = INT16_MAX;
      if (out < INT16_MIN) out = INT16_MIN;
      outBuf[outIdx++] = applyPlaybackGain((int16_t)out);

      if (outIdx >= FS)
      {
        size_t bw = 0;
        i2s_write(I2S_TX_PORT, outBuf, outIdx * sizeof(int16_t), &bw, portMAX_DELAY);
        outIdx = 0;
        if (g_waveform_visible) drawWaveformPlayhead((int)played);
        if (isJoystickButtonPressed()) cancelled = true;
      }
      played++;
    }

    if (outIdx > 0)
    {
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, outBuf, outIdx * sizeof(int16_t), &bw, portMAX_DELAY);
    }

    free(ring);
    f.close();
    stopTxAndFlush();
    return;
  }

  // Pitch Down — granular synthesis, duration-preserving (early return)
  if (fx == 6)
  {
    int32_t played = 0;
    const int   PB_LEN   = 2048;
    const int   PB_GRAIN = 441;
    const int   PB_FADE  = 48;
    const int   PB_GAP   = 100;
    const float PB_RATE  = 0.67f;  // pitch factor (1 fifth down)

    int16_t *ring = (int16_t *)calloc(PB_LEN, sizeof(int16_t));
    if (!ring)
    {
      int16_t pb[128];
      while (played < totalSamples)
      {
        int32_t rem = totalSamples - played;
        int n = (int)(rem < 128 ? rem : 128);
        int got = (int)(f.read((uint8_t *)pb, n * sizeof(int16_t)) / sizeof(int16_t));
        if (got == 0) break;
        for (int i = 0; i < got; ++i) pb[i] = applyPlaybackGain(pb[i]);
        size_t bw = 0;
        i2s_write(I2S_TX_PORT, pb, got * sizeof(int16_t), &bw, portMAX_DELAY);
        played += got;
        if (g_waveform_visible) drawWaveformPlayhead((int)played);
        if (isJoystickButtonPressed()) break;
      }
      f.close();
      stopTxAndFlush();
      return;
    }

    const int FS = 128;
    int16_t   fstage[FS];
    int       fstageIdx = 0, fstageLen = 0;
    int32_t   fileConsumed = 0;

    int32_t pitchWPos  = 0;
    float   pitchRPos  = 0.0f;
    float   pitchRPos2 = 0.0f;
    int     pitchFade  = 0;

    // Pre-fill ring buffer
    while (pitchWPos < PB_GRAIN + PB_GAP && fileConsumed < totalSamples)
    {
      if (fstageIdx >= fstageLen)
      {
        int32_t _rem = totalSamples - fileConsumed;
        int want = (int)(_rem < FS ? _rem : FS);
        fstageLen = (int)(f.read((uint8_t *)fstage, want * sizeof(int16_t)) / sizeof(int16_t));
        fstageIdx = 0;
        if (fstageLen == 0) break;
      }
      ring[pitchWPos % PB_LEN] = fstage[fstageIdx++];
      pitchWPos++;
      fileConsumed++;
    }

    int16_t outBuf[FS];
    int     outIdx   = 0;
    bool    cancelled = false;

    while (played < totalSamples && !cancelled)
    {
      // Consume exactly one file sample per output sample so the gap grows
      // at (1 - PB_RATE) per step, triggering a forward jump every ~1336 samples.
      if (fileConsumed < totalSamples)
      {
        if (fstageIdx >= fstageLen)
        {
          int32_t _rem = totalSamples - fileConsumed;
          int want = (int)(_rem < FS ? _rem : FS);
          fstageLen = (int)(f.read((uint8_t *)fstage, want * sizeof(int16_t)) / sizeof(int16_t));
          fstageIdx = 0;
        }
        if (fstageIdx < fstageLen)
        {
          ring[pitchWPos % PB_LEN] = fstage[fstageIdx++];
          pitchWPos++;
          fileConsumed++;
        }
      }

      float outF;
      if (pitchFade > 0)
      {
        float alpha = (float)pitchFade / PB_FADE;
        int i0 = (int)pitchRPos  % PB_LEN;
        float fr1 = pitchRPos  - floorf(pitchRPos);
        float s1  = (1.0f - fr1) * ring[i0] + fr1 * ring[(i0 + 1) % PB_LEN];
        int j0 = (int)pitchRPos2 % PB_LEN;
        float fr2 = pitchRPos2 - floorf(pitchRPos2);
        float s2  = (1.0f - fr2) * ring[j0] + fr2 * ring[(j0 + 1) % PB_LEN];
        outF = (1.0f - alpha) * s1 + alpha * s2;
        pitchRPos2 += PB_RATE;
        pitchFade--;
      }
      else
      {
        int i0 = (int)pitchRPos % PB_LEN;
        float fr = pitchRPos - floorf(pitchRPos);
        outF = (1.0f - fr) * ring[i0] + fr * ring[(i0 + 1) % PB_LEN];
      }

      pitchRPos += PB_RATE;

      // Jump read pointer forward when write gets too far ahead (gap > half ring)
      if ((pitchWPos - (int)pitchRPos) > PB_LEN / 2)
      {
        pitchRPos2 = pitchRPos;
        pitchRPos += (float)PB_GRAIN;
        pitchFade  = PB_FADE;
      }

      if (pitchWPos > PB_LEN * 16)
      {
        int sub = (pitchWPos / PB_LEN - 8) * PB_LEN;
        pitchWPos -= sub;
        pitchRPos  -= (float)sub;
        pitchRPos2 -= (float)sub;
      }

      int32_t out = (int32_t)outF;
      if (out > INT16_MAX) out = INT16_MAX;
      if (out < INT16_MIN) out = INT16_MIN;
      outBuf[outIdx++] = applyPlaybackGain((int16_t)out);

      if (outIdx >= FS)
      {
        size_t bw = 0;
        i2s_write(I2S_TX_PORT, outBuf, outIdx * sizeof(int16_t), &bw, portMAX_DELAY);
        outIdx = 0;
        if (g_waveform_visible) drawWaveformPlayhead((int)played);
        if (isJoystickButtonPressed()) cancelled = true;
      }
      played++;
    }

    if (outIdx > 0)
    {
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, outBuf, outIdx * sizeof(int16_t), &bw, portMAX_DELAY);
    }

    free(ring);
    f.close();
    stopTxAndFlush();
    return;
  }

  // Echo: 250ms circular delay buffer (auto-allocated from PSRAM if available)
  const int ECHO_LEN = (int)(0.25f * SAMPLE_RATE);
  int16_t *echoBuf = nullptr;
  int echoWr = 0;
  if (fx == 1)
  {
    echoBuf = (int16_t *)calloc(ECHO_LEN, sizeof(int16_t));
    if (!echoBuf) fx = 0;
  }

  // Chorus: 50ms delay buffer, LFO sweeps read point 10–30ms back
  const int CHORUS_LEN = (int)(0.05f * SAMPLE_RATE) + 1;
  int16_t *chorusBuf = nullptr;
  int chorusWr = 0;
  float chorusPhase = 0.0f;
  if (fx == 4)
  {
    chorusBuf = (int16_t *)calloc(CHORUS_LEN, sizeof(int16_t));
    if (!chorusBuf) fx = 0;
  }

  // Phase accumulators for math-only effects
  float ringPhase = 0.0f;
  float tremPhase = 0.0f;
  const float RING_FREQ  = 50.0f;
  const float TREM_RATE  = 6.0f;
  const float TREM_DEPTH = 0.85f;

  const int CHUNK = 256;
  int16_t chunk[CHUNK];
  int32_t played = 0;

  while (played < totalSamples)
  {
    int toRead = min((int32_t)CHUNK, totalSamples - played);
    size_t bytesRead = f.read((uint8_t *)chunk, toRead * sizeof(int16_t));
    int samplesRead = bytesRead / sizeof(int16_t);
    if (samplesRead == 0) break;

    for (int i = 0; i < samplesRead; ++i)
    {
      int32_t s = chunk[i];

      if (fx == 1)  // echo
      {
        int32_t echo = echoBuf[echoWr];
        int32_t out  = s + (int32_t)(echo * 0.4f);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        echoBuf[echoWr] = (int16_t)out;
        echoWr = (echoWr + 1) % ECHO_LEN;
        s = out;
      }
      else if (fx == 2)  // star fighter ring mod
      {
        float mod = sinf(2.0f * 3.14159265f * ringPhase);
        ringPhase += RING_FREQ / SAMPLE_RATE;
        if (ringPhase >= 1.0f) ringPhase -= 1.0f;
        int32_t out = (int32_t)((float)s * mod);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }
      else if (fx == 3)  // tremolo
      {
        float env = (1.0f - TREM_DEPTH) + TREM_DEPTH * (0.5f + 0.5f * sinf(2.0f * 3.14159265f * tremPhase));
        tremPhase += TREM_RATE / SAMPLE_RATE;
        if (tremPhase >= 1.0f) tremPhase -= 1.0f;
        int32_t out = (int32_t)((float)s * env);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }
      else if (fx == 4)  // chorus
      {
        float lfo = 0.5f + 0.5f * sinf(2.0f * 3.14159265f * chorusPhase);
        chorusPhase += 0.5f / SAMPLE_RATE;
        if (chorusPhase >= 1.0f) chorusPhase -= 1.0f;
        int delaySmp = (int)(0.010f * SAMPLE_RATE + lfo * 0.020f * SAMPLE_RATE);
        int readIdx  = (chorusWr - delaySmp + CHORUS_LEN) % CHORUS_LEN;
        int16_t del  = chorusBuf[readIdx];
        chorusBuf[chorusWr] = (int16_t)s;
        chorusWr = (chorusWr + 1) % CHORUS_LEN;
        int32_t out = (int32_t)(0.6f * s + 0.6f * del);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }

      chunk[i] = applyPlaybackGain((int16_t)s);
    }

    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, samplesRead * sizeof(int16_t), &bw, portMAX_DELAY);
    played += samplesRead;
    if (g_waveform_visible) drawWaveformPlayhead((int)played);

    if (isJoystickButtonPressed()) break;
  }

  f.close();
  if (echoBuf)   free(echoBuf);
  if (chorusBuf) free(chorusBuf);
  stopTxAndFlush();
}

static int showStorageSubMenu()
{
  static int sel = MENU_ROOT_COUNT;
  unsigned long lastMoveMs = 0;
  const int storageCount = MENU_STORAGE_COUNT - MENU_ROOT_COUNT;
  while (isJoystickButtonPressed()) delay(10);

  int prevSel = -1;

  while (true)
  {
    if (sel != prevSel)
    {
      prevSel = sel;
      tft.fillScreen(C_BG);
      tft.setTextSize(2);

      for (int i = 0; i < storageCount; ++i)
      {
        int itemEnum = MENU_ROOT_COUNT + i;
        int16_t y = i * ITEM_H;
        uint16_t iconColor;
        if (itemEnum == sel)
        {
          fillGradH(0, y, TFT_W, ITEM_H - 1, 0, 130, 190, 0, 55, 120);
          tft.fillRect(0, y, 4, ITEM_H - 1, TFT_CYAN);
          tft.setTextColor(TFT_WHITE);
          iconColor = TFT_WHITE;
        }
        else
        {
          uint16_t rc = (i & 1) ? tft.color565(12, 15, 38) : C_BG;
          tft.fillRect(0, y, TFT_W, ITEM_H - 1, rc);
          tft.setTextColor(0xDEFB);
          iconColor = 0xDEFB;
        }
        tft.drawFastHLine(0, y + ITEM_H - 1, TFT_W, tft.color565(20, 25, 55));
        tft.drawBitmap(6, y + 5, MENU_ICONS[itemEnum], 16, 16, iconColor);
        tft.setCursor(28, y + 5);
        tft.print(menuLabels[itemEnum]);
      }

      tft.setTextSize(1);
      tft.setTextColor(COL_GRAY);
      tft.setCursor(4, storageCount * ITEM_H + 4);
      tft.print("< X: back");
    }

    int y = readJoystickAxis(JOY_Y_PIN);
    int x = readJoystickAxis(JOY_X_PIN);
    unsigned long now = millis();

    if ((y != 0 || x < 0) && now - lastMoveMs > 200)
    {
      if (x < 0) return -1;
      sel += (y < 0 ? -1 : 1);
      if (sel < MENU_ROOT_COUNT) sel = MENU_ROOT_COUNT;
      if (sel >= MENU_STORAGE_COUNT) sel = MENU_STORAGE_COUNT - 1;
      lastMoveMs = now;
    }

    if (isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);
      return sel;
    }
    delay(10);
  }
}

static int showEffectsSubMenu()
{
  static int sel = MENU_STORAGE_COUNT;
  unsigned long lastMoveMs = 0;
  const int effectCount = MENU_COUNT - MENU_STORAGE_COUNT;
  while (isJoystickButtonPressed()) delay(10);

  int prevSel2 = -1;

  while (true)
  {
    if (sel != prevSel2)
    {
      prevSel2 = sel;
      const int visibleCount = 8;
      int relSel = sel - MENU_STORAGE_COUNT;
      int startIdx = relSel - visibleCount / 2;
      if (startIdx < 0) startIdx = 0;
      if (startIdx > effectCount - visibleCount) startIdx = effectCount - visibleCount;
      if (startIdx < 0) startIdx = 0;

      tft.fillScreen(C_BG);
      tft.setTextSize(2);

      for (int i = 0; i < visibleCount && (startIdx + i) < effectCount; ++i)
      {
        int itemEnum = MENU_STORAGE_COUNT + startIdx + i;
        int16_t y = i * ITEM_H;
        uint16_t iconColor;
        if (itemEnum == sel)
        {
          fillGradH(0, y, TFT_W, ITEM_H - 1, 0, 130, 190, 0, 55, 120);
          tft.fillRect(0, y, 4, ITEM_H - 1, TFT_CYAN);
          tft.setTextColor(TFT_WHITE);
          iconColor = TFT_WHITE;
        }
        else
        {
          uint16_t rc = (i & 1) ? tft.color565(12, 15, 38) : C_BG;
          tft.fillRect(0, y, TFT_W, ITEM_H - 1, rc);
          tft.setTextColor(0xDEFB);
          iconColor = 0xDEFB;
        }
        tft.drawFastHLine(0, y + ITEM_H - 1, TFT_W, tft.color565(20, 25, 55));
        tft.drawBitmap(6, y + 5, MENU_ICONS[itemEnum], 16, 16, iconColor);
        tft.setCursor(28, y + 5);
        tft.print(menuLabels[itemEnum]);
      }

      // Scroll indicators
      if (startIdx > 0)
      {
        tft.setTextColor(TFT_CYAN); tft.setTextSize(1);
        tft.setCursor(TFT_W - 10, 2);  tft.print("^");
      }
      if (startIdx + visibleCount < effectCount)
      {
        tft.setTextColor(TFT_CYAN); tft.setTextSize(1);
        tft.setCursor(TFT_W - 10, visibleCount * ITEM_H - 8); tft.print("v");
      }
      tft.setTextSize(1);
      tft.setTextColor(COL_GRAY);
      tft.setCursor(4, visibleCount * ITEM_H + 4);
      tft.print("< X: back");
    }

    int y = readJoystickAxis(JOY_Y_PIN);
    int x = readJoystickAxis(JOY_X_PIN);
    unsigned long now = millis();

    if ((y != 0 || x < 0) && now - lastMoveMs > 200)
    {
      if (x < 0) return -1;
      sel += (y < 0 ? -1 : 1);
      if (sel < MENU_STORAGE_COUNT) sel = MENU_STORAGE_COUNT;
      if (sel >= MENU_COUNT) sel = MENU_COUNT - 1;
      lastMoveMs = now;
    }

    if (isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);
      return sel;
    }
    delay(10);
  }
}

void runMenuAction(int item)
{
  while (isJoystickButtonPressed()) delay(10);

  // Block all playback effects if nothing has been recorded yet
  if (!g_has_recording && item != MENU_RECORD && item != MENU_PASSTHROUGH
      && item != MENU_LONG_REC && item != MENU_LONG_PLAY
      && item != MENU_VOLUME && item != MENU_EFFECTS && item != MENU_STORAGE)
  {
    drawStatus("No recording!", "Record first");
    delay(1500);
    drawMenu();
    return;
  }

  switch (item)
  {
    case MENU_RECORD:
    {
      int durSecs = showDurationSubMenu(active_sample_count / SAMPLE_RATE);
      active_sample_count = durSecs * SAMPLE_RATE;
      recordToBuffer();
      saveRecordingAuto();
      char durStr[32];
      snprintf(durStr, sizeof(durStr), "%ds saved  Btn:ok", durSecs);
      drawStatus("Recording done.", durStr);
      while (!isJoystickButtonPressed()) delay(50);
      break;
    }
    case MENU_PLAY:
      g_waveform_visible = true;
      drawWaveformScreen("Play");
      playBufferSimple();
      g_waveform_visible = false;
      break;
    case MENU_EFFECTS:
    {
      while (true)
      {
        int fx = showEffectsSubMenu();
        if (fx < 0) break;
        runMenuAction(fx);
      }
      break;
    }
    case MENU_STORAGE:
    {
      while (true)
      {
        int item = showStorageSubMenu();
        if (item < 0) break;
        runMenuAction(item);
      }
      break;
    }
    case MENU_PASSTHROUGH:
    {
      int fx = showPassthroughFxSubMenu();
      static const char *fxNames[] = { "Plain", "Echo", "Star Fghtr", "Tremolo", "Chorus", "Distort", "Telephone", "Pitch Up" };
      char status[32];
      snprintf(status, sizeof(status), "%s  Btn:stop", fxNames[fx]);
      drawStatus("Passthrough", status);
      passthroughWithEffect(fx);
      drawStatus("Passthrough done", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    }
    case MENU_REVERSE:
      g_waveform_visible = true;
      drawWaveformScreen("Reverse");
      playReverse();
      g_waveform_visible = false;
      break;
    case MENU_PITCH:
    {
      // range 0.3x–2.5x; level 0.318 = 1.0x (neutral/no change)
      for (;;)
      {
        float lvl = showLevelSubMenu("Pitch", "Speed", s_pitchLevel, 0.3f, 2.5f, "x");
        if (lvl < 0.0f) break;
        s_pitchLevel = lvl;
        float speed = 0.3f + s_pitchLevel * 2.2f;
        char info[32];
        snprintf(info, sizeof(info), "Speed: %.2fx", speed);
        g_waveform_visible = true;
        drawWaveformScreen("Pitch");
        playResample(speed);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_ECHO:
    {
      for (;;)
      {
        float lvl = showLevelSubMenu("Echo", "Delay", s_echoLevel, 100.0f, 500.0f, "ms");
        if (lvl < 0.0f) break;
        s_echoLevel = lvl;
        float delaySec = (100.0f + s_echoLevel * 400.0f) / 1000.0f;
        float decay = 0.2f + s_echoLevel * 0.5f;
        char info[32];
        snprintf(info, sizeof(info), "%.0fms dec %.2f", delaySec * 1000.0f, decay);
        g_waveform_visible = true;
        drawWaveformScreen("Echo");
        playEcho(delaySec, decay);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_RINGMOD:
    {
      for (;;)
      {
        float lvl = showLevelSubMenu("Star fighter", "Freq", s_ringmodLevel, 10.0f, 90.0f, "Hz");
        if (lvl < 0.0f) break;
        s_ringmodLevel = lvl;
        float freq = 10.0f + s_ringmodLevel * 80.0f;
        char info[32];
        snprintf(info, sizeof(info), "Freq: %.0fHz", freq);
        g_waveform_visible = true;
        drawWaveformScreen("Star fighter");
        playRingMod(freq);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_STUTTER:
    {
      // level 0 = subtle (long 200ms chunks, 2 repeats)
      // level 1 = heavy  (short 30ms chunks, 6 repeats)
      for (;;)
      {
        float lvl = showLevelSubMenu("Stutter", "Intensity", s_stutterLevel, 0.0f, 100.0f, "%");
        if (lvl < 0.0f) break;
        s_stutterLevel = lvl;
        float chunkSec = (200.0f - s_stutterLevel * 0.01f * 170.0f) / 1000.0f;
        int repeats = 2 + (int)(s_stutterLevel * 0.04f);
        char info[32];
        snprintf(info, sizeof(info), "%.0fms x%d", chunkSec * 1000.0f, repeats);
        g_waveform_visible = true;
        drawWaveformScreen("Stutter");
        playStutter(chunkSec, repeats);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_TREMOLO:
    {
      for (;;)
      {
        float lvl = showLevelSubMenu("Tremolo", "Rate", s_tremoloLevel, 2.0f, 15.0f, "Hz");
        if (lvl < 0.0f) break;
        s_tremoloLevel = lvl;
        float rate = 2.0f + s_tremoloLevel * 13.0f;
        char info[32];
        snprintf(info, sizeof(info), "Rate: %.1fHz", rate);
        g_waveform_visible = true;
        drawWaveformScreen("Tremolo");
        playTremolo(rate);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_HAUNTED:
    {
      for (;;)
      {
        float lvl = showLevelSubMenu("Haunted", "Echo depth", s_hauntedLevel, 10.0f, 70.0f, "%");
        if (lvl < 0.0f) break;
        s_hauntedLevel = lvl;
        float decay = 0.1f + s_hauntedLevel * 0.6f;
        g_waveform_visible = true;
        drawWaveformScreen("Haunted");
        playHaunted(0.25f, decay);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_ALIEN:
    {
      for (;;)
      {
        float lvl = showLevelSubMenu("Alien", "Ring freq", s_alienLevel, 20.0f, 80.0f, "Hz");
        if (lvl < 0.0f) break;
        s_alienLevel = lvl;
        float ringFreq = 20.0f + s_alienLevel * 60.0f;
        char info[32];
        snprintf(info, sizeof(info), "Pitch up + %.0fHz", ringFreq);
        g_waveform_visible = true;
        drawWaveformScreen("Alien");
        playAlien(1.6f, ringFreq);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_MONSTER:
    {
      for (;;)
      {
        float lvl = showLevelSubMenu("Monster", "Echo depth", s_monsterLevel, 10.0f, 70.0f, "%");
        if (lvl < 0.0f) break;
        s_monsterLevel = lvl;
        float decay = 0.1f + s_monsterLevel * 0.6f;
        g_waveform_visible = true;
        drawWaveformScreen("Monster");
        playMonster(0.5f, 0.3f, decay);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_CHORUS:
    {
      for (;;)
      {
        float lvl = showLevelSubMenu("Chorus", "Depth", s_chorusLevel, 0.0f, 100.0f, "%");
        if (lvl < 0.0f) break;
        s_chorusLevel = lvl;
        float rate  = 0.5f;
        float depth = s_chorusLevel;
        char info[32];
        snprintf(info, sizeof(info), "Rate:%.1fHz d:%.0f%%", rate, depth * 100.0f);
        g_waveform_visible = true;
        drawWaveformScreen("Chorus");
        playChorus(rate, depth);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_LONG_REC:
    {
      int slot = showSlotSubMenu("Stored Rec", longSlotPath, 2);
      if (slot < 0) break;
      g_long_rec_secs = showDurationSubMenu(g_long_rec_secs, 60);
      char durStr[32];
      snprintf(durStr, sizeof(durStr), "%ds stored...", g_long_rec_secs);
      drawStatus("Stored Rec", durStr);
      recordToLittleFS(g_long_rec_secs, longSlotPath(slot));
      break;
    }
    case MENU_LONG_PLAY:
    {
      int slot = showSlotSubMenu("Stored Play", longSlotPath, 2);
      if (slot < 0) break;
      int fx = showLongPlayFxSubMenu();
      static const char *fxNames[] = { "Plain", "Echo", "Star Fghtr", "Tremolo", "Chorus", "Pitch Up", "Pitch Dn" };
      char status[32];
      snprintf(status, sizeof(status), "%s  Btn:stop", fxNames[fx]);
      g_waveform_visible = true;
      playFromLittleFSWithEffect(fx, longSlotPath(slot));
      g_waveform_visible = false;
      g_wf_use_peaks = false;
      g_wf_total = 0;
      break;
    }
    case MENU_VOLUME:
    {
      float lvl = showLevelSubMenu("Volume", "Gain", s_volumeLevel, 0.5f, 10.0f, "x");
      if (lvl >= 0.0f)
      {
        s_volumeLevel = lvl;
        playback_gain = 0.5f + lvl * 9.5f;
        g_prefs.putFloat("vol_gain", playback_gain);
        char info[32];
        snprintf(info, sizeof(info), "Gain: %.2fx saved", playback_gain);
        drawStatus("Volume saved!", info);
        delay(1000);
      }
      break;
    }
    default:
      break;
  }
  drawMenu();
}

void initI2S()
{
  // RX config (microphone)
  i2s_config_t i2s_rx_config = {};
  i2s_rx_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2s_rx_config.sample_rate = SAMPLE_RATE;
  i2s_rx_config.bits_per_sample = (i2s_bits_per_sample_t)I2S_BITS_PER_SAMPLE_16BIT;
  i2s_rx_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2s_rx_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_rx_config.dma_buf_count = 8;
  i2s_rx_config.dma_buf_len = 1024;
  i2s_rx_config.use_apll = false;

  // TX config (DAC / amplifier)
  i2s_config_t i2s_tx_config = {};
  i2s_tx_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_tx_config.sample_rate = SAMPLE_RATE;
  i2s_tx_config.bits_per_sample = (i2s_bits_per_sample_t)I2S_BITS_PER_SAMPLE_16BIT;
  i2s_tx_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; // mono output for MAX98357A
  i2s_tx_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_tx_config.dma_buf_count = 8;
  i2s_tx_config.dma_buf_len = 256;
  i2s_tx_config.use_apll = false;

  // Install drivers
  esp_err_t ret_rx = i2s_driver_install(I2S_RX_PORT, &i2s_rx_config, 0, NULL);
  esp_err_t ret_tx = i2s_driver_install(I2S_TX_PORT, &i2s_tx_config, 0, NULL);
  
  if (ret_rx != ESP_OK)
    Serial.printf("ERROR: I2S RX driver install failed (0x%X)\n", ret_rx);
  else
    Serial.println("✓ I2S RX driver installed");
    
  if (ret_tx != ESP_OK)
    Serial.printf("ERROR: I2S TX driver install failed (0x%X)\n", ret_tx);
  else
    Serial.println("✓ I2S TX driver installed");

  // Pin config - INMP441 (RX - microphone)
  i2s_pin_config_t pin_config_rx = {};
  pin_config_rx.bck_io_num = I2S_RX_BCK;
  pin_config_rx.ws_io_num = I2S_RX_WS;
  pin_config_rx.data_out_num = I2S_PIN_NO_CHANGE;
  pin_config_rx.data_in_num = I2S_RX_SD;
  
  esp_err_t pin_ret_rx = i2s_set_pin(I2S_RX_PORT, &pin_config_rx);
  if (pin_ret_rx != ESP_OK)
    Serial.printf("ERROR: I2S RX pins failed (0x%X)\n", pin_ret_rx);
  else
    Serial.printf("✓ INMP441 (RX): BCLK=%d, LRCLK=%d, SD=%d\n", I2S_RX_BCK, I2S_RX_WS, I2S_RX_SD);

  // Pin config - MAX98357A (TX - amplifier)
  i2s_pin_config_t pin_config_tx = {};
  pin_config_tx.bck_io_num = I2S_TX_BCK;
  pin_config_tx.ws_io_num = I2S_TX_WS;
  pin_config_tx.data_out_num = I2S_TX_SD;
  pin_config_tx.data_in_num = I2S_PIN_NO_CHANGE;
  
  esp_err_t pin_ret_tx = i2s_set_pin(I2S_TX_PORT, &pin_config_tx);
  if (pin_ret_tx != ESP_OK)
    Serial.printf("ERROR: I2S TX pins failed (0x%X)\n", pin_ret_tx);
  else
    Serial.printf("✓ MAX98357A (TX): BCLK=%d, LRCLK=%d, DIN=%d\n", I2S_TX_BCK, I2S_TX_WS, I2S_TX_SD);

  i2s_zero_dma_buffer(I2S_RX_PORT);
  i2s_zero_dma_buffer(I2S_TX_PORT);
}

static inline int16_t applyPlaybackGain(int16_t sample)
{
  int32_t scaled = (int32_t)(sample * playback_gain);
  if (scaled > INT16_MAX) scaled = INT16_MAX;
  else if (scaled < INT16_MIN) scaled = INT16_MIN;
  return (int16_t)scaled;
}

static void stopTxAndFlush()
{
  i2s_stop(I2S_TX_PORT);
  i2s_zero_dma_buffer(I2S_TX_PORT);
  i2s_start(I2S_TX_PORT);
}

// Display /splash.raw from LittleFS (320×240 raw RGB565, little-endian uint16_t).
// LittleFS f.read() is a single block DMA read — avoids the pgm_read_word
// word-by-word flash cache stall that caused WDT reboots with PROGMEM.
static void showColorCheck()
{
  struct { const char *label; uint16_t color; } swatches[] = {
    { "RED",     TFT_RED     },
    { "GREEN",   TFT_GREEN   },
    { "BLUE",    TFT_BLUE    },
    { "WHITE",   TFT_WHITE   },
    { "BLACK",   TFT_BLACK   },
    { "YELLOW",  TFT_YELLOW  },
    { "CYAN",    TFT_CYAN    },
    { "MAGENTA", TFT_MAGENTA },
  };
  const int N = sizeof(swatches) / sizeof(swatches[0]);
  const int blockH = TFT_H / N;

  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < N; ++i) {
    int y = i * blockH;
    tft.fillRect(0, y, TFT_W, blockH, swatches[i].color);
    uint16_t textColor = (swatches[i].color == TFT_BLACK || swatches[i].color == TFT_BLUE) ? TFT_WHITE : TFT_BLACK;
    tft.setTextColor(textColor);
    tft.setTextSize(2);
    tft.setCursor(8, y + (blockH - 16) / 2);
    tft.print(swatches[i].label);
  }
  delay(5000);
}

static void showSplash()
{
  File f = LittleFS.open("/splash.raw", "r");
  if (!f) return;

  uint16_t rowBuf[TFT_W];
  tft.startWrite();
  tft.setAddrWindow(0, 0, TFT_W, TFT_H);
  for (int row = 0; row < TFT_H; ++row)
  {
    if (f.read((uint8_t *)rowBuf, TFT_W * 2) != TFT_W * 2) break;
    tft.pushColors(rowBuf, TFT_W);
    if ((row & 31) == 31) yield();
  }
  tft.endWrite();
  f.close();
}

static void playStartupWav()
{
  if (!LittleFS.begin(true))
  {
    Serial.println("LittleFS mount failed — skipping startup sound");
    return;
  }

  File f = LittleFS.open("/voicemorpher.wav", "r");
  if (!f)
  {
    Serial.println("voicemorpher.wav not found in LittleFS");
    return;
  }

  // Parse RIFF header
  uint8_t riff[12];
  if (f.read(riff, 12) != 12 || riff[0] != 'R' || riff[1] != 'I')
  {
    Serial.println("Startup WAV: invalid RIFF header");
    f.close(); return;
  }

  // Scan chunks for fmt and data
  bool foundData = false;
  uint32_t sz = 0;
  uint8_t chunkHdr[8];
  while (f.read(chunkHdr, 8) == 8)
  {
    sz = (uint32_t)chunkHdr[4] | ((uint32_t)chunkHdr[5] << 8) |
         ((uint32_t)chunkHdr[6] << 16) | ((uint32_t)chunkHdr[7] << 24);
    if (chunkHdr[0] == 'f' && chunkHdr[1] == 'm' && chunkHdr[2] == 't')
    {
      uint8_t fmt[16];
      f.read(fmt, 16);
      if (sz > 16) f.seek(sz - 16, SeekCur);
    }
    else if (chunkHdr[0] == 'd' && chunkHdr[1] == 'a' && chunkHdr[2] == 't' && chunkHdr[3] == 'a')
    {
      foundData = true;
      break; // sz holds the data chunk size — used below to limit playback
    }
    else
    {
      f.seek(sz, SeekCur);
    }
  }

  if (!foundData)
  {
    Serial.println("Startup WAV: no data chunk found");
    f.close(); return;
  }

  showSplash();
  Serial.println("Playing startup WAV...");

  float savedGain = playback_gain;
  if (playback_gain > STARTUP_WAV_MAX_GAIN) playback_gain = STARTUP_WAV_MAX_GAIN;

  int16_t buf[256];
  size_t bytesRead;
  uint32_t bytesRemaining = sz; // limit to data chunk — stops before trailing metadata
  while (bytesRemaining > 0 && (bytesRead = f.read((uint8_t *)buf, min((size_t)bytesRemaining, sizeof(buf)))) > 0)
  {
    bytesRemaining -= bytesRead;
    size_t samples = bytesRead / sizeof(int16_t);
    for (size_t i = 0; i < samples; ++i)
      buf[i] = applyPlaybackGain(buf[i]);
    size_t bytesWritten = 0;
    i2s_write(I2S_TX_PORT, buf, samples * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
  }

  f.close();
  stopTxAndFlush();
  playback_gain = savedGain;
  Serial.println("Startup WAV done.");
}

static void writeSamplesWithGain(const int16_t *src, size_t sampleCount)
{
  const size_t CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  size_t offset = 0;

  while (offset < sampleCount)
  {
    size_t chunkCount = min(CHUNK_SAMPLES, sampleCount - offset);
    for (size_t i = 0; i < chunkCount; ++i)
    {
      chunk[i] = applyPlaybackGain(src[offset + i]);
    }

    size_t bytes_written = 0;
    esp_err_t ret = i2s_write(I2S_TX_PORT, chunk, chunkCount * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    if (ret != ESP_OK)
    {
      Serial.printf("ERROR: I2S write failed during playback (0x%X)\n", ret);
      return;
    }

    size_t written_samples = bytes_written / sizeof(int16_t);
    if (written_samples == 0)
    {
      Serial.println("ERROR: I2S write returned zero samples written");
      return;
    }
    offset += written_samples;
    if (g_waveform_visible) drawWaveformPlayhead((int)offset);
    if (isJoystickButtonPressed()) return;
  }
}

void cleanRecording()
{
#if ENABLE_AUDIO_CLEANING
  // Simple smoothing filter for recorded data to reduce crackle.
  int16_t prev = record_buffer[0];
  for (int i = 1; i < active_sample_count - 1; ++i)
  {
    int32_t next = record_buffer[i + 1];
    int32_t cur = record_buffer[i];
    int32_t filtered = (prev + cur + next) / 3;
    if (AUDIO_CLEANING_STRENGTH > 1)
    {
      filtered = (filtered + cur) / 2;
    }
    prev = record_buffer[i];
    record_buffer[i] = (int16_t)filtered;
  }
#endif
}

void normalizeRecording(float targetPeak = 0.95f)
{
  int32_t maxSample = 0;
  int32_t minSample = 0;
  for (int i = 0; i < active_sample_count; ++i)
  {
    int32_t s = record_buffer[i];
    if (s > maxSample) maxSample = s;
    if (s < minSample) minSample = s;
  }

  Serial.printf("Recording peak: max=%ld  min=%ld\n", (long)maxSample, (long)minSample);

  int32_t absPeak = max(maxSample, -minSample);
  if (absPeak <= 0)
  {
    Serial.println("WARNING: silent recording — skipping normalization");
    return;
  }

  float scale = (INT16_MAX * targetPeak) / (float)absPeak;
  Serial.printf("Normalization scale: %.2fx\n", scale);
  if (scale > 10.0f)
  {
    Serial.println("WARNING: scale > 10 — mic signal very weak, capping to prevent noise amplification");
    scale = 10.0f;
  }
  if (scale <= 1.0f) return;

  for (int i = 0; i < active_sample_count; ++i)
  {
    int32_t scaled = int32_t(record_buffer[i] * scale);
    if (scaled > INT16_MAX) scaled = INT16_MAX;
    else if (scaled < INT16_MIN) scaled = INT16_MIN;
    record_buffer[i] = (int16_t)scaled;
  }
}

static void playRecordingDoneBlips()
{
  const float FREQ = 1000.0f;
  const int BLIP_SAMPLES = (int)(0.08f * SAMPLE_RATE);
  const int GAP_SAMPLES  = (int)(0.07f * SAMPLE_RATE);
  const int CHUNK = 64;
  int16_t chunk[CHUNK];

  for (int blip = 0; blip < 3; ++blip)
  {
    for (int s = 0; s < BLIP_SAMPLES; s += CHUNK)
    {
      int n = min(CHUNK, BLIP_SAMPLES - s);
      int fadeStart = (int)(BLIP_SAMPLES * 0.8f);
      for (int i = 0; i < n; ++i)
      {
        float t = (float)(s + i) / SAMPLE_RATE;
        float env = (s + i >= fadeStart)
                    ? 1.0f - (float)(s + i - fadeStart) / (BLIP_SAMPLES - fadeStart)
                    : 1.0f;
        chunk[i] = (int16_t)(sinf(2.0f * 3.14159265f * FREQ * t) * 16000.0f * env);
      }
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, n * sizeof(int16_t), &bw, portMAX_DELAY);
    }
    if (blip < 2)
    {
      memset(chunk, 0, sizeof(chunk));
      for (int s = 0; s < GAP_SAMPLES; s += CHUNK)
      {
        int n = min(CHUNK, GAP_SAMPLES - s);
        size_t bw = 0;
        i2s_write(I2S_TX_PORT, chunk, n * sizeof(int16_t), &bw, portMAX_DELAY);
      }
    }
  }
  stopTxAndFlush();
}

void recordToBuffer()
{
  const size_t TEMP_SAMPLES = 256;
  int16_t temp[TEMP_SAMPLES]; // 16-bit to match I2S_BITS_PER_SAMPLE_16BIT
  size_t sample_offset = 0;
  Serial.printf("Recording %d seconds (%d samples)...\n", active_sample_count / SAMPLE_RATE, active_sample_count);

  const int16_t BX = 8, BY = 46, BW = TFT_W - 16, BH = 28;
  int totalSecs = active_sample_count / SAMPLE_RATE;

  while (sample_offset < active_sample_count)
  {
    // Refresh display roughly every half-second of audio
    if (sample_offset % (SAMPLE_RATE / 2) < TEMP_SAMPLES)
    {
      tft.fillScreen(C_BG);
      drawHeader("Recording...");
      drawProgressBar(BX, BY, BW, BH, (float)sample_offset / active_sample_count);
      int elapsed = (int)(sample_offset / SAMPLE_RATE);
      int remaining = totalSecs - elapsed;
      char timeBuf[32];
      snprintf(timeBuf, sizeof(timeBuf), "%ds remaining", remaining);
      tft.setTextColor(TFT_CYAN);
      tft.setTextSize(2);
      tft.setCursor(BX, BY + BH + 10);
      tft.print(timeBuf);
    }

    size_t samples_to_read = min(TEMP_SAMPLES, (size_t)(active_sample_count - sample_offset));
    size_t bytes_to_read = samples_to_read * sizeof(int16_t);
    size_t bytes_read = 0;

    i2s_read(I2S_RX_PORT, temp, bytes_to_read, &bytes_read, portMAX_DELAY);
    size_t read_samples = bytes_read / sizeof(int16_t);
    if (read_samples == 0)
      continue;

    for (size_t i = 0; i < read_samples && sample_offset < active_sample_count; ++i)
    {
      record_buffer[sample_offset++] = temp[i];
    }
    yield();
  }

  Serial.printf("Recording complete. First 8 samples: %d %d %d %d %d %d %d %d\n",
    record_buffer[0], record_buffer[1], record_buffer[2], record_buffer[3],
    record_buffer[4], record_buffer[5], record_buffer[6], record_buffer[7]);
  cleanRecording();
  normalizeRecording();
  g_has_recording = true;
  playRecordingDoneBlips();
}

void playBufferSimple()
{
  // Check if buffer has any non-zero samples
  int32_t sum = 0;
  int nonzero_count = 0;
  for (int i = 0; i < active_sample_count; ++i)
  {
    if (record_buffer[i] != 0) nonzero_count++;
    sum += abs(record_buffer[i]);
  }
  
  if (nonzero_count == 0)
  {
    Serial.println("ERROR: Buffer is empty! Did you record first? (send 'r')");
    return;
  }
  
  int avg_magnitude = (sum > 0) ? sum / active_sample_count : 0;
  Serial.printf("Playing buffer: %d non-zero samples, avg magnitude: %d\n", nonzero_count, avg_magnitude);
  
  writeSamplesWithGain(record_buffer, active_sample_count);
  stopTxAndFlush();
  Serial.printf("Playback complete: %u bytes written\n", (unsigned int)(active_sample_count * sizeof(int16_t)));
}

void playReverse()
{
  // send samples in reverse order
  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  int idx = active_sample_count - 1;

  while (idx >= 0)
  {
    int count = min(CHUNK_SAMPLES, idx + 1);
    for (int i = 0; i < count; ++i)
    {
      chunk[i] = applyPlaybackGain(record_buffer[idx - i]);
    }
    size_t bytes_written = 0;
    i2s_write(I2S_TX_PORT, chunk, count * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    if (g_waveform_visible) drawWaveformPlayhead(idx);
    if (isJoystickButtonPressed()) break;
    idx -= count;
  }
  stopTxAndFlush();
}

void playResample(float speed)
{
  // speed >1.0 = faster (pitch up), <1.0 = slower (pitch down)
  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  int chunkIndex = 0;
  float idx = 0.0f;

  while ((int)idx < active_sample_count)
  {
    int read_idx = (int)idx;
    chunk[chunkIndex++] = applyPlaybackGain(record_buffer[read_idx]);
    if (chunkIndex >= CHUNK_SAMPLES)
    {
      size_t bytes_written = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bytes_written, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead((int)idx);
      if (isJoystickButtonPressed()) break;
    }
    idx += speed;
  }

  if (chunkIndex > 0)
  {
    size_t bytes_written = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bytes_written, portMAX_DELAY);
  }
  stopTxAndFlush();
}

void playEcho(float delaySec, float decay)
{
  int delaySamples = int(delaySec * SAMPLE_RATE);
  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  int chunkIndex = 0;

  for (int i = 0; i < active_sample_count; ++i)
  {
    int32_t out = record_buffer[i];
    if (i - delaySamples >= 0)
    {
      out += int32_t(record_buffer[i - delaySamples] * decay);
    }
    if (out > INT16_MAX) out = INT16_MAX;
    if (out < INT16_MIN) out = INT16_MIN;
    chunk[chunkIndex++] = applyPlaybackGain((int16_t)out);

    if (chunkIndex >= CHUNK_SAMPLES)
    {
      size_t bytes_written = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bytes_written, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }

  if (chunkIndex > 0)
  {
    size_t bytes_written = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bytes_written, portMAX_DELAY);
  }
  stopTxAndFlush();
}

void playRingMod(float freq)
{
  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  int chunkIndex = 0;

  for (int i = 0; i < active_sample_count; ++i)
  {
    float t = (float)i / SAMPLE_RATE;
    float mod = sinf(2.0f * 3.14159265f * freq * t);
    int32_t out = int32_t(record_buffer[i] * mod);
    if (out > INT16_MAX) out = INT16_MAX;
    if (out < INT16_MIN) out = INT16_MIN;
    chunk[chunkIndex++] = applyPlaybackGain((int16_t)out);

    if (chunkIndex >= CHUNK_SAMPLES)
    {
      size_t bytes_written = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bytes_written, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }

  if (chunkIndex > 0)
  {
    size_t bytes_written = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bytes_written, portMAX_DELAY);
  }
  stopTxAndFlush();
}

void playStutter(float chunkSec, int repeats)
{
  int chunkSamples = (int)(chunkSec * SAMPLE_RATE);
  if (chunkSamples < 1) chunkSamples = 1;

  const size_t WRITE_SIZE = 256;
  int16_t buf[WRITE_SIZE];

  for (int pos = 0; pos < active_sample_count; pos += chunkSamples)
  {
    int chunkEnd = pos + chunkSamples;
    if (chunkEnd > active_sample_count) chunkEnd = active_sample_count;

    for (int rep = 0; rep < repeats; ++rep)
    {
      int writePos = pos;
      while (writePos < chunkEnd)
      {
        int count = chunkEnd - writePos;
        if (count > (int)WRITE_SIZE) count = (int)WRITE_SIZE;
        for (int i = 0; i < count; ++i)
          buf[i] = applyPlaybackGain(record_buffer[writePos + i]);
        size_t bytesWritten = 0;
        i2s_write(I2S_TX_PORT, buf, count * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        writePos += count;
      }
    }

    if (g_waveform_visible) drawWaveformPlayhead(pos);
    if (isJoystickButtonPressed()) break;
  }
  stopTxAndFlush();
}

void playTremolo(float rate)
{
  // Amplitude modulation: volume oscillates at the given rate in Hz.
  const float DEPTH = 0.85f;
  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  int chunkIndex = 0;

  for (int i = 0; i < active_sample_count; ++i)
  {
    float t = (float)i / SAMPLE_RATE;
    float env = (1.0f - DEPTH) + DEPTH * (0.5f + 0.5f * sinf(2.0f * 3.14159265f * rate * t));
    int32_t out = (int32_t)((float)record_buffer[i] * env);
    if (out > INT16_MAX) out = INT16_MAX;
    if (out < INT16_MIN) out = INT16_MIN;
    chunk[chunkIndex++] = applyPlaybackGain((int16_t)out);

    if (chunkIndex >= CHUNK_SAMPLES)
    {
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }

  if (chunkIndex > 0)
  {
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  stopTxAndFlush();
}

void playHaunted(float delaySec, float decay)
{
  // Play the recording backwards and mix in an echo that trails behind each reversed sound.
  // In reversed playback "behind" means a higher index in the original buffer.
  int delaySamples = (int)(delaySec * SAMPLE_RATE);
  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  int chunkIndex = 0;

  for (int i = active_sample_count - 1; i >= 0; --i)
  {
    int32_t out = record_buffer[i];
    int echoIdx = i + delaySamples;
    if (echoIdx < active_sample_count)
      out += (int32_t)(record_buffer[echoIdx] * decay);
    if (out > INT16_MAX) out = INT16_MAX;
    if (out < INT16_MIN) out = INT16_MIN;
    chunk[chunkIndex++] = applyPlaybackGain((int16_t)out);

    if (chunkIndex >= CHUNK_SAMPLES)
    {
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }

  if (chunkIndex > 0)
  {
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  stopTxAndFlush();
}

void playAlien(float speed, float ringFreq)
{
  // Pitch-shifted playback with simultaneous ring modulation.
  // speed > 1.0 raises pitch; ringFreq controls the modulation buzz.
  // Ring mod timing follows output time so the buzz stays steady regardless of pitch.
  float idx = 0.0f;
  int outSample = 0;
  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  int chunkIndex = 0;

  while ((int)idx < active_sample_count)
  {
    float t = (float)outSample / SAMPLE_RATE;
    float mod = sinf(2.0f * 3.14159265f * ringFreq * t);
    int32_t out = (int32_t)((float)record_buffer[(int)idx] * mod);
    if (out > INT16_MAX) out = INT16_MAX;
    if (out < INT16_MIN) out = INT16_MIN;
    chunk[chunkIndex++] = applyPlaybackGain((int16_t)out);

    if (chunkIndex >= CHUNK_SAMPLES)
    {
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead((int)idx);
      if (isJoystickButtonPressed()) break;
    }
    idx += speed;
    outSample++;
  }

  if (chunkIndex > 0)
  {
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  stopTxAndFlush();
}

void playMonster(float speed, float delaySec, float decay)
{
  // Slow pitch-shifted playback with a live echo applied via a small circular delay buffer.
  // Because resampling changes the output length, a fixed-size delay buffer is used
  // rather than reading back into the original record_buffer.
  int delayLen = (int)(delaySec * SAMPLE_RATE) + 1;
  int16_t *echoBuf = (int16_t *)calloc(delayLen, sizeof(int16_t));
  if (!echoBuf)
  {
    playResample(speed);
    return;
  }
  int echoWr = 0;

  float idx = 0.0f;
  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  int chunkIndex = 0;

  while ((int)idx < active_sample_count)
  {
    int32_t sample = record_buffer[(int)idx];
    int32_t echSample = echoBuf[echoWr];  // oldest entry = delayLen output samples ago
    int32_t out = sample + (int32_t)(echSample * decay);
    if (out > INT16_MAX) out = INT16_MAX;
    if (out < INT16_MIN) out = INT16_MIN;

    echoBuf[echoWr] = (int16_t)out;
    echoWr = (echoWr + 1) % delayLen;

    chunk[chunkIndex++] = applyPlaybackGain((int16_t)out);
    if (chunkIndex >= CHUNK_SAMPLES)
    {
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead((int)idx);
      if (isJoystickButtonPressed()) break;
    }
    idx += speed;
  }

  if (chunkIndex > 0)
  {
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  free(echoBuf);
  stopTxAndFlush();
}

void playChorus(float rate, float depth)
{
  // LFO sweeps a short delay (10–30 ms) at the given rate in Hz.
  // depth in [0,1] scales the wet/dry mix: 0 = subtle, 1 = full chorus.
  const int CHORUS_LEN = (int)(0.05f * SAMPLE_RATE) + 1;
  int16_t *chorusBuf = (int16_t *)calloc(CHORUS_LEN, sizeof(int16_t));
  if (!chorusBuf)
  {
    playBufferSimple();
    return;
  }

  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  int chunkIndex = 0;
  int chorusWr = 0;
  float chorusPhase = 0.0f;
  float wet = 0.4f + depth * 0.5f;   // 0.4 – 0.9
  float dry = 1.0f - wet * 0.4f;     // keeps overall level stable

  for (int i = 0; i < active_sample_count; ++i)
  {
    float lfo = 0.5f + 0.5f * sinf(2.0f * 3.14159265f * chorusPhase);
    chorusPhase += rate / SAMPLE_RATE;
    if (chorusPhase >= 1.0f) chorusPhase -= 1.0f;

    int delaySmp = (int)(0.010f * SAMPLE_RATE + lfo * 0.020f * SAMPLE_RATE);
    int readIdx  = (chorusWr - delaySmp + CHORUS_LEN) % CHORUS_LEN;
    int16_t del  = chorusBuf[readIdx];
    chorusBuf[chorusWr] = record_buffer[i];
    chorusWr = (chorusWr + 1) % CHORUS_LEN;

    int32_t out = (int32_t)(dry * record_buffer[i] + wet * del);
    if (out > INT16_MAX) out = INT16_MAX;
    if (out < INT16_MIN) out = INT16_MIN;
    chunk[chunkIndex++] = applyPlaybackGain((int16_t)out);

    if (chunkIndex >= CHUNK_SAMPLES)
    {
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }

  if (chunkIndex > 0)
  {
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  free(chorusBuf);
  stopTxAndFlush();
}

void passthroughWithEffect(int fx)
{
  // fx: 0=plain 1=echo 2=star fighter 3=tremolo 4=chorus 5=distort 6=telephone 7=pitch up
  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];

  // Echo: 250 ms circular delay buffer
  const int ECHO_LEN = (int)(0.25f * SAMPLE_RATE);
  int16_t *echoBuf = nullptr;
  int echoWr = 0;
  if (fx == 1)
  {
    echoBuf = (int16_t *)calloc(ECHO_LEN, sizeof(int16_t));
    if (!echoBuf) fx = 0;
  }

  // Chorus: 50 ms delay buffer, LFO sweeps read point 10–30 ms back
  const int CHORUS_LEN = (int)(0.05f * SAMPLE_RATE) + 1;
  int16_t *chorusBuf = nullptr;
  int chorusWr = 0;
  float chorusPhase = 0.0f;
  if (fx == 4)
  {
    chorusBuf = (int16_t *)calloc(CHORUS_LEN, sizeof(int16_t));
    if (!chorusBuf) fx = 0;
  }

  // Telephone bandpass state (1st-order HP ~300 Hz + LP ~3000 Hz at 11025 Hz)
  // HP: α = RC/(RC+T) = 0.854   LP: α = T/(RC+T) = 0.631
  float hp_x1 = 0.0f, hp_y1 = 0.0f;
  float lp_y1 = 0.0f;

  // Pitch Up: granular pitch shift — reads the ring buffer at PITCH_UP speed with linear
  // interpolation, jumping back one grain whenever the read pointer catches the write
  // pointer, and crossfading at each jump to avoid clicks.  Words stay intelligible and
  // tempo is approximately preserved (unlike simple decimation which also speeds up).
  const int   PITCH_BUF      = 1024;   // ring buffer size (~93 ms at 11025 Hz)
  const int   PITCH_GRAIN    = 256;    // jump distance (~23 ms)
  const int   PITCH_FADE_LEN = 48;     // crossfade samples (~4 ms)
  const int   PITCH_GAP      = 48;     // min read-to-write gap before jumping
  const float PITCH_UP       = 1.5f;   // pitch factor (1.5 = one musical fifth higher)
  const float PITCH_DOWN     = 0.67f;  // pitch factor (0.67 = one musical fifth lower)
  int16_t *pitchRing  = nullptr;
  int32_t  pitchWPos  = 0;
  float    pitchRPos  = -(float)(PITCH_GRAIN * 2);  // start behind; outputs silence until filled
  float    pitchRPos2 = 0.0f;
  int      pitchFade  = 0;
  if (fx == 7 || fx == 8)
  {
    pitchRing = (int16_t *)calloc(PITCH_BUF, sizeof(int16_t));
    if (!pitchRing) fx = 0;
  }

  // Noise gate envelope follower
  float gateEnv = 0.0f;

  // Phase accumulators
  float ringPhase = 0.0f;
  float tremPhase = 0.0f;
  const float RING_FREQ  = 40.0f;
  const float TREM_FREQ  = 6.0f;
  const float TREM_DEPTH = 0.85f;

  while (true)
  {
    size_t bytes_read = 0;
    i2s_read(I2S_RX_PORT, chunk, CHUNK_SAMPLES * sizeof(int16_t), &bytes_read, portMAX_DELAY);
    size_t n = bytes_read / sizeof(int16_t);

    for (size_t i = 0; i < n; ++i)
    {
      int32_t s = chunk[i];

      if (fx == 1)  // echo
      {
        int32_t echo = echoBuf[echoWr];
        int32_t out  = s + (int32_t)(echo * 0.4f);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        echoBuf[echoWr] = (int16_t)out;
        echoWr = (echoWr + 1) % ECHO_LEN;
        s = out;
      }
      else if (fx == 2)  // star fighter ring mod
      {
        float mod = sinf(2.0f * 3.14159265f * ringPhase);
        ringPhase += RING_FREQ / SAMPLE_RATE;
        if (ringPhase >= 1.0f) ringPhase -= 1.0f;
        int32_t out = (int32_t)((float)s * mod);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }
      else if (fx == 3)  // tremolo
      {
        float env = (1.0f - TREM_DEPTH) + TREM_DEPTH * (0.5f + 0.5f * sinf(2.0f * 3.14159265f * tremPhase));
        tremPhase += TREM_FREQ / SAMPLE_RATE;
        if (tremPhase >= 1.0f) tremPhase -= 1.0f;
        int32_t out = (int32_t)((float)s * env);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }
      else if (fx == 4)  // chorus — modulated short delay mixed with original
      {
        float lfo = 0.5f + 0.5f * sinf(2.0f * 3.14159265f * chorusPhase);
        chorusPhase += 0.5f / SAMPLE_RATE;  // 0.5 Hz sweep
        if (chorusPhase >= 1.0f) chorusPhase -= 1.0f;
        int delaySmp = (int)(0.010f * SAMPLE_RATE + lfo * 0.020f * SAMPLE_RATE);
        int readIdx  = (chorusWr - delaySmp + CHORUS_LEN) % CHORUS_LEN;
        int16_t del  = chorusBuf[readIdx];
        chorusBuf[chorusWr] = (int16_t)s;
        chorusWr = (chorusWr + 1) % CHORUS_LEN;
        int32_t out = (int32_t)(0.6f * s + 0.6f * del);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }
      else if (fx == 5)  // distortion — 4× soft clip via x/(1+|x|) saturation
      {
        float x = (float)s / INT16_MAX * 4.0f;
        float y = x / (1.0f + fabsf(x));
        s = (int32_t)(y * INT16_MAX);
      }
      else if (fx == 6)  // telephone bandpass
      {
        float in   = (float)s;
        float hp   = 0.854f * (hp_y1 + in - hp_x1);
        hp_x1 = in;  hp_y1 = hp;
        float lp   = lp_y1 + 0.631f * (hp - lp_y1);
        lp_y1 = lp;
        int32_t out = (int32_t)(lp * 2.0f);  // compensate for filter attenuation
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }
      else if (fx == 7)  // pitch up — granular, tempo preserved
      {
        // Write incoming mic sample into the ring buffer
        pitchRing[pitchWPos % PITCH_BUF] = (int16_t)s;
        pitchWPos++;

        float outF = 0.0f;
        if (pitchFade > 0)
        {
          // Cross-fade: primary (new pos) fades in, secondary (old pos) fades out
          float alpha = (float)pitchFade / PITCH_FADE_LEN;
          int p0 = ((int)pitchRPos)  % PITCH_BUF, p1 = (p0 + 1) % PITCH_BUF;
          float pf = pitchRPos  - floorf(pitchRPos);
          float s1 = (1.0f - pf) * pitchRing[p0] + pf * pitchRing[p1];
          int q0 = ((int)pitchRPos2) % PITCH_BUF, q1 = (q0 + 1) % PITCH_BUF;
          float qf = pitchRPos2 - floorf(pitchRPos2);
          float s2 = (1.0f - qf) * pitchRing[q0] + qf * pitchRing[q1];
          outF = (1.0f - alpha) * s1 + alpha * s2;
          pitchRPos2 += PITCH_UP;
          pitchFade--;
        }
        else if (pitchRPos >= 0.0f)
        {
          int i0 = ((int)pitchRPos) % PITCH_BUF, i1 = (i0 + 1) % PITCH_BUF;
          float fr = pitchRPos - floorf(pitchRPos);
          outF = (1.0f - fr) * pitchRing[i0] + fr * pitchRing[i1];
        }

        pitchRPos += PITCH_UP;

        // If read pointer is getting too close to write, jump back one grain and crossfade
        if (pitchRPos >= 0.0f && pitchWPos - (int)pitchRPos < PITCH_GAP)
        {
          pitchRPos2 = pitchRPos;
          pitchRPos -= (float)PITCH_GRAIN;
          pitchFade  = PITCH_FADE_LEN;
          // Normalize to prevent float precision loss over long sessions
          if (pitchWPos > PITCH_BUF * 8)
          {
            int sub = (pitchWPos / PITCH_BUF - 4) * PITCH_BUF;
            pitchWPos -= sub;
            pitchRPos -= (float)sub;
            pitchRPos2 -= (float)sub;
          }
        }

        int32_t out = (int32_t)outF;
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }
      else if (fx == 8)  // pitch down — granular, tempo preserved
      {
        pitchRing[pitchWPos % PITCH_BUF] = (int16_t)s;
        pitchWPos++;

        float outF = 0.0f;
        if (pitchFade > 0)
        {
          float alpha = (float)pitchFade / PITCH_FADE_LEN;
          int p0 = ((int)pitchRPos)  % PITCH_BUF, p1 = (p0 + 1) % PITCH_BUF;
          float pf = pitchRPos  - floorf(pitchRPos);
          float s1 = (1.0f - pf) * pitchRing[p0] + pf * pitchRing[p1];
          int q0 = ((int)pitchRPos2) % PITCH_BUF, q1 = (q0 + 1) % PITCH_BUF;
          float qf = pitchRPos2 - floorf(pitchRPos2);
          float s2 = (1.0f - qf) * pitchRing[q0] + qf * pitchRing[q1];
          outF = (1.0f - alpha) * s1 + alpha * s2;
          pitchRPos2 += PITCH_DOWN;
          pitchFade--;
        }
        else if (pitchRPos >= 0.0f)
        {
          int i0 = ((int)pitchRPos) % PITCH_BUF, i1 = (i0 + 1) % PITCH_BUF;
          float fr = pitchRPos - floorf(pitchRPos);
          outF = (1.0f - fr) * pitchRing[i0] + fr * pitchRing[i1];
        }

        pitchRPos += PITCH_DOWN;

        // Jump read pointer forward when write gets too far ahead
        if (pitchRPos >= 0.0f && pitchWPos - (int)pitchRPos > PITCH_BUF - PITCH_GAP * 2)
        {
          pitchRPos2 = pitchRPos;
          pitchRPos += (float)PITCH_GRAIN;
          pitchFade  = PITCH_FADE_LEN;
          if (pitchWPos > PITCH_BUF * 8)
          {
            int sub = (pitchWPos / PITCH_BUF - 4) * PITCH_BUF;
            pitchWPos -= sub;
            pitchRPos -= (float)sub;
            pitchRPos2 -= (float)sub;
          }
        }

        int32_t out = (int32_t)outF;
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }

      int32_t gained = (int32_t)((float)s * playback_gain);
      if (gained > INT16_MAX) gained = INT16_MAX;
      if (gained < INT16_MIN) gained = INT16_MIN;
      chunk[i] = (int16_t)gained;
    }

    // Noise gate: measure input peak, update smoothed envelope, silence output if below threshold
    float peak = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
      float a = fabsf((float)chunk[i]);
      if (a > peak) peak = a;
    }
    gateEnv += (peak > gateEnv ? 0.8f : 0.05f) * (peak - gateEnv);
    if (gateEnv < PASSTHROUGH_GATE_THRESHOLD)
      memset(chunk, 0, n * sizeof(int16_t));

    size_t bytes_written = 0;
    i2s_write(I2S_TX_PORT, chunk, bytes_read, &bytes_written, portMAX_DELAY);

    if (isJoystickButtonPressed() || Serial.available()) break;
  }

  if (echoBuf)   free(echoBuf);
  if (chorusBuf) free(chorusBuf);
  if (pitchRing) free(pitchRing);
}

void passthrough()
{
  Serial.println("Passthrough: speak into mic, output will follow (Ctrl-C to stop via reset).\n");
  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  while (true)
  {
    size_t bytes_read = 0;
    i2s_read(I2S_RX_PORT, chunk, CHUNK_SAMPLES * sizeof(int16_t), &bytes_read, portMAX_DELAY);
    for (size_t i = 0; i < bytes_read / sizeof(int16_t); ++i)
    {
      chunk[i] = applyPlaybackGain(chunk[i]);
    }
    size_t bytes_written = 0;
    i2s_write(I2S_TX_PORT, chunk, bytes_read, &bytes_written, portMAX_DELAY);
    // allow serial commands by checking available
    if (Serial.available()) break;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("VoiceMorpher ESP32-S3 - initializing...");

  esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_PANIC)
    Serial.println("*** Last reset: PANIC/CRASH ***");
  else if (resetReason == ESP_RST_INT_WDT)
    Serial.println("*** Last reset: INTERRUPT WATCHDOG ***");
  else if (resetReason == ESP_RST_TASK_WDT)
    Serial.println("*** Last reset: TASK WATCHDOG ***");
  else if (resetReason == ESP_RST_WDT)
    Serial.println("*** Last reset: WATCHDOG ***");
  else
    Serial.printf("Last reset reason: %d\n", (int)resetReason);

  // Must be internal DRAM — I2S ISR conflicts with PSRAM cache on ESP32-S3 OPI.
  record_buffer = (int16_t *)heap_caps_malloc(
      SAMPLE_RATE * g_max_record_secs * sizeof(int16_t),
      MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  if (!record_buffer)
  {
    Serial.println("FATAL: Failed to allocate record buffer in internal DRAM");
    while (1) delay(1000);
  }
  Serial.printf("✓ Record buffer: %ds @ %d Hz (internal DRAM)\n", g_max_record_secs, SAMPLE_RATE);

  pinMode(AMP_SD, OUTPUT);
  digitalWrite(AMP_SD, HIGH);
  Serial.printf("✓ AMP_SD enabled on GPIO%d\n", AMP_SD);

  tft.init();
  tft.setRotation(3);  // landscape (rotated 180° from portrait)
  tft.fillScreen(C_BG);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  Serial.println("✓ ST7789V TFT initialized (240x320)");

  pinMode(JOY_BTN_PIN, INPUT_PULLUP);
  pinMode(JOY_X_PIN, INPUT);

  g_prefs.begin("voicemorph", false);
  float savedGain = g_prefs.getFloat("vol_gain", DEFAULT_PLAYBACK_GAIN);
  playback_gain  = savedGain;
  s_volumeLevel  = (savedGain - 0.5f) / 9.5f;
  if (s_volumeLevel < 0.0f) s_volumeLevel = 0.0f;
  if (s_volumeLevel > 1.0f) s_volumeLevel = 1.0f;
  Serial.printf("✓ Volume loaded: %.2fx\n", savedGain);

  LittleFS.begin(true);
  if (loadRecordingAuto())
    Serial.printf("✓ Auto-loaded last recording: %ds\n", active_sample_count / SAMPLE_RATE);

  initI2S();
#if PLAY_STARTUP_WAV
  playStartupWav();
#endif
  drawMenu();

  Serial.println("Ready. Commands:\n r = record  p = play  v = passthrough\n1 = play reverse 2 = pitch up 3 = pitch down 4 = echo 5 = ring mod\n");
}

void loop()
{
  if (Serial.available())
  {
    char c = Serial.read();
    switch (c)
    {
    case 'r':
      recordToBuffer();
      break;
    case 'p':
      Serial.println("Playing raw...");
      playBufferSimple();
      break;
    case 'v':
      passthrough();
      break;
    case '1':
      Serial.println("Playing reverse...");
      playReverse();
      break;
    case '2':
      Serial.println("Pitch up (speed 1.6)...");
      playResample(1.6f);
      break;
    case '3':
      Serial.println("Pitch down (speed 0.6)...");
      playResample(0.6f);
      break;
    case '4':
    {
      float intensity = readJoystickXIntensity();
      float delaySec = 0.1f + intensity * 0.4f;
      float decay = 0.2f + intensity * 0.5f;
      Serial.printf("Echo %.0fms decay %.2f...\n", delaySec * 1000.0f, decay);
      playEcho(delaySec, decay);
      break;
    }
    case '5':
    {
      float intensity = readJoystickXIntensity();
      float freq = 10.0f + intensity * 80.0f;
      Serial.printf("Ring modulation %.0fHz...\n", freq);
      playRingMod(freq);
      break;
    }
    default:
      break;
    }
  }

  handleJoystickMenu();
  delay(10);
}
