#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
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
#define JOY_Y_PIN 20
#define JOY_BTN_PIN 35
#define JOY_LOW_THRESHOLD 1200
#define JOY_HIGH_THRESHOLD 2800

// Audio parameters
const int SAMPLE_RATE = 16000;
const int BITS_PER_SAMPLE = 16;
const int CHANNELS = 1; // mono

// Recording buffer (seconds * sample_rate)
const int RECORD_SECONDS = 10;
const int SAMPLE_COUNT = SAMPLE_RATE * RECORD_SECONDS;
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
  "Ring mod"
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

static int readJoystickAxis(int pin)
{
  int value = analogRead(pin);
  if (value < JOY_LOW_THRESHOLD) return -1;
  if (value > JOY_HIGH_THRESHOLD) return 1;
  return 0;
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
  u8g2.drawStr(0, 12, "VoiceMorpher Menu");
  for (int i = 0; i < MENU_COUNT; ++i)
  {
    int y = 24 + i * 8;
    if (i == currentMenu)
    {
      u8g2.drawBox(0, y - 8, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawStr(2, y, menuLabels[i]);
      u8g2.setDrawColor(1);
    }
    else
    {
      u8g2.drawStr(2, y, menuLabels[i]);
    }
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

void runMenuAction(int item)
{
  switch (item)
  {
    case MENU_RECORD:
      drawStatus("Recording...", "Please wait");
      recordToBuffer();
      drawStatus("Recording done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
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
      drawStatus("Pitch up...", nullptr);
      playResample(1.6f);
      drawStatus("Done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    case MENU_PITCH_DOWN:
      drawStatus("Pitch down...", nullptr);
      playResample(0.6f);
      drawStatus("Done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    case MENU_ECHO:
      drawStatus("Echo effect...", nullptr);
      playEcho(0.2f, 0.5f);
      drawStatus("Done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    case MENU_RINGMOD:
      drawStatus("Ring modulation...", nullptr);
      playRingMod(30.0f);
      drawStatus("Done.", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
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
  i2s_rx_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  i2s_rx_config.dma_buf_count = 8;
  i2s_rx_config.dma_buf_len = 1024;
  i2s_rx_config.use_apll = true;

  // TX config (DAC / amplifier)
  i2s_config_t i2s_tx_config = {};
  i2s_tx_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_tx_config.sample_rate = SAMPLE_RATE;
  i2s_tx_config.bits_per_sample = (i2s_bits_per_sample_t)I2S_BITS_PER_SAMPLE_16BIT;
  i2s_tx_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; // mono output for MAX98357A
  i2s_tx_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  i2s_tx_config.dma_buf_count = 8;
  i2s_tx_config.dma_buf_len = 1024;
  i2s_tx_config.use_apll = true;

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
  for (int i = 1; i < SAMPLE_COUNT - 1; ++i)
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

void recordToBuffer()
{
  size_t bytes_read = 0;
  size_t to_read = SAMPLE_COUNT * sizeof(int16_t);
  size_t offset = 0;
  Serial.printf("Recording %d seconds (%d samples)...\n", RECORD_SECONDS, SAMPLE_COUNT);
  while (offset < to_read)
  {
    size_t chunk = min((size_t)4096, to_read - offset);
    i2s_read(I2S_RX_PORT, ((uint8_t *)record_buffer) + offset, chunk, &bytes_read, portMAX_DELAY);
    offset += bytes_read;
  }
  Serial.println("Recording complete.");
  cleanRecording();
}

void playBufferSimple()
{
  // Check if buffer has any non-zero samples
  int32_t sum = 0;
  int nonzero_count = 0;
  for (int i = 0; i < SAMPLE_COUNT; ++i)
  {
    if (record_buffer[i] != 0) nonzero_count++;
    sum += abs(record_buffer[i]);
  }
  
  if (nonzero_count == 0)
  {
    Serial.println("ERROR: Buffer is empty! Did you record first? (send 'r')");
    return;
  }
  
  int avg_magnitude = (sum > 0) ? sum / SAMPLE_COUNT : 0;
  Serial.printf("Playing buffer: %d non-zero samples, avg magnitude: %d\n", nonzero_count, avg_magnitude);
  
  writeSamplesWithGain(record_buffer, SAMPLE_COUNT);
  stopTxAndFlush();
  Serial.printf("Playback complete: %u bytes written\n", (unsigned int)(SAMPLE_COUNT * sizeof(int16_t)));
}

void playReverse()
{
  // send samples in reverse order
  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];
  int idx = SAMPLE_COUNT - 1;

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

  while ((int)idx < SAMPLE_COUNT)
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

  for (int i = 0; i < SAMPLE_COUNT; ++i)
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

  for (int i = 0; i < SAMPLE_COUNT; ++i)
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

  record_buffer = (int16_t *)malloc(SAMPLE_COUNT * sizeof(int16_t));
  if (!record_buffer)
  {
    Serial.println("Failed to allocate record buffer. Reduce RECORD_SECONDS or enable PSRAM.");
    while (1)
      delay(1000);
  }

  pinMode(AMP_SD, OUTPUT);
  digitalWrite(AMP_SD, HIGH);
  Serial.printf("✓ AMP_SD enabled on GPIO%d\n", AMP_SD);

  Wire.begin(DISPLAY_SDA, DISPLAY_SCL);
  u8g2.begin();
  pinMode(JOY_BTN_PIN, INPUT_PULLUP);
  drawMenu();

  initI2S();

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
      Serial.println("Echo 200ms...");
      playEcho(0.2f, 0.5f);
      break;
    case '5':
      Serial.println("Ring modulation 30Hz...");
      playRingMod(30.0f);
      break;
    default:
      break;
    }
  }

  handleJoystickMenu();
  delay(10);
}
