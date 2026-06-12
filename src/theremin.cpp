#include "globals.h"
#include "kitten_pcm.h"
#include "beavis_pcm.h"
#include "choir_pcm.h"

const char *TH_SOUND_NAMES[]    = { "Sine", "Kitten", "Beavis", "Choir" };
const char *TH_PITCH_SRC_NAMES[] = { "Joystick", "Sonar" };
const char *TH_SOUND_PATHS[]    = { nullptr, "/kitten.wav", "/beavis.wav", "/choir.wav" };

// Pre-computed 256-entry sine LUT
static const int16_t SINE_LUT[256] PROGMEM = {
      0,   804,  1608,  2410,  3212,  4011,  4808,  5602,
   6393,  7179,  7962,  8739,  9512, 10278, 11039, 11793,
  12539, 13279, 14010, 14733, 15446, 16151, 16846, 17530,
  18204, 18868, 19519, 20159, 20787, 21403,	22005,	22594,
	23170,	23731,	24279,	24811,	25329,	25832,	26319,	26790,
  27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
  30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971,
  32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
  32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
  32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
  30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683,
  27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
  23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868,
  18204, 17530, 16846, 16151, 15446, 14733, 14010, 13279,
  12539, 11793, 11039, 10278,  9512,  8739,  7962,  7179,
   6393,  5602,  4808,  4011,  3212,  2410,  1608,   804,
      0,  -804, -1608, -2410, -3212, -4011, -4808, -5602,
  -6393, -7179, -7962, -8739, -9512,-10278,-11039,-11793,
 -12539,-13279,-14010,-14733,-15446,-16151,-16846,-17530,
 -18204,-18868,-19519,-20159,-20787,-21403,-22005,-22594,
 -23170,-23731,-24279,-24811,-25329,-25832,-26319,-26790,
 -27245,-27683,-28105,-28510,-28898,-29268,-29621,-29956,
 -30273,-30571,-30852,-31113,-31356,-31580,-31785,-31971,
 -32137,-32285,-32412,-32521,-32609,-32678,-32728,-32757,
 -32767,-32757,-32728,-32678,-32609,-32521,-32412,-32285,
 -32137,-31971,-31785,-31580,-31356,-31113,-30852,-30571,
 -30273,-29956,-29621,-29268,-28898,-28510,-28105,-27683,
 -27245,-26790,-26319,-25832,-25329,-24811,-24279,-23731,
 -23170,-22594,-22005,-21403,-20787,-20159,-19519,-18868,
 -18204,-17530,-16846,-16151,-15446,-14733,-14010,-13279,
 -12539,-11793,-11039,-10278, -9512, -8739, -7962, -7179,
  -6393, -5602, -4808, -4011, -3212, -2410, -1608,  -804,
};

void calibrateThereminJoy()
{
  tft.fillScreen(C_BG);
  fillGradH(0, 0, TFT_W, 36, 0, 55, 140, 0, 15, 65);
  tft.drawFastHLine(0, 36, TFT_W, TFT_CYAN);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.print("Joy Cal");
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(8, 50);
  tft.print("Calibrate joystick");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(8, 80);
  tft.print("Push Y fully up & down");
  tft.setCursor(8, 105);
  tft.print("then press button.");

  const int16_t CB_X = 8, CB_Y = 145, CB_W = TFT_W - 16, CB_H = 22;
  tft.drawRect(CB_X - 1, CB_Y - 1, CB_W + 2, CB_H + 2, tft.color565(40, 50, 100));
  tft.setTextSize(1);
  tft.setTextColor(COL_GRAY);
  tft.setCursor(4, TFT_H - 12);
  tft.print("Btn: done");

  int joyYMin = 4095, joyYMax = 0, lastBarFill = -1;

  while (!isJoystickButtonPressed())
  {
    int y = analogRead(JOY_Y_PIN);
    if (y < joyYMin) joyYMin = y;
    if (y > joyYMax) joyYMax = y;

    int sweepPx = (int)((float)(joyYMax - joyYMin) / 4095.0f * CB_W);
    if (sweepPx != lastBarFill)
    {
      lastBarFill = sweepPx;
      tft.fillRect(CB_X, CB_Y, CB_W, CB_H, tft.color565(10, 12, 30));
      int startX = (int)((float)joyYMin / 4095.0f * CB_W);
      tft.fillRect(CB_X + startX, CB_Y, sweepPx, CB_H, TFT_CYAN);
    }
    delay(20);
  }
  while (isJoystickButtonPressed()) delay(10);

  if (joyYMax <= joyYMin) { joyYMin = 100; joyYMax = 3900; }

  int margin = (joyYMax - joyYMin) / 20;
  joyYMin = max(0,    joyYMin - margin);
  joyYMax = min(4095, joyYMax + margin);

  g_prefs.putInt("th_ymin", joyYMin);
  g_prefs.putInt("th_ymax", joyYMax);

  drawStatus("Joy Cal saved!", "");
  delay(1000);
}

