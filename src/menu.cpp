#include "globals.h"

static float s_longPitchRate = 1.5f;   // set by pitch slider before calling playFromLittleFSWithEffect

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

// Records directly to LittleFS for long recordings
static void recordToLittleFS(int durationSecs, const char *path)
{
  if (!LittleFS.begin(true)) { drawStatus("FS Error", "LittleFS failed"); delay(1500); return; }

  size_t needed = sizeof(int32_t) + (size_t)durationSecs * SAMPLE_RATE * sizeof(int16_t);
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

  while (written < plannedSamples)
  {
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
      if (g_mic_gain != 1.0f)
      {
        for (int i = 0; i < samplesRead; ++i)
        {
          int32_t s = (int32_t)(stage[i] * g_mic_gain);
          if (s > INT16_MAX) s = INT16_MAX;
          if (s < INT16_MIN) s = INT16_MIN;
          stage[i] = (int16_t)s;
        }
      }
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

  {
    const int16_t BX = 8, BY = 46, BW = TFT_W - 16, BH = 28;
    int finalSecs = (int)(written / SAMPLE_RATE);
    tft.fillScreen(C_BG);
    drawHeader("Stored Rec");
    drawProgressBar(BX, BY, BW, BH, 1.0f);
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%ds / %ds", finalSecs, finalSecs);
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(2);
    tft.setCursor(BX, BY + BH + 10);
    tft.print(timeBuf);
  }

  f.close();

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

// fx: 0=plain 1=echo 2=star fighter 3=tremolo 4=chorus 5=pitch up 6=pitch dn 7=stutter 8=monster 9=alien 10=telephone 11=wavefold
static void playFromLittleFSWithEffect(int fx, const char *path)
{
  Serial.printf("[LongPlay] fx=%d path=%s  freeInternal=%u freeTotal=%u\n",
    fx, path,
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
    (unsigned)ESP.getFreeHeap());

  openMoodPlayback();
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
    // drawWaveformScreen is declared static in display.cpp — call it via a local redraw
    // We replicate just the screen draw here since drawWaveformScreen is file-static
    // Actually, we need to promote drawWaveformScreen. Let's call the public waveform
    // setup path: the screen was already set up by fillStoredWaveformBuf + title draw below.
    // Re-implement the title+box draw inline:
    {
      g_wf_last_px = -1;
      {
        int peak = 1;
        for (int px = 0; px < WF_W - 2; ++px)
          if (abs(g_wf_peaks[px]) > peak) peak = abs(g_wf_peaks[px]);
        g_wf_peak = peak > 1000 ? peak : 1000;
      }
      tft.fillScreen(C_BG);
      fillGradH(0, 0, TFT_W, 36, 0, 55, 140, 0, 15, 65);
      tft.drawFastHLine(0, 36, TFT_W, TFT_CYAN);
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(2);
      tft.setCursor(8, 8);
      tft.print("Stored Play");
      tft.drawRect(WF_X, WF_Y, WF_W, WF_H, tft.color565(40, 50, 100));
      tft.drawFastHLine(WF_X + 1, WF_CY, WF_W - 2, tft.color565(20, 30, 60));
      for (int px = 0; px < WF_W - 2; ++px)
        drawWfColumn(px);
    }
  }

  // Pitch Up — granular synthesis, duration-preserving (early return)
  if (fx == 5)
  {
    int32_t played = 0;
    const int   PB_LEN   = 2048;
    const int   PB_GRAIN = 441;
    const int   PB_FADE  = 48;
    const int   PB_GAP   = 100;
    const float PB_RATE  = s_longPitchRate;

    int16_t *ring = (int16_t *)heap_caps_calloc(PB_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!ring && psramFound())
      ring = (int16_t *)heap_caps_calloc(PB_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    Serial.printf("[LongPlay] Pitch Up ring alloc: %s  totalSamples=%d\n", ring ? "OK" : "FAILED", (int)totalSamples);
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
        mixMoodInto(pb, got);
        size_t bw = 0;
        i2s_write(I2S_TX_PORT, pb, got * sizeof(int16_t), &bw, portMAX_DELAY);
        played += got;
        if (g_waveform_visible) drawWaveformPlayhead((int)played);
        if (isJoystickButtonPressed()) break;
      }
      f.close();
      closeMoodPlayback();
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

      if ((pitchWPos - (int)pitchRPos) < PB_GAP)
      {
        pitchRPos2 = pitchRPos;
        pitchRPos -= (float)PB_GRAIN;
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
        mixMoodInto(outBuf, outIdx);
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
      mixMoodInto(outBuf, outIdx);
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, outBuf, outIdx * sizeof(int16_t), &bw, portMAX_DELAY);
    }

    free(ring);
    f.close();
    closeMoodPlayback();
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
    const float PB_RATE  = s_longPitchRate;

    int16_t *ring = (int16_t *)heap_caps_calloc(PB_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!ring && psramFound())
      ring = (int16_t *)heap_caps_calloc(PB_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    Serial.printf("[LongPlay] Pitch Dn ring alloc: %s  totalSamples=%d\n", ring ? "OK" : "FAILED", (int)totalSamples);
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
        mixMoodInto(pb, got);
        size_t bw = 0;
        i2s_write(I2S_TX_PORT, pb, got * sizeof(int16_t), &bw, portMAX_DELAY);
        played += got;
        if (g_waveform_visible) drawWaveformPlayhead((int)played);
        if (isJoystickButtonPressed()) break;
      }
      f.close();
      closeMoodPlayback();
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
        mixMoodInto(outBuf, outIdx);
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
      mixMoodInto(outBuf, outIdx);
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, outBuf, outIdx * sizeof(int16_t), &bw, portMAX_DELAY);
    }

    free(ring);
    f.close();
    closeMoodPlayback();
    stopTxAndFlush();
    return;
  }

  // Stutter: repeat each 100ms chunk 3 times by seeking back in the file
  if (fx == 7)
  {
    const int STUTTER_SAMPLES = (int)(0.100f * SAMPLE_RATE);
    const int STUTTER_REPEATS = 3;
    const int WCHUNK = 256;
    int16_t wbuf[WCHUNK];
    int32_t played = 0;

    while (played < totalSamples)
    {
      int32_t chunkLen   = min((int32_t)STUTTER_SAMPLES, totalSamples - played);
      size_t  chunkStart = f.position();

      for (int rep = 0; rep < STUTTER_REPEATS; ++rep)
      {
        f.seek(chunkStart);
        int32_t repDone = 0;
        while (repDone < chunkLen)
        {
          int toRead = (int)min((int32_t)WCHUNK, chunkLen - repDone);
          int got = (int)(f.read((uint8_t *)wbuf, toRead * sizeof(int16_t)) / sizeof(int16_t));
          if (got == 0) break;
          for (int i = 0; i < got; ++i) wbuf[i] = applyPlaybackGain(wbuf[i]);
          mixMoodInto(wbuf, got);
          size_t bw = 0;
          i2s_write(I2S_TX_PORT, wbuf, got * sizeof(int16_t), &bw, portMAX_DELAY);
          repDone += got;
        }
      }

      played += chunkLen;
      f.seek(chunkStart + (size_t)chunkLen * sizeof(int16_t));
      if (g_waveform_visible) drawWaveformPlayhead((int)played);
      if (isJoystickButtonPressed()) break;
    }

    f.close();
    closeMoodPlayback();
    stopTxAndFlush();
    return;
  }

  // Monster: pitch-down granular + 300ms echo
  if (fx == 8)
  {
    int32_t played = 0;
    const int   PB_LEN   = 2048;
    const int   PB_GRAIN = 441;
    const int   PB_FADE  = 48;
    const int   PB_GAP   = 100;
    const float PB_RATE  = 0.67f;

    const int MON_ECHO_LEN = (int)(0.30f * SAMPLE_RATE) + 1;
    const float MON_DECAY  = 0.45f;
    int16_t *monEcho = (int16_t *)heap_caps_calloc(MON_ECHO_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    int monEchoWr = 0;

    int16_t *ring = (int16_t *)heap_caps_calloc(PB_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!ring)
    {
      if (monEcho) free(monEcho);
      int16_t pb[128];
      while (played < totalSamples)
      {
        int32_t rem = totalSamples - played;
        int n = (int)(rem < 128 ? rem : 128);
        int got = (int)(f.read((uint8_t *)pb, n * sizeof(int16_t)) / sizeof(int16_t));
        if (got == 0) break;
        for (int i = 0; i < got; ++i) pb[i] = applyPlaybackGain(pb[i]);
        mixMoodInto(pb, got);
        size_t bw = 0;
        i2s_write(I2S_TX_PORT, pb, got * sizeof(int16_t), &bw, portMAX_DELAY);
        played += got;
        if (g_waveform_visible) drawWaveformPlayhead((int)played);
        if (isJoystickButtonPressed()) break;
      }
      f.close();
      closeMoodPlayback();
      stopTxAndFlush();
      return;
    }

    const int FS = 128;
    int16_t fstage[FS];
    int fstageIdx = 0, fstageLen = 0;
    int32_t fileConsumed = 0;
    int32_t pitchWPos = 0;
    float pitchRPos = 0.0f, pitchRPos2 = 0.0f;
    int pitchFade = 0;

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
      pitchWPos++; fileConsumed++;
    }

    int16_t outBuf[FS];
    int outIdx = 0;
    bool cancelled = false;

    while (played < totalSamples && !cancelled)
    {
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
          pitchWPos++; fileConsumed++;
        }
      }

      float outF;
      if (pitchFade > 0)
      {
        float alpha = (float)pitchFade / PB_FADE;
        int i0 = (int)pitchRPos % PB_LEN;
        float fr1 = pitchRPos - floorf(pitchRPos);
        float s1 = (1.0f - fr1) * ring[i0] + fr1 * ring[(i0 + 1) % PB_LEN];
        int j0 = (int)pitchRPos2 % PB_LEN;
        float fr2 = pitchRPos2 - floorf(pitchRPos2);
        float s2 = (1.0f - fr2) * ring[j0] + fr2 * ring[(j0 + 1) % PB_LEN];
        outF = (1.0f - alpha) * s1 + alpha * s2;
        pitchRPos2 += PB_RATE; pitchFade--;
      }
      else
      {
        int i0 = (int)pitchRPos % PB_LEN;
        float fr = pitchRPos - floorf(pitchRPos);
        outF = (1.0f - fr) * ring[i0] + fr * ring[(i0 + 1) % PB_LEN];
      }
      pitchRPos += PB_RATE;

      if ((pitchWPos - (int)pitchRPos) > PB_LEN / 2)
      {
        pitchRPos2 = pitchRPos;
        pitchRPos += (float)PB_GRAIN;
        pitchFade = PB_FADE;
      }

      if (pitchWPos > PB_LEN * 16)
      {
        int sub = (pitchWPos / PB_LEN - 8) * PB_LEN;
        pitchWPos -= sub;
        pitchRPos -= (float)sub;
        pitchRPos2 -= (float)sub;
      }

      int32_t pitched = (int32_t)outF;
      if (pitched > INT16_MAX) pitched = INT16_MAX;
      if (pitched < INT16_MIN) pitched = INT16_MIN;
      int32_t out = pitched + (int32_t)(monEcho[monEchoWr] * MON_DECAY);
      if (out > INT16_MAX) out = INT16_MAX;
      if (out < INT16_MIN) out = INT16_MIN;
      monEcho[monEchoWr] = (int16_t)out;
      monEchoWr = (monEchoWr + 1) % MON_ECHO_LEN;

      outBuf[outIdx++] = applyPlaybackGain((int16_t)out);

      if (outIdx >= FS)
      {
        mixMoodInto(outBuf, outIdx);
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
      mixMoodInto(outBuf, outIdx);
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, outBuf, outIdx * sizeof(int16_t), &bw, portMAX_DELAY);
    }

    free(ring);
    if (monEcho) free(monEcho);
    f.close();
    closeMoodPlayback();
    stopTxAndFlush();
    return;
  }

  // Alien: pitch-up granular + ring modulation
  if (fx == 9)
  {
    int32_t played = 0;
    const int   PB_LEN   = 2048;
    const int   PB_GRAIN = 441;
    const int   PB_FADE  = 48;
    const int   PB_GAP   = 100;
    const float PB_RATE  = 1.5f;
    const float RING_FREQ = 50.0f;
    float ringPhase = 0.0f;

    int16_t *ring = (int16_t *)heap_caps_calloc(PB_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
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
        mixMoodInto(pb, got);
        size_t bw = 0;
        i2s_write(I2S_TX_PORT, pb, got * sizeof(int16_t), &bw, portMAX_DELAY);
        played += got;
        if (g_waveform_visible) drawWaveformPlayhead((int)played);
        if (isJoystickButtonPressed()) break;
      }
      f.close();
      closeMoodPlayback();
      stopTxAndFlush();
      return;
    }

    const int FS = 128;
    int16_t fstage[FS];
    int fstageIdx = 0, fstageLen = 0;
    int32_t fileConsumed = 0;
    int32_t pitchWPos = 0;
    float pitchRPos = 0.0f, pitchRPos2 = 0.0f;
    int pitchFade = 0;

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
      pitchWPos++; fileConsumed++;
    }

    int16_t outBuf[FS];
    int outIdx = 0;
    bool cancelled = false;

    while (played < totalSamples && !cancelled)
    {
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
        pitchWPos++; fileConsumed++;
      }

      float outF;
      if (pitchFade > 0)
      {
        float alpha = (float)pitchFade / PB_FADE;
        int i0 = (int)pitchRPos % PB_LEN;
        float fr1 = pitchRPos - floorf(pitchRPos);
        float s1 = (1.0f - fr1) * ring[i0] + fr1 * ring[(i0 + 1) % PB_LEN];
        int j0 = (int)pitchRPos2 % PB_LEN;
        float fr2 = pitchRPos2 - floorf(pitchRPos2);
        float s2 = (1.0f - fr2) * ring[j0] + fr2 * ring[(j0 + 1) % PB_LEN];
        outF = (1.0f - alpha) * s1 + alpha * s2;
        pitchRPos2 += PB_RATE; pitchFade--;
      }
      else
      {
        int i0 = (int)pitchRPos % PB_LEN;
        float fr = pitchRPos - floorf(pitchRPos);
        outF = (1.0f - fr) * ring[i0] + fr * ring[(i0 + 1) % PB_LEN];
      }
      pitchRPos += PB_RATE;

      if ((pitchWPos - (int)pitchRPos) < PB_GAP)
      {
        pitchRPos2 = pitchRPos;
        pitchRPos -= (float)PB_GRAIN;
        pitchFade = PB_FADE;
      }

      if (pitchWPos > PB_LEN * 16)
      {
        int sub = (pitchWPos / PB_LEN - 8) * PB_LEN;
        pitchWPos -= sub;
        pitchRPos -= (float)sub;
        pitchRPos2 -= (float)sub;
      }

      float mod = sinf(2.0f * 3.14159265f * ringPhase);
      ringPhase += RING_FREQ / SAMPLE_RATE;
      if (ringPhase >= 1.0f) ringPhase -= 1.0f;
      int32_t out = (int32_t)(outF * mod);
      if (out > INT16_MAX) out = INT16_MAX;
      if (out < INT16_MIN) out = INT16_MIN;

      outBuf[outIdx++] = applyPlaybackGain((int16_t)out);

      if (outIdx >= FS)
      {
        mixMoodInto(outBuf, outIdx);
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
      mixMoodInto(outBuf, outIdx);
      size_t bw = 0;
      i2s_write(I2S_TX_PORT, outBuf, outIdx * sizeof(int16_t), &bw, portMAX_DELAY);
    }

    free(ring);
    f.close();
    closeMoodPlayback();
    stopTxAndFlush();
    return;
  }

  // Echo: 250ms circular delay buffer
  const int ECHO_LEN = (int)(0.25f * SAMPLE_RATE);
  int16_t *echoBuf = nullptr;
  int echoWr = 0;
  if (fx == 1)
  {
    echoBuf = (int16_t *)heap_caps_calloc(ECHO_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!echoBuf) fx = 0;
  }

  // Chorus: 50ms delay buffer, LFO sweeps read point 10–30ms back
  const int CHORUS_LEN = (int)(0.05f * SAMPLE_RATE) + 1;
  int16_t *chorusBuf = nullptr;
  int chorusWr = 0;
  float chorusPhase = 0.0f;
  if (fx == 4)
  {
    chorusBuf = (int16_t *)heap_caps_calloc(CHORUS_LEN, sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!chorusBuf) fx = 0;
  }

  // Phase accumulators and filter state for math-only effects
  float ringPhase = 0.0f;
  float tremPhase = 0.0f;
  const float RING_FREQ  = 50.0f;
  const float TREM_RATE  = 6.0f;
  const float TREM_DEPTH = 0.85f;
  float tel_hp_x1 = 0.0f, tel_hp_y1 = 0.0f, tel_lp_y1 = 0.0f;
  float tel_hp_a  = 0.0f, tel_lp_b   = 0.0f;
  if (fx == 10) {
    const float PI2 = 2.0f * 3.14159265f;
    const float DT  = 1.0f / SAMPLE_RATE;
    float hp_tau  = 1.0f / (PI2 * 800.0f);
    tel_hp_a      = hp_tau / (hp_tau + DT);
    float lp_tau  = 1.0f / (PI2 * 4000.0f);
    tel_lp_b      = DT    / (lp_tau + DT);
  }

  const float WF_THRESH   = 4000.0f;
  const float WF_OUT_GAIN = (float)INT16_MAX / WF_THRESH;

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
      else if (fx == 10)  // telephone HP+LP band-pass
      {
        float x   = (float)s;
        float yhp = tel_hp_a * (tel_hp_y1 + x - tel_hp_x1);
        tel_hp_x1 = x; tel_hp_y1 = yhp;
        float ylp = tel_lp_b * yhp + (1.0f - tel_lp_b) * tel_lp_y1;
        tel_lp_y1 = ylp;
        int32_t out = (int32_t)ylp;
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }
      else if (fx == 11)  // wavefold
      {
        float fs = (float)s;
        if (fs > WF_THRESH)
          fs = WF_THRESH - (fs - WF_THRESH);
        else if (fs < -WF_THRESH)
          fs = -WF_THRESH - (fs + WF_THRESH);
        int32_t out = (int32_t)(fs * WF_OUT_GAIN);
        if (out > INT16_MAX) out = INT16_MAX;
        if (out < INT16_MIN) out = INT16_MIN;
        s = out;
      }

      chunk[i] = applyPlaybackGain((int16_t)s);
    }

    mixMoodInto(chunk, samplesRead);
    size_t bw = 0;
    i2s_write(I2S_TX_PORT, chunk, samplesRead * sizeof(int16_t), &bw, portMAX_DELAY);
    played += samplesRead;
    if (g_waveform_visible) drawWaveformPlayhead((int)played);

    if (isJoystickButtonPressed()) break;
  }

  f.close();
  if (echoBuf)   free(echoBuf);
  if (chorusBuf) free(chorusBuf);
  closeMoodPlayback();
  stopTxAndFlush();
}

