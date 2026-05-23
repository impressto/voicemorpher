#include <Arduino.h>
#include "driver/i2s.h"

// Pin definitions (adjust to your wiring)
#define I2S_BCK 4
#define I2S_WS 5
#define I2S_SD_IN 6
#define I2S_SD_OUT 7

// Audio parameters
const int SAMPLE_RATE = 16000;
const int BITS_PER_SAMPLE = 16;
const int CHANNELS = 1; // mono

// Recording buffer (seconds * sample_rate)
const int RECORD_SECONDS = 5;
const int SAMPLE_COUNT = SAMPLE_RATE * RECORD_SECONDS;
static int16_t *record_buffer = nullptr;

// I2S ports
const i2s_port_t I2S_RX_PORT = I2S_NUM_0;
const i2s_port_t I2S_TX_PORT = I2S_NUM_1;

void initI2S()
{
  // RX config (microphone)
  i2s_config_t i2s_rx_config = {};
  i2s_rx_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2s_rx_config.sample_rate = SAMPLE_RATE;
  i2s_rx_config.bits_per_sample = (i2s_bits_per_sample_t)I2S_BITS_PER_SAMPLE_16BIT;
  i2s_rx_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2s_rx_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  i2s_rx_config.dma_buf_count = 4;
  i2s_rx_config.dma_buf_len = 1024;
  i2s_rx_config.use_apll = false;

  // TX config (DAC / amplifier)
  i2s_config_t i2s_tx_config = {};
  i2s_tx_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_tx_config.sample_rate = SAMPLE_RATE;
  i2s_tx_config.bits_per_sample = (i2s_bits_per_sample_t)I2S_BITS_PER_SAMPLE_16BIT;
  i2s_tx_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT; // allow stereo framing
  i2s_tx_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  i2s_tx_config.dma_buf_count = 4;
  i2s_tx_config.dma_buf_len = 1024;
  i2s_tx_config.use_apll = false;

  // Install drivers
  i2s_driver_install(I2S_RX_PORT, &i2s_rx_config, 0, NULL);
  i2s_driver_install(I2S_TX_PORT, &i2s_tx_config, 0, NULL);

  // Pin config (share BCK and WS between devices)
  i2s_pin_config_t pin_config_rx = {};
  pin_config_rx.bck_io_num = I2S_BCK;
  pin_config_rx.ws_io_num = I2S_WS;
  pin_config_rx.data_out_num = I2S_PIN_NO_CHANGE;
  pin_config_rx.data_in_num = I2S_SD_IN;
  i2s_set_pin(I2S_RX_PORT, &pin_config_rx);

  i2s_pin_config_t pin_config_tx = {};
  pin_config_tx.bck_io_num = I2S_BCK;
  pin_config_tx.ws_io_num = I2S_WS;
  pin_config_tx.data_out_num = I2S_SD_OUT;
  pin_config_tx.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(I2S_TX_PORT, &pin_config_tx);

  i2s_zero_dma_buffer(I2S_RX_PORT);
  i2s_zero_dma_buffer(I2S_TX_PORT);
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
}

void playBufferSimple()
{
  size_t bytes_written = 0;
  size_t to_write = SAMPLE_COUNT * sizeof(int16_t);
  size_t offset = 0;
  while (offset < to_write)
  {
    size_t chunk = min((size_t)4096, to_write - offset);
    i2s_write(I2S_TX_PORT, ((uint8_t *)record_buffer) + offset, chunk, &bytes_written, portMAX_DELAY);
    offset += bytes_written;
  }
}

void playReverse()
{
  // send samples in reverse order
  for (int i = SAMPLE_COUNT - 1; i >= 0; --i)
  {
    int16_t s = record_buffer[i];
    i2s_write(I2S_TX_PORT, &s, sizeof(s), NULL, portMAX_DELAY);
  }
}

void playResample(float speed)
{
  // speed >1.0 = faster (pitch up), <1.0 = slower (pitch down)
  float idx = 0.0f;
  while ((int)idx < SAMPLE_COUNT)
  {
    int read_idx = (int)idx;
    int16_t s = record_buffer[read_idx];
    i2s_write(I2S_TX_PORT, &s, sizeof(s), NULL, portMAX_DELAY);
    idx += speed;
  }
}

void playEcho(float delaySec, float decay)
{
  int delaySamples = int(delaySec * SAMPLE_RATE);
  // create temporary buffer to output mixed samples
  for (int i = 0; i < SAMPLE_COUNT; ++i)
  {
    int32_t out = record_buffer[i];
    if (i - delaySamples >= 0)
    {
      out += int32_t(record_buffer[i - delaySamples] * decay);
      // clamp
      if (out > INT16_MAX) out = INT16_MAX;
      if (out < INT16_MIN) out = INT16_MIN;
    }
    int16_t s = (int16_t)out;
    i2s_write(I2S_TX_PORT, &s, sizeof(s), NULL, portMAX_DELAY);
  }
}

void playRingMod(float freq)
{
  for (int i = 0; i < SAMPLE_COUNT; ++i)
  {
    float t = (float)i / SAMPLE_RATE;
    float mod = sinf(2.0f * 3.14159265f * freq * t);
    int32_t out = int32_t(record_buffer[i] * mod);
    if (out > INT16_MAX) out = INT16_MAX;
    if (out < INT16_MIN) out = INT16_MIN;
    int16_t s = (int16_t)out;
    i2s_write(I2S_TX_PORT, &s, sizeof(s), NULL, portMAX_DELAY);
  }
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
  delay(10);
}