void thereminMode()
{
  static const char *noteNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
  };

  if (g_th_pitch_src == 0 &&
      g_prefs.getInt("th_ymax", -1) - g_prefs.getInt("th_ymin", -1) < 500)
    calibrateThereminJoy();

  int joyYMin = g_prefs.getInt("th_ymin", 100);
  int joyYMax = g_prefs.getInt("th_ymax", 3900);
  bool th_quantize = g_prefs.getBool("th_quantize", false);

  i2s_driver_uninstall(I2S_TX_PORT);

  const int16_t *th_sample = nullptr;
  int            th_sample_len = 0;
  if      (g_th_sound == 1) { th_sample = KITTEN_PCM; th_sample_len = KITTEN_LEN; }
  else if (g_th_sound == 2) { th_sample = BEAVIS_PCM; th_sample_len = BEAVIS_LEN; }
  else if (g_th_sound == 3) { th_sample = CHOIR_PCM;  th_sample_len = CHOIR_LEN;  }

  // Reinitialize TX with low-latency DMA
  {
    i2s_config_t cfg = {};
    cfg.mode                = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate         = SAMPLE_RATE;
    cfg.bits_per_sample     = (i2s_bits_per_sample_t)I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format      = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format= I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count       = 4;
    cfg.dma_buf_len         = 128;
    cfg.use_apll            = false;
    i2s_driver_install(I2S_TX_PORT, &cfg, 0, NULL);
    i2s_pin_config_t pins = {};
    pins.bck_io_num     = I2S_TX_BCK;
    pins.ws_io_num      = I2S_TX_WS;
    pins.data_out_num   = I2S_TX_SD;
    pins.data_in_num    = I2S_PIN_NO_CHANGE;
    i2s_set_pin(I2S_TX_PORT, &pins);
  }
  Serial.println("TH: I2S installed"); Serial.flush();

  // Draw UI
  tft.fillScreen(C_BG);
  fillGradH(0, 0, TFT_W, 36, 0, 55, 140, 0, 15, 65);
  tft.drawFastHLine(0, 36, TFT_W, TFT_CYAN);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.print("Theremin");
  if (g_th_sound > 0) {
    tft.setTextSize(1);
    tft.setTextColor(tft.color565(140, 200, 255));
    tft.setCursor(10, 26);
    tft.print(TH_SOUND_NAMES[g_th_sound]);
  }
  const int16_t BAR_X = 8, BAR_Y = 148, BAR_W = TFT_W - 16, BAR_H = 22;
  tft.drawRect(BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2, tft.color565(40, 50, 100));
  tft.setTextSize(1);
  tft.setTextColor(COL_GRAY);
  tft.setCursor(BAR_X, BAR_Y + BAR_H + 4);
  tft.print("C3");
  tft.setCursor(BAR_X + BAR_W - 12, BAR_Y + BAR_H + 4);
  tft.print("C6");
  const int16_t VBAR_X = 8, VBAR_Y = 192, VBAR_W = TFT_W - 16, VBAR_H = 14;
  tft.setCursor(BAR_X, VBAR_Y - 12);
  tft.print("Vol");
  tft.drawRect(VBAR_X - 1, VBAR_Y - 1, VBAR_W + 2, VBAR_H + 2, tft.color565(40, 50, 100));
  auto drawThereminBadge = [&](bool quantize) {
    const char *label = quantize ? "NOTES" : " FREE";
    uint16_t bg = quantize ? tft.color565(160, 120, 0) : tft.color565(0, 130, 60);
    tft.fillRoundRect(TFT_W - 72, 6, 64, 22, 4, bg);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(TFT_W - 68, 10);
    tft.print(label);
  };
  drawThereminBadge(th_quantize);

  tft.setTextSize(1);
  tft.setTextColor(COL_GRAY);
  tft.setCursor(4, TFT_H - 22);
  tft.print(g_th_pitch_src == 1 ? "Sonar: pitch  X: vol" : "Y: pitch   X: vol");
  tft.setCursor(4, TFT_H - 12);
  tft.print("Btn: free/notes  Hold/2xClick: exit");

  float phase    = 0.0f;
  float readPos  = 0.0f;
  char lastNote[8]  = "";
  char lastFreq[16] = "";
  int  lastPitchBar = -1;
  int  lastVolBar   = -1;
  bool lastQuantize = th_quantize;
  float lastSonarCm = (TH_SONAR_MIN_CM + TH_SONAR_MAX_CM) * 0.5f;
  int   sonarTick   = 0;
  float stepVol     = 0.5f;
  unsigned long lastVolMoveMs = 0;
  unsigned long lastDisplayMs = 0;
  unsigned long lastTapMs     = 0;

  const int CHUNK = 64;
  int16_t buf[CHUNK];

  while (true)
  {
    int rawX = analogRead(JOY_X_PIN);
    if (rawX < 0) rawX = 0; if (rawX > 4095) rawX = 4095;

    float pitchT;
    if (g_th_pitch_src == 1) {
      if ((sonarTick & 3) == 0) {
        float d = readHCSR04cm();
        if (d > 0.0f) lastSonarCm = d;
      }
      sonarTick++;
      float sc = lastSonarCm < TH_SONAR_MIN_CM ? TH_SONAR_MIN_CM
               : (lastSonarCm > TH_SONAR_MAX_CM ? TH_SONAR_MAX_CM : lastSonarCm);
      pitchT = 1.0f - (sc - TH_SONAR_MIN_CM) / (TH_SONAR_MAX_CM - TH_SONAR_MIN_CM);
    } else {
      int rawY = analogRead(JOY_Y_PIN);
      if (rawY < 0) rawY = 0; if (rawY > 4095) rawY = 4095;
      int clampedY = rawY < joyYMin ? joyYMin : (rawY > joyYMax ? joyYMax : rawY);
      pitchT = 1.0f - (float)(clampedY - joyYMin) / (float)(joyYMax - joyYMin);
    }
    float freq = 130.813f * powf(2.0f, pitchT * 3.0f);  // C3–C6
    if (th_quantize) {
      int midi = (int)roundf(69.0f + 12.0f * log2f(freq / 440.0f));
      freq = 440.0f * powf(2.0f, (midi - 69) / 12.0f);
    }

    float amp;
    if (g_th_pitch_src == 1) {
      int xDir = readJoystickAxis(JOY_X_PIN);
      unsigned long vm = millis();
      if (xDir != 0 && vm - lastVolMoveMs >= 300) {
        stepVol += xDir * (1.0f / 8.0f);
        if (stepVol < 0.0f) stepVol = 0.0f;
        if (stepVol > 1.0f) stepVol = 1.0f;
        lastVolMoveMs = vm;
      }
      amp = stepVol;
    } else {
      float v = (float)rawX / 4095.0f;
      amp = v * v;
    }

    if (th_sample == nullptr) {
      for (int i = 0; i < CHUNK; i++) {
        int   idx  = (int)phase & 0xFF;
        float frac = phase - (int)phase;
        float s    = (1.0f - frac) * SINE_LUT[idx] + frac * SINE_LUT[(idx + 1) & 0xFF];
        phase += 256.0f * freq / (float)SAMPLE_RATE;
        if (phase >= 256.0f) phase -= 256.0f;
        int32_t out = (int32_t)(s * amp * 0.85f);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        buf[i] = (int16_t)out;
      }
    } else {
      float len_f      = (float)th_sample_len;
      float pitch_rate = freq / 440.0f;
      for (int i = 0; i < CHUNK; i++) {
        int   idx0 = (int)readPos;
        if (idx0 < 0) idx0 = 0;
        if (idx0 >= th_sample_len) idx0 = th_sample_len - 1;
        float frac = readPos - idx0;
        int   idx1 = (idx0 + 1 < th_sample_len) ? idx0 + 1 : 0;
        float s    = (1.0f - frac) * th_sample[idx0] + frac * th_sample[idx1];
        readPos = fmodf(readPos + pitch_rate, len_f);
        int32_t out = (int32_t)(s * amp * 0.85f);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        buf[i] = (int16_t)out;
      }
    }
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, buf, CHUNK * sizeof(int16_t), &bw, portMAX_DELAY);

    // Display updates throttled to ~120ms
    unsigned long now = millis();
    if (now - lastDisplayMs >= 120)
    {
      lastDisplayMs = now;

      int midiNote = (int)roundf(69.0f + 12.0f * log2f(freq / 440.0f));
      char noteBuf[8];
      snprintf(noteBuf, sizeof(noteBuf), "%s%d", noteNames[((midiNote % 12) + 12) % 12], midiNote / 12 - 1);
      if (strcmp(noteBuf, lastNote) != 0)
      {
        strcpy(lastNote, noteBuf);
        tft.fillRect(0, 44, 160, 48, C_BG);
        tft.setTextColor(TFT_CYAN);
        tft.setTextSize(4);
        tft.setCursor(8, 48);
        tft.print(noteBuf);
      }

      char freqBuf[16];
      snprintf(freqBuf, sizeof(freqBuf), "%.1f Hz", freq);
      if (strcmp(freqBuf, lastFreq) != 0)
      {
        strcpy(lastFreq, freqBuf);
        tft.fillRect(0, 100, TFT_W, 20, C_BG);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(2);
        tft.setCursor(8, 102);
        tft.print(freqBuf);
      }

      int pitchFill = (int)(pitchT * BAR_W);
      if (pitchFill != lastPitchBar)
      {
        lastPitchBar = pitchFill;
        tft.fillRect(BAR_X, BAR_Y, BAR_W, BAR_H, tft.color565(10, 12, 30));
        if (pitchFill > 0)
          tft.fillRect(BAR_X, BAR_Y, pitchFill, BAR_H, TFT_CYAN);
      }

      int volFill = (int)((g_th_pitch_src == 1 ? stepVol : amp) * VBAR_W);
      if (volFill != lastVolBar)
      {
        lastVolBar = volFill;
        tft.fillRect(VBAR_X, VBAR_Y, VBAR_W, VBAR_H, tft.color565(10, 12, 30));
        if (volFill > 0)
          tft.fillRect(VBAR_X, VBAR_Y, volFill, VBAR_H, tft.color565(80, 200, 80));
      }

      if (th_quantize != lastQuantize)
      {
        lastQuantize = th_quantize;
        drawThereminBadge(th_quantize);
      }
    }

    if (isJoystickButtonPressed())
    {
      unsigned long pressStart = millis();
      while (isJoystickButtonPressed()) delay(10);
      unsigned long now = millis();
      if (now - pressStart >= 500) {
        break;
      } else if (now - lastTapMs <= DOUBLE_CLICK_MS) {
        break;   // double click = exit
      } else {
        th_quantize = !th_quantize;
        g_prefs.putBool("th_quantize", th_quantize);
        lastQuantize = !th_quantize;
        lastTapMs = now;
      }
    }
  }

  // Fade to silence
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 3; i++) { size_t bw = 0; i2s_write(I2S_TX_PORT, buf, CHUNK * sizeof(int16_t), &bw, portMAX_DELAY); }

  // Restore TX to normal DMA config (8×256)
  i2s_driver_uninstall(I2S_TX_PORT);
  {
    i2s_config_t cfg = {};
    cfg.mode                = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate         = SAMPLE_RATE;
    cfg.bits_per_sample     = (i2s_bits_per_sample_t)I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format      = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format= I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count       = 8;
    cfg.dma_buf_len         = 256;
    cfg.use_apll            = false;
    i2s_driver_install(I2S_TX_PORT, &cfg, 0, NULL);
    i2s_pin_config_t pins = {};
    pins.bck_io_num     = I2S_TX_BCK;
    pins.ws_io_num      = I2S_TX_WS;
    pins.data_out_num   = I2S_TX_SD;
    pins.data_in_num    = I2S_PIN_NO_CHANGE;
    i2s_set_pin(I2S_TX_PORT, &pins);
  }
  i2s_zero_dma_buffer(I2S_TX_PORT);
}

