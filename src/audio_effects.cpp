#include "globals.h"

void writeSamplesWithGain(const int16_t *src, size_t sampleCount)
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

    mixMoodInto(chunk, chunkCount);
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

void stopTxAndFlush()
{
  i2s_stop(I2S_TX_PORT);
  i2s_zero_dma_buffer(I2S_TX_PORT);
  i2s_start(I2S_TX_PORT);
}

void playReverse()
{
  openMoodPlayback();
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
    mixMoodInto(chunk, count);
    size_t bytes_written = 0;
    i2s_write(I2S_TX_PORT, chunk, count * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    if (g_waveform_visible) drawWaveformPlayhead(idx);
    if (isJoystickButtonPressed()) break;
    idx -= count;
  }
  closeMoodPlayback();
  stopTxAndFlush();
}

void playResample(float speed)
{
  openMoodPlayback();
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
      mixMoodInto(chunk, chunkIndex);
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
    mixMoodInto(chunk, chunkIndex);
    size_t bytes_written = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bytes_written, portMAX_DELAY);
  }
  closeMoodPlayback();
  stopTxAndFlush();
}

void playEcho(float delaySec, float decay)
{
  openMoodPlayback();
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
      mixMoodInto(chunk, chunkIndex);
      size_t bytes_written = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bytes_written, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }

  if (chunkIndex > 0)
  {
    mixMoodInto(chunk, chunkIndex);
    size_t bytes_written = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bytes_written, portMAX_DELAY);
  }
  closeMoodPlayback();
  stopTxAndFlush();
}

void playRingMod(float freq)
{
  openMoodPlayback();
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
      mixMoodInto(chunk, chunkIndex);
      size_t bytes_written = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bytes_written, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }

  if (chunkIndex > 0)
  {
    mixMoodInto(chunk, chunkIndex);
    size_t bytes_written = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bytes_written, portMAX_DELAY);
  }
  closeMoodPlayback();
  stopTxAndFlush();
}

void playStutter(float chunkSec, int repeats)
{
  openMoodPlayback();
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
        mixMoodInto(buf, count);
        size_t bytesWritten = 0;
        i2s_write(I2S_TX_PORT, buf, count * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        writePos += count;
      }
    }

    if (g_waveform_visible) drawWaveformPlayhead(pos);
    if (isJoystickButtonPressed()) break;
  }
  closeMoodPlayback();
  stopTxAndFlush();
}

void playTremolo(float rate)
{
  openMoodPlayback();
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
      mixMoodInto(chunk, chunkIndex);
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }

  if (chunkIndex > 0)
  {
    mixMoodInto(chunk, chunkIndex);
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  closeMoodPlayback();
  stopTxAndFlush();
}

void playHaunted(float delaySec, float decay)
{
  openMoodPlayback();
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
      mixMoodInto(chunk, chunkIndex);
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }

  if (chunkIndex > 0)
  {
    mixMoodInto(chunk, chunkIndex);
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  closeMoodPlayback();
  stopTxAndFlush();
}

void playAlien(float speed, float ringFreq)
{
  openMoodPlayback();
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
      mixMoodInto(chunk, chunkIndex);
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
    mixMoodInto(chunk, chunkIndex);
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  closeMoodPlayback();
  stopTxAndFlush();
}