// ── Sub-menus ────────────────────────────────────────────────────────────────

int showPassthroughFxSubMenu()
{
  static const char *choices[] = { "Plain", "Echo", "Star Fighter", "Tremolo", "Chorus", "Distort", "Telephone", "Pitch Up", "Pitch Dn", "Delay 2s" };
  const int NUM_CHOICES = 10;
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

int showLongPlayFxSubMenu()
{
  static const char *labels[] = {
    "Plain",    "Echo",      "Star Fghtr", "Tremolo",  "Chorus",
    "Pitch",    "Stutter",   "Monster",    "Alien",
    "Telephone","Wavefold"
  };
  static const uint8_t *icons[] = {
    ICON_PLAY,    ICON_ECHO,    ICON_RINGMOD,  ICON_TREMOLO, ICON_CHORUS,
    ICON_PITCH,   ICON_STUTTER, ICON_MONSTER,  ICON_ALIEN,
    ICON_TELEPHONE, ICON_WAVEFOLD
  };
  static int sel = 0;
  return showIconList(labels, icons, 11, sel);
}

int showSettingsSubMenu()
{
  static int sel = MENU_STORAGE_COUNT;
  unsigned long lastMoveMs = 0;
  const int settingsCount = MENU_SETTINGS_COUNT - MENU_STORAGE_COUNT;
  while (isJoystickButtonPressed()) delay(10);

  int prevSel = -1;

  while (true)
  {
    if (sel != prevSel)
    {
      prevSel = sel;
      tft.fillScreen(C_BG);
      tft.setTextSize(2);

      for (int i = 0; i < settingsCount; ++i)
      {
        int itemEnum = MENU_STORAGE_COUNT + i;
        int16_t y = i * ITEM_H;
        uint8_t r = ITEM_RGB[itemEnum][0], g = ITEM_RGB[itemEnum][1], b = ITEM_RGB[itemEnum][2];
        uint16_t accentCol = tft.color565(r, g, b);
        uint16_t iconColor;
        if (itemEnum == sel)
        {
          fillGradH(0, y, TFT_W, ITEM_H - 1, r/4, g/4, b/4, r/10, g/10, b/10);
          tft.fillRect(0, y, 4, ITEM_H - 1, accentCol);
          tft.setTextColor(TFT_WHITE);
          iconColor = TFT_WHITE;
        }
        else
        {
          tft.fillRect(0, y, TFT_W, ITEM_H - 1, tft.color565(r/12, g/12, b/12));
          tft.fillRect(0, y, 4, ITEM_H - 1, tft.color565(r/3, g/3, b/3));
          uint16_t tc = tft.color565((uint8_t)((int)r*3/4), (uint8_t)((int)g*3/4), (uint8_t)((int)b*3/4));
          tft.setTextColor(tc);
          iconColor = tc;
        }
        tft.drawFastHLine(0, y + ITEM_H - 1, TFT_W, tft.color565(r/8, g/8, b/8));
        tft.drawBitmap(6, y + 5, MENU_ICONS[itemEnum], 16, 16, iconColor);
        tft.setCursor(28, y + 5);
        tft.print(menuLabels[itemEnum]);
      }

      tft.setTextSize(1);
      tft.setTextColor(COL_GRAY);
      tft.setCursor(4, settingsCount * ITEM_H + 4);
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
      if (sel >= MENU_SETTINGS_COUNT) sel = MENU_SETTINGS_COUNT - 1;
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

int showPlaySubMenu()
{
  static const char *labels[] = {
    "Plain",    "Reverse",  "Pitch",   "Echo",      "Ring Mod",
    "Stutter",  "Tremolo",  "Haunted", "Alien",     "Monster",
    "Chorus",   "Telephone"
  };
  static const uint8_t *icons[] = {
    ICON_PLAY,    ICON_REVERSE,  ICON_PITCH,    ICON_ECHO,    ICON_RINGMOD,
    ICON_STUTTER, ICON_TREMOLO,  ICON_HAUNTED,  ICON_ALIEN,   ICON_MONSTER,
    ICON_CHORUS,  ICON_TELEPHONE
  };
  static int sel = 0;
  return showIconList(labels, icons, 12, sel);
}

int showMoodPickerScreen()
{
  static const uint8_t *icons[] = {
    ICON_MOOD, ICON_MOOD, ICON_MOOD, ICON_MOOD, ICON_MOOD, ICON_MOOD, ICON_MOOD
  };
  int sel = g_mood;
  return showIconList(MOOD_NAMES, icons, MOOD_COUNT, sel);
}

void runMoodMenu()
{
  static const char  *items[]  = { "Select Mood", "Mood Volume" };
  static const uint8_t *icons[] = { ICON_MOOD, ICON_VOLUME };
  const int COUNT = 2;
  int sel = 0;
  unsigned long lastMoveMs = 0;
  while (isJoystickButtonPressed()) delay(10);

  int prevSel = -1;

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
      snprintf(hint1, sizeof(hint1), "Mood: %s", MOOD_NAMES[g_mood]);
      snprintf(hint2, sizeof(hint2), "Vol: %.0f%%   < X:back", s_moodVolLevel * 100.0f);
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

      if (sel == 0)  // Select Mood
      {
        int newMood = showMoodPickerScreen();
        if (newMood >= 0 && newMood != g_mood)
        {
          g_mood = newMood;
          g_prefs.putInt("mood", g_mood);
          drawStatus("Loading...", MOOD_NAMES[g_mood]);
          loadMoodTrack(g_mood);
        }
      }
      else  // Mood Volume
      {
        float lvl = showLevelSubMenu("Mood Volume", "Level", s_moodVolLevel, 0.0f, 100.0f, "%");
        if (lvl >= 0.0f)
        {
          s_moodVolLevel = lvl;
          g_mood_gain = lvl * 0.5f;
          g_prefs.putFloat("mood_vol", g_mood_gain);
          char info[32];
          snprintf(info, sizeof(info), "Vol: %.0f%% saved", lvl * 100.0f);
          drawStatus("Mood vol saved!", info);
          delay(1000);
        }
      }
      prevSel = -1;
    }
    delay(10);
  }
}

void runWifiSettingsMenu()
{
  static const char    *items[] = { "Edit SSID", "Edit Password", "Reconnect" };
  static const uint8_t *icons[]  = { ICON_WIFI, ICON_WIFI, ICON_WIFI };
  const int COUNT = 3;
  int sel = 0;
  unsigned long lastMoveMs = 0;
  while (isJoystickButtonPressed()) delay(10);

  int prevSel = -1;

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

      char hint1[40], hint2[40];
      if (g_wifi_connected)
        snprintf(hint1, sizeof(hint1), "Connected: %s", g_wifi_ssid);
      else
        snprintf(hint1, sizeof(hint1), "Not connected: %s", g_wifi_ssid);
      snprintf(hint2, sizeof(hint2), "IP: %s   < X:back",
               g_wifi_connected ? g_wifi_ip.c_str() : "-");
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
      switch (sel)
      {
        case 0: // Edit SSID
        {
          char tmp[33];
          strncpy(tmp, g_wifi_ssid, sizeof(tmp) - 1);
          tmp[sizeof(tmp) - 1] = '\0';
          if (showTextKeyboard("Edit SSID", tmp, sizeof(tmp), false))
          {
            strncpy(g_wifi_ssid, tmp, sizeof(g_wifi_ssid) - 1);
            g_wifi_ssid[sizeof(g_wifi_ssid) - 1] = '\0';
            g_prefs.putString("wifi_ssid", g_wifi_ssid);
          }
          break;
        }
        case 1: // Edit Password
        {
          char tmp[64];
          strncpy(tmp, g_wifi_pass, sizeof(tmp) - 1);
          tmp[sizeof(tmp) - 1] = '\0';
          if (showTextKeyboard("Edit Password", tmp, sizeof(tmp), true))
          {
            strncpy(g_wifi_pass, tmp, sizeof(g_wifi_pass) - 1);
            g_wifi_pass[sizeof(g_wifi_pass) - 1] = '\0';
            g_prefs.putString("wifi_pass", g_wifi_pass);
          }
          break;
        }
        case 2: // Reconnect
          drawStatus("Connecting...", g_wifi_ssid);
          if (reconnectWiFi(g_wifi_ssid, g_wifi_pass, WIFI_CONNECT_TIMEOUT_MS))
          {
            setupTimeSync();
            drawStatus("Connected!", g_wifi_ip.c_str());
          }
          else
            drawStatus("Connect failed", "Check SSID/password");
          delay(1500);
          break;
      }
      prevSel = -1;
    }
    delay(10);
  }
}

