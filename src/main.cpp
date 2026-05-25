#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <LittleFS.h>
#include "driver/i2s.h"
#include "config.h"

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

// OLED display pins (I2C)
#define DISPLAY_SDA 18
#define DISPLAY_SCL 16

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

// Playback volume config
float playback_gain = DEFAULT_PLAYBACK_GAIN; // adjust default volume in src/config.h
  
// Menu and UI
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

enum MenuItem
{
  MENU_RECORD,
  MENU_PLAY,
  MENU_PASSTHROUGH,
  MENU_REVERSE,
  MENU_PITCH_UP,
  MENU_PITCH_DOWN,
  MENU_ECHO,
  MENU_RINGMOD,
  MENU_STUTTER,
  MENU_COUNT
};

static const char *menuLabels[MENU_COUNT] = {
  "Record",
  "Play raw",
  "Passthrough",
  "Reverse",
  "Pitch up",
  "Pitch down",
  "Echo",
  "Ring mod",
  "Stutter"
};

int currentMenu = 0;
int lastMenu = -1;
static int lastJoystickY = 0;
static unsigned long lastJoystickMoveMs = 0;

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
void playReverse();
void playResample(float speed);
void playEcho(float delaySec, float decay);
void playRingMod(float freq);
void playStutter(float chunkSec, int repeats);

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
    currentMenu = (currentMenu + MENU_COUNT + (y < 0 ? -1 : 1)) % MENU_COUNT;
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

void drawMenu()
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);

  const int visibleCount = 4;
  int startIndex = currentMenu - visibleCount / 2;
  if (startIndex < 0) startIndex = 0;
  if (startIndex > MENU_COUNT - visibleCount)
    startIndex = MENU_COUNT - visibleCount;

  for (int i = 0; i < visibleCount; ++i)
  {
    int itemIndex = startIndex + i;
    int y = 14 + i * 12;

    if (itemIndex == currentMenu)
    {
      u8g2.drawBox(0, y - 10, 128, 12);
      u8g2.setDrawColor(0);
      u8g2.drawStr(2, y, menuLabels[itemIndex]);
      u8g2.setDrawColor(1);
    }
    else
    {
      u8g2.drawStr(2, y, menuLabels[itemIndex]);
    }
  }

  if (startIndex > 0)
  {
    u8g2.drawStr(110, 10, "^");
  }
  if (startIndex + visibleCount < MENU_COUNT)
  {
    u8g2.drawStr(110, 58, "v");
  }

  u8g2.drawStr(0, 62, "Press button to select");
  u8g2.sendBuffer();
}

void drawStatus(const char *line1, const char *line2)
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 16, line1);
  if (line2)
    u8g2.drawStr(0, 32, line2);
  u8g2.sendBuffer();
}

