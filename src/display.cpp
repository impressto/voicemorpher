#include "globals.h"

// ── Gradient helpers ─────────────────────────────────────────────────────────

// Top→bottom gradient fill over a rectangle
void fillGradH(int16_t x, int16_t y, int16_t w, int16_t h,
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
void fillGradV(int16_t x, int16_t y, int16_t w, int16_t h,
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

// Colored header bar with white title (y 0–36, separator line at y=36)
void drawHeader(const char *title)
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
void drawHints(const char *line1, const char *line2)
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
void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, float level)
{
  tft.drawRoundRect(x, y, w, h, 3, TFT_WHITE);
  tft.fillRoundRect(x + 1, y + 1, w - 2, h - 2, 2, tft.color565(12, 15, 38));
  int16_t fw = (int16_t)(level * (w - 2));
  if (fw > 0)
    fillGradV(x + 1, y + 1, fw, h - 2, 0, 210, 100, 200, 200, 0);
}

// ── Per-item accent colors (R,G,B) — indexed by MenuItem enum value ──────────
// Covers the root menu (0..MENU_ROOT_COUNT-1) and settings items (MENU_STORAGE_COUNT..MENU_SETTINGS_COUNT-1)
const uint8_t ITEM_RGB[][3] = {
    {220,  60,  60},  // MENU_RECORD         — red
    { 60, 200,  80},  // MENU_PLAY           — green
    {160,  80, 220},  // MENU_PASSTHROUGH    — purple
    {220, 140,  30},  // MENU_LONG_REC       — amber
    { 40, 200, 180},  // MENU_LONG_PLAY      — teal
    {220,  80, 180},  // MENU_MOOD           — pink
    {220, 200,  40},  // MENU_THEREMIN       — yellow
    { 40, 180, 255},  // MENU_MATHSYNTH      — electric blue
    {140, 140, 160},  // MENU_SETTINGS       — silver
    // Settings sub-menu items
    {220, 160,  40},  // MENU_VOLUME         — gold
    {220,  80,  60},  // MENU_FEEDBACK       — red-orange
    { 80, 220,  80},  // MENU_LIVE_GAIN      — bright green
    { 80, 160, 220},  // MENU_MIC_GAIN       — sky blue
    { 60, 200, 200},  // MENU_CALIBRATE_JOY  — teal
    {160,  80, 220},  // MENU_WAVELAB_VOL    — violet
};

// ── Main menu ────────────────────────────────────────────────────────────────

void drawMenu()
{
  tft.fillScreen(C_BG);
  tft.setTextSize(2);

  for (int i = 0; i < MENU_ROOT_COUNT; ++i)
  {
    int16_t y = i * ITEM_H;
    uint8_t r = ITEM_RGB[i][0], g = ITEM_RGB[i][1], b = ITEM_RGB[i][2];
    uint16_t accentCol = tft.color565(r, g, b);
    uint16_t iconColor;

    if (i == currentMenu)
    {
      // Selected: dark-tinted gradient in the item's hue + bright accent bar
      fillGradH(0, y, TFT_W, ITEM_H - 1, r/4, g/4, b/4, r/10, g/10, b/10);
      tft.fillRect(0, y, 4, ITEM_H - 1, accentCol);
      tft.setTextColor(TFT_WHITE);
      iconColor = TFT_WHITE;
    }
    else
    {
      // Unselected: very dark tint + dim accent bar + item-colored text
      tft.fillRect(0, y, TFT_W, ITEM_H - 1, tft.color565(r/12, g/12, b/12));
      tft.fillRect(0, y, 4, ITEM_H - 1, tft.color565(r/3, g/3, b/3));
      uint16_t tc = tft.color565((uint8_t)((int)r*3/4), (uint8_t)((int)g*3/4), (uint8_t)((int)b*3/4));
      tft.setTextColor(tc);
      iconColor = tc;
    }
    tft.drawFastHLine(0, y + ITEM_H - 1, TFT_W, tft.color565(r/8, g/8, b/8));
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
void drawWfColumn(int px)
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

  // Colour by frequency: blue (low) → red (mid) → yellow (high)
  float t = g_wf_freqt[px] / 255.0f;
  uint8_t wr, wg, wb;
  if (t < 0.5f) {
    float t2 = t * 2.0f;
    wr = (uint8_t)(255 * t2);
    wg = (uint8_t)(100 * (1.0f - t2));
    wb = (uint8_t)(255 * (1.0f - t2));
  } else {
    float t2 = (t - 0.5f) * 2.0f;
    wr = 255;
    wg = (uint8_t)(220 * t2);
    wb = 0;
  }
  uint16_t wc = tft.color565(wr, wg, wb);
  if (y1 <= y0) tft.drawFastVLine(x, y1, y0 - y1 + 1, wc);
  else          tft.drawFastVLine(x, y0, y1 - y0 + 1, wc);
}

// Stretch raw per-column frequencies to fill the full blue→yellow colour range.
static void normalizeFreqToColor(const float *raw, int cols)
{
  float f_min = 1e9f, f_max = 0.0f;
  for (int i = 0; i < cols; ++i) {
    if (raw[i] > 0 && raw[i] < f_min) f_min = raw[i];
    if (raw[i] > f_max) f_max = raw[i];
  }
  float f_range = f_max - f_min;
  for (int i = 0; i < cols; ++i) {
    float t = (f_range > 1.0f && raw[i] > 0) ? (raw[i] - f_min) / f_range : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    g_wf_freqt[i] = (uint8_t)(t * 255.0f);
  }
}

static void drawWaveformScreen(const char *title)
{
  g_wf_last_px = -1;

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

  if (!g_wf_use_peaks && active_sample_count > 0)
  {
    const int innerW = WF_W - 2;
    const int ZCR_WIN = 512;
    static float raw_freq[TFT_W];
    memset(raw_freq, 0, sizeof(raw_freq));
    for (int px = 0; px < innerW; ++px)
    {
      int sIdx = (int)((int64_t)px * active_sample_count / innerW);
      int s0 = max(0, sIdx - ZCR_WIN / 2);
      int s1 = min(active_sample_count - 1, sIdx + ZCR_WIN / 2);
      int crossings = 0, prev_sign = record_buffer[s0] >= 0 ? 1 : -1;
      for (int i = s0 + 1; i <= s1; ++i)
      {
        int sign = record_buffer[i] >= 0 ? 1 : -1;
        if (sign != prev_sign) crossings++;
        prev_sign = sign;
      }
      int winLen = s1 - s0;
      raw_freq[px] = winLen > 0 ? (float)crossings * SAMPLE_RATE / (2.0f * winLen) : 0.0f;
    }
    normalizeFreqToColor(raw_freq, innerW);
  }

  for (int px = 0; px < WF_W - 2; ++px)
    drawWfColumn(px);
}

// Scans the already-open LittleFS file to fill g_wf_peaks, then seeks back to
// the start of sample data so the caller can immediately begin playback.
void fillStoredWaveformBuf(File &f, int32_t totalSamples)
{
  const int innerW = WF_W - 2;
  memset(g_wf_peaks, 0, sizeof(g_wf_peaks));
  memset(g_wf_freqt, 0, sizeof(g_wf_freqt));
  if (totalSamples <= 0) return;

  static uint16_t zcr_count[TFT_W];
  static uint16_t col_samples[TFT_W];
  memset(zcr_count,   0, sizeof(zcr_count));
  memset(col_samples, 0, sizeof(col_samples));

  const int CHUNK = 256;
  int16_t chunk[CHUNK];
  int32_t pos = 0;
  int prev_sign = 0;

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
      int sign = chunk[i] > 0 ? 1 : (chunk[i] < 0 ? -1 : 0);
      if (sign != 0) {
        if (prev_sign != 0 && sign != prev_sign) zcr_count[px]++;
        col_samples[px]++;
        prev_sign = sign;
      }
    }
    pos += got;
  }

  static float raw_freq[TFT_W];
  memset(raw_freq, 0, sizeof(raw_freq));
  for (int px = 0; px < innerW; ++px)
  {
    raw_freq[px] = col_samples[px] > 0
      ? (float)zcr_count[px] * SAMPLE_RATE / (2.0f * col_samples[px])
      : 0.0f;
  }
  normalizeFreqToColor(raw_freq, innerW);

  f.seek(sizeof(int32_t));  // rewind to start of sample data for playback
}