void runMenuAction(int item)
{
  while (isJoystickButtonPressed()) delay(10);

  bool isSettingsItem = (item >= MENU_STORAGE_COUNT && item < MENU_SETTINGS_COUNT);
  if (!g_has_recording && !isSettingsItem && item != MENU_RECORD && item != MENU_PASSTHROUGH
      && item != MENU_LONG_REC && item != MENU_LONG_PLAY
      && item != MENU_MOOD && item != MENU_THEREMIN && item != MENU_RADIO
      && item != MENU_ALARM && item != MENU_MATHSYNTH && item != MENU_SETTINGS)
  {
    drawStatus("No recording!", "Record first");
    delay(1500);
    drawMenu();
    return;
  }

  // Helper lambda to draw waveform screen (needed since drawWaveformScreen is static in display.cpp)
  // We expose it via a local inline that replicates the draw.
  // Note: for the Play/effects path we call the public fillStoredWaveformBuf path is not needed.
  // Instead we use a wrapper that sets up the screen manually (same as original drawWaveformScreen).
  auto doWaveformScreen = [](const char *title) {
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
      // Normalize freq to color inline
      float f_min = 1e9f, f_max = 0.0f;
      for (int i = 0; i < innerW; ++i) {
        if (raw_freq[i] > 0 && raw_freq[i] < f_min) f_min = raw_freq[i];
        if (raw_freq[i] > f_max) f_max = raw_freq[i];
      }
      float f_range = f_max - f_min;
      for (int i = 0; i < innerW; ++i) {
        float t = (f_range > 1.0f && raw_freq[i] > 0) ? (raw_freq[i] - f_min) / f_range : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        g_wf_freqt[i] = (uint8_t)(t * 255.0f);
      }
    }
    for (int px = 0; px < WF_W - 2; ++px)
      drawWfColumn(px);
  };

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
    {
      while (true)
      {
        int sel = showPlaySubMenu();
        if (sel < 0) break;
        if (sel == 0)
        {
          g_waveform_visible = true;
          doWaveformScreen("Play");
          playBufferSimple();
          g_waveform_visible = false;
        }
        else
        {
          runMenuAction(MENU_SETTINGS_COUNT + sel - 1);
        }
      }
      break;
    }
    case MENU_THEREMIN:
      runThereminMenu();
      drawMenu();
      break;
    case MENU_MATHSYNTH:
      runMathSynthMenu();
      drawMenu();
      break;
    case MENU_SETTINGS:
    {
      while (true)
      {
        int itm = showSettingsSubMenu();
        if (itm < 0) break;
        runMenuAction(itm);
      }
      break;
    }
    case MENU_PASSTHROUGH:
    {
      int fx = showPassthroughFxSubMenu();
      passthroughWithEffect(fx);
      drawStatus("Passthrough done", "Press button");
      while (!isJoystickButtonPressed()) delay(50);
      break;
    }
    case MENU_REVERSE:
      g_waveform_visible = true;
      doWaveformScreen("Reverse");
      playReverse();
      g_waveform_visible = false;
      break;
    case MENU_PITCH:
    {
      for (;;)
      {
        float lvl = showLevelSubMenu("Pitch", "Speed", s_pitchLevel, 0.3f, 2.5f, "x");
        if (lvl < 0.0f) break;
        s_pitchLevel = lvl;
        float speed = 0.3f + s_pitchLevel * 2.2f;
        char info[32];
        snprintf(info, sizeof(info), "Speed: %.2fx", speed);
        g_waveform_visible = true;
        doWaveformScreen("Pitch");
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
        doWaveformScreen("Echo");
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
        doWaveformScreen("Star fighter");
        playRingMod(freq);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_STUTTER:
    {
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
        doWaveformScreen("Stutter");
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
        doWaveformScreen("Tremolo");
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
        doWaveformScreen("Haunted");
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
        doWaveformScreen("Alien");
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
        doWaveformScreen("Monster");
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
        doWaveformScreen("Chorus");
        playChorus(rate, depth);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_TELEPHONE:
    {
      for (;;)
      {
        float lvl = showLevelSubMenu("Telephone", "HP cutoff", s_telephoneLevel, 300.0f, 2000.0f, "Hz");
        if (lvl < 0.0f) break;
        s_telephoneLevel = lvl;
        float hpHz = 300.0f + lvl * 1700.0f;
        g_waveform_visible = true;
        doWaveformScreen("Telephone");
        playTelephone(hpHz);
        g_waveform_visible = false;
      }
      break;
    }
    case MENU_WAVEFOLD:
    {
      for (;;)
      {
        float lvl = showLevelSubMenu("Wavefold", "Fold depth", s_wavefoldLevel, 0.0f, 100.0f, "%");
        if (lvl < 0.0f) break;
        s_wavefoldLevel = lvl;
        float threshold = 30000.0f - lvl * 28000.0f;
        char info[32];
        snprintf(info, sizeof(info), "Thr: %.0f", threshold);
        g_waveform_visible = true;
        doWaveformScreen("Wavefold");
        playWavefold(threshold);
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
      // Menu has 11 items (Pitch Up/Dn merged). Map to internal fx 0-11 (which skips no 6).
      // menuSel: 0 1 2 3 4  5  6  7  8   9  10
      // fx:      0 1 2 3 4  *  7  8  9  10  11   (* = 5 or 6 chosen by rate)
      static const int LP_FX_MAP[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11 };
      while (true)
      {
        int menuSel = showLongPlayFxSubMenu();
        if (menuSel < 0) break;
        int fx = LP_FX_MAP[menuSel];
        if (menuSel == 5)  // "Pitch" — show speed slider, then pick algorithm by rate
        {
          float initLvl = s_pitchLevel > 0.01f ? s_pitchLevel : 0.545f;  // default ≈1.5×
          float lvl = showLevelSubMenu("Pitch", "Speed", initLvl, 0.3f, 2.5f, "x");
          if (lvl < 0.0f) continue;
          s_pitchLevel    = lvl;
          s_longPitchRate = 0.3f + lvl * 2.2f;
          fx = (s_longPitchRate >= 1.0f) ? 5 : 6;  // up algorithm or down algorithm
        }
        g_waveform_visible = true;
        playFromLittleFSWithEffect(fx, longSlotPath(slot));
        g_waveform_visible = false;
        g_wf_use_peaks = false;
        g_wf_total = 0;
      }
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
    case MENU_FEEDBACK:
    {
      float lvl = showLevelSubMenu("Feedback Gate", "Threshold", s_gateLevel, 0.0f, 3000.0f, "");
      if (lvl >= 0.0f)
      {
        s_gateLevel = lvl;
        g_gate_threshold = lvl * 3000.0f;
        g_prefs.putFloat("gate_thresh", g_gate_threshold);
        char info[32];
        snprintf(info, sizeof(info), "%.0f saved", g_gate_threshold);
        drawStatus("Gate saved!", info);
        delay(1000);
      }
      break;
    }
    case MENU_LIVE_GAIN:
    {
      float lvl = showLevelSubMenu("Live Gain", "Gain", s_liveGainLevel, 0.5f, 6.0f, "x");
      if (lvl >= 0.0f)
      {
        s_liveGainLevel = lvl;
        g_live_gain = 0.5f + lvl * 5.5f;
        g_prefs.putFloat("live_gain", g_live_gain);
        char info[32];
        snprintf(info, sizeof(info), "Gain: %.2fx saved", g_live_gain);
        drawStatus("Live gain saved!", info);
        delay(1000);
      }
      break;
    }
    case MENU_MIC_GAIN:
    {
      float lvl = showLevelSubMenu("Mic Gain", "Sensitivity", s_micGainLevel, 0.1f, 2.0f, "x");
      if (lvl >= 0.0f)
      {
        s_micGainLevel = lvl;
        g_mic_gain = 0.1f + lvl * 1.9f;
        g_prefs.putFloat("mic_gain", g_mic_gain);
        char info[32];
        snprintf(info, sizeof(info), "Gain: %.2fx saved", g_mic_gain);
        drawStatus("Mic gain saved!", info);
        delay(1000);
      }
      break;
    }
    case MENU_CALIBRATE_JOY:
      calibrateThereminJoy();
      calibrateJoystickX();
      break;
    case MENU_WAVELAB_VOL:
    {
      float lvl = showLevelSubMenu("WaveLab Vol", "Max level", s_wavLabVolLevel, 10.0f, 100.0f, "%");
      if (lvl >= 0.0f)
      {
        s_wavLabVolLevel = lvl;
        g_wl_max_amp = 28000.0f * (0.1f + lvl * 0.9f);
        g_prefs.putFloat("wl_vol", g_wl_max_amp);
        char info[32];
        snprintf(info, sizeof(info), "Max: %.0f%% saved", 10.0f + lvl * 90.0f);
        drawStatus("WaveLab vol saved!", info);
        delay(1000);
      }
      break;
    }
    case MENU_WIFI:
      runWifiSettingsMenu();
      break;
    case MENU_MOOD:
      runMoodMenu();
      break;
    case MENU_RADIO:
      runRadioMenu();
      drawMenu();
      break;
    case MENU_ALARM:
      runAlarmClockMenu();
      drawMenu();
      break;
    default:
      break;
  }
  drawMenu();
}
