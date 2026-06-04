#include "globals.h"

static const char *autoSlotPath() { return "/rec_auto.pcm"; }

void saveRecordingAuto()
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

bool loadRecordingAuto()
{
  const char *path = autoSlotPath();
  if (!LittleFS.exists(path)) return false;
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

void recordToBuffer()
{
  const size_t TEMP_SAMPLES = 256;
  int16_t temp[TEMP_SAMPLES];
  size_t sample_offset = 0;
  Serial.printf("Recording %d seconds (%d samples)...\n", active_sample_count / SAMPLE_RATE, active_sample_count);

  const int16_t BX = 8, BY = 46, BW = TFT_W - 16, BH = 28;
  int totalSecs = active_sample_count / SAMPLE_RATE;

  while (sample_offset < (size_t)active_sample_count)
  {
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

    for (size_t i = 0; i < read_samples && sample_offset < (size_t)active_sample_count; ++i)
    {
      record_buffer[sample_offset++] = temp[i];
    }
    yield();
  }

  // Draw final 100% frame
  tft.fillScreen(C_BG);
  drawHeader("Recording...");
  drawProgressBar(BX, BY, BW, BH, 1.0f);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(BX, BY + BH + 10);
  tft.print("0s remaining");

  Serial.printf("Recording complete. First 8 samples: %d %d %d %d %d %d %d %d\n",
    record_buffer[0], record_buffer[1], record_buffer[2], record_buffer[3],
    record_buffer[4], record_buffer[5], record_buffer[6], record_buffer[7]);
  if (g_mic_gain != 1.0f)
  {
    for (int i = 0; i < active_sample_count; ++i)
    {
      int32_t s = (int32_t)(record_buffer[i] * g_mic_gain);
      if (s > INT16_MAX) s = INT16_MAX;
      if (s < INT16_MIN) s = INT16_MIN;
      record_buffer[i] = (int16_t)s;
    }
  }
  cleanRecording();
  normalizeRecording();
  g_has_recording = true;
  playRecordingDoneBlips();
}

void playBufferSimple()
{
  openMoodPlayback();
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
  closeMoodPlayback();
  stopTxAndFlush();
  Serial.printf("Playback complete: %u bytes written\n", (unsigned int)(active_sample_count * sizeof(int16_t)));
}

void cleanRecording()
{
#if ENABLE_AUDIO_CLEANING
  const int32_t CLICK_THRESHOLD = 2500 * AUDIO_CLEANING_STRENGTH;
  for (int i = 1; i < active_sample_count - 1; ++i)
  {
    int32_t prev = record_buffer[i - 1];
    int32_t cur  = record_buffer[i];
    int32_t next = record_buffer[i + 1];
    int32_t d1 = abs(cur - prev);
    int32_t d2 = abs(cur - next);
    int32_t dn = abs(next - prev);
    if (d1 > CLICK_THRESHOLD && d2 > CLICK_THRESHOLD && dn < d1 / 2 && dn < d2 / 2)
      record_buffer[i] = (int16_t)((prev + next) / 2);
  }
#endif
}

void normalizeRecording(float targetPeak)
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

void playRecordingDoneBlips()
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