void runThereminMenu()
{
  static const char  *items[]  = { "Play", "Sound", "Pitch" };
  static const uint8_t *icons[] = { ICON_THEREMIN, ICON_PLAY, ICON_JOYCAL };
  const int COUNT = 3;
  int sel = 0;
  int prevSel = -1;
  unsigned long lastMoveMs = 0;
  while (isJoystickButtonPressed()) delay(10);

  while (true)
  {
    if (sel != prevSel)
    {
      prevSel = sel;
      tft.fillScreen(C_BG);
      tft.setTextSize(2);

      for (int i = 0; i < COUNT; ++i)
      {
        int16_t y = i * ITEM_H;
        if (i == sel)
        {
          fillGradH(0, y, TFT_W, ITEM_H - 1, 0, 130, 190, 0, 55, 120);
          tft.fillRect(0, y, 4, ITEM_H - 1, TFT_CYAN);
          tft.setTextColor(TFT_WHITE);
          tft.drawBitmap(6, y + 5, icons[i], 16, 16, TFT_WHITE);
        }
        else
        {
          uint16_t rc = (i & 1) ? tft.color565(12, 15, 38) : C_BG;
          tft.fillRect(0, y, TFT_W, ITEM_H - 1, rc);
          tft.setTextColor(0xDEFB);
          tft.drawBitmap(6, y + 5, icons[i], 16, 16, 0xDEFB);
        }
        tft.drawFastHLine(0, y + ITEM_H - 1, TFT_W, tft.color565(20, 25, 55));
        tft.setCursor(28, y + 5);
        tft.print(items[i]);
      }

      char hint1[32], hint2[32];
      snprintf(hint1, sizeof(hint1), "Sound: %s", TH_SOUND_NAMES[g_th_sound]);
      snprintf(hint2, sizeof(hint2), "Pitch: %s", TH_PITCH_SRC_NAMES[g_th_pitch_src]);
      drawHints(hint1, hint2);
    }

    int y = readJoystickAxis(JOY_Y_PIN);
    int x = readJoystickAxis(JOY_X_PIN);
    unsigned long now = millis();

    if ((y != 0 || x < 0) && now - lastMoveMs > 200)
    {
      if (x < 0) return;
      sel = (sel + (y < 0 ? -1 : 1) + COUNT) % COUNT;
      lastMoveMs = now;
    }

    if (isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);

      if (sel == 0)  // Play
      {
        thereminMode();
        prevSel = -1;
      }
      else if (sel == 1)  // Sound picker
      {
        static const uint8_t *soundIcons[] = { ICON_THEREMIN, ICON_PLAY, ICON_PLAY, ICON_PLAY, ICON_PLAY };
        int newSound = showIconList(TH_SOUND_NAMES, soundIcons, TH_SOUND_COUNT, g_th_sound);
        if (newSound >= 0 && newSound != g_th_sound)
        {
          g_th_sound = newSound;
          g_prefs.putInt("th_sound", g_th_sound);
        }
        prevSel = -1;
      }
      else  // Pitch source picker
      {
        static const uint8_t *pitchIcons[] = { ICON_JOYCAL, ICON_THEREMIN };
        int newSrc = showIconList(TH_PITCH_SRC_NAMES, pitchIcons, TH_PITCH_SRC_COUNT, g_th_pitch_src);
        if (newSrc >= 0 && newSrc != g_th_pitch_src)
        {
          g_th_pitch_src = newSrc;
          g_prefs.putInt("th_pitch_src", g_th_pitch_src);
        }
        prevSel = -1;
      }
    }
  }
}