// Shows a sub-menu to select recording duration (1 to g_max_record_secs seconds).
// Joystick X adjusts in 1-second steps; button confirms.
static int showDurationSubMenu(int currentSecs)
{
  int secs = currentSecs < 1 ? 1 : (currentSecs > g_max_record_secs ? g_max_record_secs : currentSecs);
  unsigned long lastMoveMs = 0;

  while (true)
  {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(2, 10, "Record Duration");

    const int BX = 2, BY = 16, BW = 124, BH = 10;
    u8g2.drawFrame(BX, BY, BW, BH);
    int filled = g_max_record_secs > 1
                   ? (int)((float)(secs - 1) / (g_max_record_secs - 1) * (BW - 2))
                   : BW - 2;
    if (filled > 0)
      u8g2.drawBox(BX + 1, BY + 1, filled, BH - 2);

    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), "Duration: %ds", secs);
    u8g2.drawStr(2, 38, valBuf);
    u8g2.drawStr(2, 50, "< X: adjust >");
    u8g2.drawStr(2, 62, "Btn: record");
    u8g2.sendBuffer();

    int x = readJoystickAxis(JOY_X_PIN);
    unsigned long now = millis();

    if (x != 0 && now - lastMoveMs > 200)
    {
      secs += x;
      if (secs < 1) secs = 1;
      if (secs > g_max_record_secs) secs = g_max_record_secs;
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
static float s_pitchUpLevel   = 0.5f;
static float s_pitchDownLevel = 0.5f;
static float s_echoLevel      = 0.5f;
static float s_ringmodLevel   = 0.5f;
static float s_stutterLevel   = 0.5f;

// Shows a full-screen sub-menu for adjusting a single effect parameter.
// Joystick X moves the level left/right in 5% steps; button confirms and returns the level.
// current is in [0,1]; minVal/maxVal are the display range with the given unit string.
static float showLevelSubMenu(const char *title, const char *paramLabel,
                               float current, float minVal, float maxVal, const char *unit)
{
  float level = current;
  unsigned long lastMoveMs = 0;

  while (true)
  {
    float actualVal = minVal + level * (maxVal - minVal);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(2, 10, title);

    // Filled progress bar
    const int BX = 2, BY = 16, BW = 124, BH = 10;
    u8g2.drawFrame(BX, BY, BW, BH);
    int filled = (int)(level * (BW - 2));
    if (filled > 0)
      u8g2.drawBox(BX + 1, BY + 1, filled, BH - 2);

    char valBuf[32];
    if (unit[0] == 'x')
      snprintf(valBuf, sizeof(valBuf), "%s: %.2f%s", paramLabel, actualVal, unit);
    else
      snprintf(valBuf, sizeof(valBuf), "%s: %.0f%s", paramLabel, actualVal, unit);
    u8g2.drawStr(2, 38, valBuf);
    u8g2.drawStr(2, 50, "< X: adjust >");
    u8g2.drawStr(2, 62, "Btn: play");
    u8g2.sendBuffer();

    int x = readJoystickAxis(JOY_X_PIN);
    unsigned long now = millis();

    if (x != 0 && now - lastMoveMs > 150)
    {
      level += x * 0.05f;
      if (level < 0.0f) level = 0.0f;
      if (level > 1.0f) level = 1.0f;
      lastMoveMs = now;
    }

    if (isJoystickButtonPressed())
    {
      while (isJoystickButtonPressed()) delay(10);
      return level;
    }

    delay(20);
  }
}

void runMenuAction(int item)
{
  // Debounce: release twhen we select an effect can we show a but menu to se the effect level. We can use the joystick x pin to set the levelshe button press that opened this action
  while (isJoystickButtonPressed()) delay(10);

  switch (item)
  {
    case MENU_RECORD:
    {
      int durSecs = showDurationSubMenu(active_sample_count / SAMPLE_RATE);
      active_sample_count = durSecs * SAMPLE_RATE;
      char durStr[32];
      snprintf(durStr, sizeof(durStr), "%ds recording...", durSecs);
      drawStatus("Recording...", durStr);
      recordToBuffer();
      drawStatus("Recording done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    }
    case MENU_PLAY:
      drawStatus("Playing raw...", nullptr);
      playBufferSimple();
      drawStatus("Playback done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    case MENU_PASSTHROUGH:
      drawStatus("Passthrough mode", "Press button to stop");
      passthrough();
      drawStatus("Passthrough stopped", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    case MENU_REVERSE:
      drawStatus("Playing reverse...", nullptr);
      playReverse();
      drawStatus("Done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    case MENU_PITCH_UP:
    {
      s_pitchUpLevel = showLevelSubMenu("Pitch Up", "Speed", s_pitchUpLevel, 1.1f, 2.5f, "x");
      float speed = 1.1f + s_pitchUpLevel * 1.4f;
      char info[32];
      snprintf(info, sizeof(info), "Speed: %.2fx", speed);
      drawStatus("Pitch up...", info);
      playResample(speed);
      drawStatus("Done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    }
    case MENU_PITCH_DOWN:
    {
      s_pitchDownLevel = showLevelSubMenu("Pitch Down", "Speed", s_pitchDownLevel, 0.3f, 0.9f, "x");
      float speed = 0.3f + s_pitchDownLevel * 0.6f;
      char info[32];
      snprintf(info, sizeof(info), "Speed: %.2fx", speed);
      drawStatus("Pitch down...", info);
      playResample(speed);
      drawStatus("Done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    }
    case MENU_ECHO:
    {
      s_echoLevel = showLevelSubMenu("Echo", "Delay", s_echoLevel, 100.0f, 500.0f, "ms");
      float delaySec = (100.0f + s_echoLevel * 400.0f) / 1000.0f;
      float decay = 0.2f + s_echoLevel * 0.5f;
      char info[32];
      snprintf(info, sizeof(info), "%.0fms dec %.2f", delaySec * 1000.0f, decay);
      drawStatus("Echo...", info);
      playEcho(delaySec, decay);
      drawStatus("Done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    }
    case MENU_RINGMOD:
    {
      s_ringmodLevel = showLevelSubMenu("Ring Mod", "Freq", s_ringmodLevel, 10.0f, 90.0f, "Hz");
      float freq = 10.0f + s_ringmodLevel * 80.0f;
      char info[32];
      snprintf(info, sizeof(info), "Freq: %.0fHz", freq);
      drawStatus("Ring mod...", info);
      playRingMod(freq);
      drawStatus("Done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    }
    case MENU_STUTTER:
    {
      // level 0 = subtle (long 200ms chunks, 2 repeats)
      // level 1 = heavy  (short 30ms chunks, 6 repeats)
      s_stutterLevel = showLevelSubMenu("Stutter", "Intensity", s_stutterLevel, 0.0f, 100.0f, "%");
      float chunkSec = (200.0f - s_stutterLevel * 0.01f * 170.0f) / 1000.0f;
      int repeats = 2 + (int)(s_stutterLevel * 0.04f);
      char info[32];
      snprintf(info, sizeof(info), "%.0fms x%d", chunkSec * 1000.0f, repeats);
      drawStatus("Stutter...", info);
      playStutter(chunkSec, repeats);
      drawStatus("Done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
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
  uint8_t chunkHdr[8];
  while (f.read(chunkHdr, 8) == 8)
  {
    uint32_t sz = (uint32_t)chunkHdr[4] | ((uint32_t)chunkHdr[5] << 8) |
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
      break;
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

  drawStatus("Audio test...", "voicemorpher.wav");
  Serial.println("Playing startup WAV...");

  uint8_t buf[512];
  size_t bytesRead;
  while ((bytesRead = f.read(buf, sizeof(buf))) > 0)
  {
    size_t bytesWritten = 0;
    i2s_write(I2S_TX_PORT, buf, bytesRead, &bytesWritten, portMAX_DELAY);
  }

  f.close();
  stopTxAndFlush();
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

void recordToBuffer()
{
  const size_t TEMP_SAMPLES = 256;
  int16_t temp[TEMP_SAMPLES]; // 16-bit to match I2S_BITS_PER_SAMPLE_16BIT
  size_t sample_offset = 0;
  Serial.printf("Recording %d seconds (%d samples)...\n", active_sample_count / SAMPLE_RATE, active_sample_count);

  while (sample_offset < active_sample_count)
  {
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

    if (isJoystickButtonPressed()) break;
  }
  stopTxAndFlush();
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

  record_buffer = (int16_t *)malloc(SAMPLE_RATE * 10 * sizeof(int16_t));
  if (!record_buffer)
  {
    Serial.println("FATAL: Failed to allocate record buffer");
    while (1) delay(1000);
  }
  Serial.println("✓ Record buffer: 10s @ 11025 Hz");

  pinMode(AMP_SD, OUTPUT);
  digitalWrite(AMP_SD, HIGH);
  Serial.printf("✓ AMP_SD enabled on GPIO%d\n", AMP_SD);

  Wire.begin(DISPLAY_SDA, DISPLAY_SCL);
  u8g2.begin();
  pinMode(JOY_BTN_PIN, INPUT_PULLUP);
  pinMode(JOY_X_PIN, INPUT);
  drawMenu();

  initI2S();
  playStartupWav();

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