void drawWaveformPlayhead(int samplePos)
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
int showDurationSubMenu(int currentSecs, int maxSecs)
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

float showLevelSubMenu(const char *title, const char *paramLabel,
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

// Shared scrollable icon-list helper used by both Play and Stored Play FX pickers.
int showIconList(const char *const *labels, const uint8_t *const *icons,
                 int count, int &sel)
{
  const int VISIBLE = 8;
  unsigned long lastMoveMs = 0;
  int prevSel = -2;
  while (isJoystickButtonPressed()) delay(10);

  while (true)
  {
    if (sel != prevSel)
    {
      prevSel = sel;
      int startIdx = sel - VISIBLE / 2;
      if (startIdx < 0) startIdx = 0;
      if (startIdx > count - VISIBLE) startIdx = count - VISIBLE;
      if (startIdx < 0) startIdx = 0;

      tft.fillScreen(C_BG);
      tft.setTextSize(2);

      for (int i = 0; i < VISIBLE && (startIdx + i) < count; ++i)
      {
        int idx = startIdx + i;
        int16_t y = i * ITEM_H;
        uint16_t iconColor;
        if (idx == sel)
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
        tft.drawBitmap(6, y + 5, icons[idx], 16, 16, iconColor);
        tft.setCursor(28, y + 5);
        tft.print(labels[idx]);
      }

      if (startIdx > 0)
      {
        tft.setTextColor(TFT_CYAN); tft.setTextSize(1);
        tft.setCursor(TFT_W - 10, 2); tft.print("^");
      }
      if (startIdx + VISIBLE < count)
      {
        tft.setTextColor(TFT_CYAN); tft.setTextSize(1);
        tft.setCursor(TFT_W - 10, VISIBLE * ITEM_H - 8); tft.print("v");
      }
      tft.setTextSize(1);
      tft.setTextColor(COL_GRAY);
      tft.setCursor(4, min(count, VISIBLE) * ITEM_H + 4);
      tft.print("< X: back");
    }

    int y = readJoystickAxis(JOY_Y_PIN);
    int x = readJoystickAxis(JOY_X_PIN);
    unsigned long now = millis();

    if ((y != 0 || x < 0) && now - lastMoveMs > 200)
    {
      if (x < 0) return -1;
      sel += (y < 0 ? -1 : 1);
      if (sel < 0)      sel = 0;
      if (sel >= count) sel = count - 1;
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