void playMonster(float speed, float delaySec, float decay)
{
  openMoodPlayback();
  int delayLen = (int)(delaySec * SAMPLE_RATE) + 1;
  int16_t *echoBuf = (int16_t *)heap_caps_calloc(delayLen, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
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
    int32_t echSample = echoBuf[echoWr];
    int32_t out = sample + (int32_t)(echSample * decay);
    if (out > INT16_MAX) out = INT16_MAX;
    if (out < INT16_MIN) out = INT16_MIN;

    echoBuf[echoWr] = (int16_t)out;
    echoWr = (echoWr + 1) % delayLen;

    chunk[chunkIndex++] = applyPlaybackGain((int16_t)out);
    if (chunkIndex >= CHUNK_SAMPLES)
    {
      mixMoodInto(chunk, chunkIndex);
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
    mixMoodInto(chunk, chunkIndex);
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  free(echoBuf);
  closeMoodPlayback();
  stopTxAndFlush();
}

void playChorus(float rate, float depth)
{
  openMoodPlayback();
  const int CHORUS_LEN = (int)(0.05f * SAMPLE_RATE) + 1;
  int16_t *chorusBuf = (int16_t *)heap_caps_calloc(CHORUS_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
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
  float wet = 0.4f + depth * 0.5f;
  float dry = 1.0f - wet * 0.4f;

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
      mixMoodInto(chunk, chunkIndex);
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
      chunkIndex = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }

  if (chunkIndex > 0)
  {
    mixMoodInto(chunk, chunkIndex);
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, chunkIndex * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  free(chorusBuf);
  closeMoodPlayback();
  stopTxAndFlush();
}

void playTelephone(float hpHz)
{
  openMoodPlayback();

  const float PI2    = 2.0f * 3.14159265f;
  const float DT     = 1.0f / SAMPLE_RATE;
  const float hp_tau = 1.0f / (PI2 * hpHz);
  const float hp_a   = hp_tau / (hp_tau + DT);
  const float lp_tau = 1.0f / (PI2 * 4000.0f);
  const float lp_b   = DT   / (lp_tau + DT);

  const int CHUNK = 256;
  int16_t chunk[CHUNK];
  int ci = 0;
  float hp_x1 = 0.0f, hp_y1 = 0.0f, lp_y1 = 0.0f;

  for (int i = 0; i < active_sample_count; ++i)
  {
    float x   = (float)record_buffer[i];
    float yhp = hp_a * (hp_y1 + x - hp_x1);
    hp_x1 = x; hp_y1 = yhp;
    float ylp = lp_b * yhp + (1.0f - lp_b) * lp_y1;
    lp_y1 = ylp;

    int32_t out = (int32_t)ylp;
    if (out > INT16_MAX) out = INT16_MAX;
    if (out < INT16_MIN) out = INT16_MIN;
    chunk[ci++] = applyPlaybackGain((int16_t)out);

    if (ci >= CHUNK)
    {
      mixMoodInto(chunk, ci);
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, ci * sizeof(int16_t), &bw, portMAX_DELAY);
      ci = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }
  if (ci > 0)
  {
    mixMoodInto(chunk, ci);
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, ci * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  closeMoodPlayback();
  stopTxAndFlush();
}

void playWavefold(float threshold)
{
  openMoodPlayback();
  const int CHUNK = 256;
  int16_t chunk[CHUNK];
  int ci = 0;

  float outGain = (float)INT16_MAX / threshold;

  for (int i = 0; i < active_sample_count; ++i)
  {
    float s = (float)record_buffer[i];
    if (s > threshold)
      s = threshold - (s - threshold);
    else if (s < -threshold)
      s = -threshold - (s + threshold);

    int32_t out = (int32_t)(s * outGain);
    if (out > INT16_MAX) out = INT16_MAX;
    if (out < INT16_MIN) out = INT16_MIN;

    chunk[ci++] = applyPlaybackGain((int16_t)out);

    if (ci >= CHUNK)
    {
      mixMoodInto(chunk, ci);
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, ci * sizeof(int16_t), &bw, portMAX_DELAY);
      ci = 0;
      if (g_waveform_visible) drawWaveformPlayhead(i);
      if (isJoystickButtonPressed()) break;
    }
  }
  if (ci > 0)
  {
    mixMoodInto(chunk, ci);
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, ci * sizeof(int16_t), &bw, portMAX_DELAY);
  }
  closeMoodPlayback();
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
    if (Serial.available()) break;
  }
}

void passthroughWithEffect(int fx)
{
  // fx: 0=plain 1=echo 2=star fighter 3=tremolo 4=chorus 5=distort 6=telephone 7=pitch up 8=pitch dn
  static const char *fxNames[] = {
    "Plain", "Echo", "Star Fghtr", "Tremolo", "Chorus",
    "Distort", "Telephone", "Pitch Up", "Pitch Dn"
  };
  const char *fxLabel = (fx >= 0 && fx <= 8) ? fxNames[fx] : "Live FX";

  // Draw oscilloscope screen
  tft.fillScreen(C_BG);
  fillGradH(0, 0, TFT_W, 36, 0, 55, 140, 0, 15, 65);
  tft.drawFastHLine(0, 36, TFT_W, TFT_CYAN);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  char hdr[32];
  snprintf(hdr, sizeof(hdr), "Live: %s", fxLabel);
  tft.print(hdr);
  tft.drawRect(WF_X, WF_Y, WF_W, WF_H, tft.color565(40, 50, 100));
  tft.drawFastHLine(WF_X + 1, WF_CY, WF_W - 2, tft.color565(20, 30, 60));
  int oscCol = 0;

  const int CHUNK_SAMPLES = 256;
  int16_t chunk[CHUNK_SAMPLES];

  // Echo: 250 ms circular delay buffer
  const int ECHO_LEN = (int)(0.25f * SAMPLE_RATE);
  int16_t *echoBuf = nullptr;
  int echoWr = 0;
  if (fx == 1)
  {
    echoBuf = (int16_t *)heap_caps_calloc(ECHO_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!echoBuf) fx = 0;
  }

  // Chorus: 50 ms delay buffer, LFO sweeps read point 10–30 ms back
  const int CHORUS_LEN = (int)(0.05f * SAMPLE_RATE) + 1;
  int16_t *chorusBuf = nullptr;
  int chorusWr = 0;
  float chorusPhase = 0.0f;
  if (fx == 4)
  {
    chorusBuf = (int16_t *)heap_caps_calloc(CHORUS_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!chorusBuf) fx = 0;
  }

  // Telephone bandpass state
  float hp_x1 = 0.0f, hp_y1 = 0.0f;
  float lp_y1 = 0.0f;

  // Pitch Up/Down: granular pitch shift
  const int   PITCH_BUF      = 1024;
  const int   PITCH_GRAIN    = 256;
  const int   PITCH_FADE_LEN = 48;
  const int   PITCH_GAP      = 48;
  const float PITCH_UP       = 1.5f;
  const float PITCH_DOWN     = 0.67f;
  int16_t *pitchRing  = nullptr;
  int32_t  pitchWPos  = 0;
  float    pitchRPos  = -(float)(PITCH_GRAIN * 2);
  float    pitchRPos2 = 0.0f;
  int      pitchFade  = 0;
  if (fx == 7 || fx == 8)
  {
    pitchRing = (int16_t *)heap_caps_calloc(PITCH_BUF, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!pitchRing) fx = 0;
  }

  // Noise gate
  float gateEnv = 0.0f;
  unsigned long gateHoldUntilMs = 0;

  // Frequency-shift phase
  float freqShiftPhase = 0.0f;

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

    {
      float rawPeak = 0.0f;
      for (size_t i = 0; i < n; ++i)
      {
        float a = fabsf((float)chunk[i]);
        if (a > rawPeak) rawPeak = a;
      }
      gateEnv += (rawPeak > gateEnv ? 0.8f : 0.05f) * (rawPeak - gateEnv);
    }

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
      else if (fx == 5)  // distortion
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
        int32_t out = (int32_t)(lp * 2.0f);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }
      else if (fx == 7)  // pitch up
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

        if (pitchRPos >= 0.0f && pitchWPos - (int)pitchRPos < PITCH_GAP)
        {
          pitchRPos2 = pitchRPos;
          pitchRPos -= (float)PITCH_GRAIN;
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
      else if (fx == 8)  // pitch down
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

      // Frequency shift: 7 Hz ring-mod at 25% depth
      float shift = 0.75f + 0.25f * cosf(2.0f * 3.14159265f * freqShiftPhase);
      freqShiftPhase += 7.0f / SAMPLE_RATE;
      if (freqShiftPhase >= 1.0f) freqShiftPhase -= 1.0f;

      int32_t gained = (int32_t)((float)s * g_live_gain * shift);
      if (gained > INT16_MAX) gained = INT16_MAX;
      if (gained < INT16_MIN) gained = INT16_MIN;
      chunk[i] = (int16_t)gained;
    }

    // Gate with hold timer
    if (gateEnv < g_gate_threshold)
      gateHoldUntilMs = millis() + 200;
    if (millis() < gateHoldUntilMs)
      memset(chunk, 0, n * sizeof(int16_t));

    size_t bytes_written = 0;
    i2s_write(I2S_TX_PORT, chunk, bytes_read, &bytes_written, portMAX_DELAY);

    // Oscilloscope: draw 8 columns per chunk
    {
      const int innerW = WF_W - 2;
      const int SUBCOLS = 8;
      const int SUB = CHUNK_SAMPLES / SUBCOLS;
      const uint16_t wfColor  = tft.color565(0, 180, 220);
      const uint16_t curColor = tft.color565(50, 50, 90);
      const uint16_t ctrColor = tft.color565(20, 30, 60);
      for (int c = 0; c < SUBCOLS; ++c)
      {
        int subEnd = min((int)n, (c + 1) * SUB);
        int16_t pk = 0;
        for (int s = c * SUB; s < subEnd; ++s)
        {
          int16_t a = (int16_t)abs(chunk[s]);
          if (a > pk) pk = a;
        }
        int x = WF_X + 1 + oscCol;
        tft.drawFastVLine(x, WF_Y + 1, WF_H - 2, C_BG);
        tft.drawPixel(x, WF_CY, ctrColor);
        int amp = (int)pk * (WF_H / 2 - 2) / 8192;
        if (amp > WF_H / 2 - 2) amp = WF_H / 2 - 2;
        if (amp > 0)
          tft.drawFastVLine(x, WF_CY - amp, amp * 2 + 1, wfColor);
        oscCol = (oscCol + 1) % innerW;
        tft.drawFastVLine(WF_X + 1 + oscCol, WF_Y + 1, WF_H - 2, curColor);
      }
    }

    if (isJoystickButtonPressed() || Serial.available())
    {
      memset(chunk, 0, sizeof(chunk));
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, chunk, bytes_read, &bw, portMAX_DELAY);
      break;
    }
  }

  stopTxAndFlush();
  if (echoBuf)   free(echoBuf);
  if (chorusBuf) free(chorusBuf);
  if (pitchRing) free(pitchRing);
}
