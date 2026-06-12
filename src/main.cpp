#include "globals.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ── Global variable definitions ──────────────────────────────────────────────

Preferences        g_prefs;
TFT_eSPI           tft = TFT_eSPI();
const i2s_port_t   I2S_RX_PORT = I2S_NUM_0;
const i2s_port_t   I2S_TX_PORT = I2S_NUM_1;

// Audio parameters (local to main.cpp — not needed across translation units)
const int BITS_PER_SAMPLE = 16;
const int CHANNELS = 1;

int     active_sample_count = SAMPLE_RATE * 5;
int     g_max_record_secs   = 10;
int16_t *record_buffer      = nullptr;
bool    g_has_recording     = false;

float   playback_gain = DEFAULT_PLAYBACK_GAIN;

int     currentMenu = 0;
int     lastMenu    = -1;

bool    g_waveform_visible = false;
int32_t g_wf_total         = 0;
int     g_wf_last_px       = -1;
bool    g_wf_use_peaks     = false;
int16_t g_wf_peaks[TFT_W]  = {};
uint8_t g_wf_freqt[TFT_W]  = {};
int     g_wf_peak           = 32768;

float   s_pitchLevel    = 0.318f;
float   s_echoLevel     = 0.5f;
float   s_ringmodLevel  = 0.5f;
float   s_stutterLevel  = 0.5f;
float   s_tremoloLevel  = 0.3f;
float   s_hauntedLevel  = 0.5f;
float   s_alienLevel    = 0.5f;
float   s_monsterLevel  = 0.5f;
float   s_chorusLevel   = 0.5f;
float   s_telephoneLevel = 0.3f;
float   s_wavefoldLevel = 0.3f;
float   g_gate_threshold = PASSTHROUGH_GATE_THRESHOLD;
float   s_gateLevel      = PASSTHROUGH_GATE_THRESHOLD / 5000.0f;
float   g_live_gain      = 2.5f;
float   s_liveGainLevel  = (2.5f - 0.5f) / 5.5f;
float   g_mic_gain       = 1.0f;
float   s_micGainLevel   = (1.0f - 0.1f) / 1.9f;
float   s_volumeLevel    = (DEFAULT_PLAYBACK_GAIN - 0.5f) / 9.5f;
float   g_wl_max_amp     = 28000.0f;
float   s_wavLabVolLevel = 1.0f;
int     g_long_rec_secs  = 60;

int     g_th_pitch_src   = 0;
int     g_th_sound       = 0;

int      g_mood           = 0;
uint32_t g_mood_data_start = 0;
uint32_t g_mood_data_size  = 0;
uint32_t g_mood_byte_pos   = 0;
File     g_mood_file;
float    g_mood_gain    = MOOD_MUSIC_GAIN;
float    s_moodVolLevel = MOOD_MUSIC_GAIN / 0.5f;

const char *menuLabels[] = {
  // Root menu items
  "Wave Lab",
  "Theremin",
  "Live FX",
  "Record",
  "Play",
  "Stored Rec",
  "Stored Play",
  "Mood Music",
  "Settings",
  // Settings sub-menu items
  "Volume",
  "Feedback",
  "Live Gain",
  "Mic Gain",
  "Joy Cal",
  "WaveLab Vol",
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
  "Telephone",
  "Wavefold",
};

// ── I2S initialisation ───────────────────────────────────────────────────────

void initI2S()
{
  i2s_config_t i2s_rx_config = {};
  i2s_rx_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2s_rx_config.sample_rate = SAMPLE_RATE;
  i2s_rx_config.bits_per_sample = (i2s_bits_per_sample_t)I2S_BITS_PER_SAMPLE_16BIT;
  i2s_rx_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2s_rx_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_rx_config.dma_buf_count = 8;
  i2s_rx_config.dma_buf_len = 1024;
  i2s_rx_config.use_apll = false;

  i2s_config_t i2s_tx_config = {};
  i2s_tx_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_tx_config.sample_rate = SAMPLE_RATE;
  i2s_tx_config.bits_per_sample = (i2s_bits_per_sample_t)I2S_BITS_PER_SAMPLE_16BIT;
  i2s_tx_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2s_tx_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2s_tx_config.dma_buf_count = 8;
  i2s_tx_config.dma_buf_len = 256;
  i2s_tx_config.use_apll = false;

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

// ── Startup display helpers ──────────────────────────────────────────────────

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

  uint8_t riff[12];
  if (f.read(riff, 12) != 12 || riff[0] != 'R' || riff[1] != 'I')
  {
    Serial.println("Startup WAV: invalid RIFF header");
    f.close(); return;
  }

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

  showSplash();
  Serial.println("Playing startup WAV...");

  float savedGain = playback_gain;
  if (playback_gain > STARTUP_WAV_MAX_GAIN) playback_gain = STARTUP_WAV_MAX_GAIN;

  int16_t buf[256];
  size_t bytesRead;
  uint32_t bytesRemaining = sz;
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

// ── Startup diagnostics ──────────────────────────────────────────────────────

static void runStartupDiagnostics()
{
  // Each item: state 0=fail(red) 1=warn(yellow) 2=ok(green)
  struct Diag { const char *label; uint8_t state; char detail[40]; };
  Diag items[6];
  int  n = 0;

  // 1. TFT — can't read back (MISO=-1); drawing this screen IS the proof
  { auto &d = items[n++]; d.label = "TFT Display";
    d.state = 2;
    snprintf(d.detail, sizeof(d.detail), "SPI init OK"); }

  // 2. LittleFS — check startup WAV is present
  { auto &d = items[n++]; d.label = "LittleFS / Flash";
    bool ok = LittleFS.exists("/voicemorpher.wav");
    d.state = ok ? 2 : 1;
    snprintf(d.detail, sizeof(d.detail), ok ? "startup WAV present" : "startup WAV missing"); }

  // 3. Joystick — both axes should be near centre (300–3800)
  { auto &d = items[n++]; d.label = "Joystick";
    int x = analogRead(JOY_X_PIN), y = analogRead(JOY_Y_PIN);
    bool ok = (x > 300 && x < 3800) && (y > 300 && y < 3800);
    d.state = ok ? 2 : 0;
    snprintf(d.detail, sizeof(d.detail), "X=%d  Y=%d", x, y); }

  // 4. Microphone — connected INMP441 always has a non-zero noise floor
  { auto &d = items[n++]; d.label = "Microphone (INMP441)";
    int16_t buf[256] = {};
    size_t  br = 0;
    i2s_read(I2S_RX_PORT, buf, sizeof(buf), &br, pdMS_TO_TICKS(200));
    int     ns  = (int)(br / 2);
    int64_t sum = 0;
    for (int i = 0; i < ns; i++) sum += abs(buf[i]);
    bool ok = (ns > 0 && sum > 0);
    d.state = ok ? 2 : 0;
    if (ok) snprintf(d.detail, sizeof(d.detail), "%d samples, avg amp %d", ns, (int)(sum / ns));
    else    snprintf(d.detail, sizeof(d.detail), "no signal — check SCK/WS/SD/L-R"); }

  // 5. HC-SR04 — optional, warn rather than fail if absent
  { auto &d = items[n++]; d.label = "HC-SR04 Sonar";
    float dist = readHCSR04cm();
    d.state = (dist > 0) ? 2 : 1;
    if (dist > 0) snprintf(d.detail, sizeof(d.detail), "%.1f cm detected", dist);
    else          snprintf(d.detail, sizeof(d.detail), "no echo (optional for theremin)"); }

  // 6. Record buffer — allocated in DRAM before we got here
  { auto &d = items[n++]; d.label = "Record Buffer";
    bool ok = (record_buffer != nullptr);
    d.state = ok ? 2 : 0;
    int kb = g_max_record_secs * SAMPLE_RATE * 2 / 1024;
    if (ok) snprintf(d.detail, sizeof(d.detail), "%ds = %d KB internal DRAM", g_max_record_secs, kb);
    else    snprintf(d.detail, sizeof(d.detail), "ALLOC FAILED"); }

  // ── Serial output ────────────────────────────────────────────────────────
  Serial.println("\n=== STARTUP DIAGNOSTICS ===");
  bool anyFail = false;
  for (int i = 0; i < n; i++) {
    const char *sym = (items[i].state == 2) ? "[ OK ]" :
                      (items[i].state == 1) ? "[WARN]" : "[FAIL]";
    Serial.printf("  %s  %-22s  %s\n", sym, items[i].label, items[i].detail);
    if (items[i].state == 0) anyFail = true;
  }
  Serial.println("===========================\n");

  // ── TFT output ──────────────────────────────────────────────────────────
  tft.fillScreen(C_BG);
  fillGradH(0, 0, TFT_W, 36, 0, 55, 140, 0, 15, 65);
  tft.drawFastHLine(0, 36, TFT_W, TFT_CYAN);
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2);
  tft.setCursor(8, 10); tft.print("Diagnostics");

  static const uint16_t STATE_COL[3] = { TFT_RED, TFT_YELLOW, 0x07E0 }; // red/yellow/green

  const int START_Y  = 42;
  const int ITEM_H_D = 28;
  for (int i = 0; i < n; i++) {
    int y = START_Y + i * ITEM_H_D;
    tft.fillCircle(12, y + 11, 7, STATE_COL[items[i].state]);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(26, y + 3);  tft.print(items[i].label);
    tft.setTextColor(tft.color565(150, 160, 180));
    tft.setCursor(26, y + 14); tft.print(items[i].detail);
  }

  tft.drawFastHLine(0, TFT_H - 22, TFT_W, tft.color565(25, 30, 65));
  tft.setTextSize(1);
  if (anyFail) {
    tft.setTextColor(TFT_RED);
    tft.setCursor(4, TFT_H - 14);
    tft.print("Check wiring!  Press button to continue");
    while (!isJoystickButtonPressed()) delay(50);
    while ( isJoystickButtonPressed()) delay(50);
  } else {
    tft.setTextColor(tft.color565(120, 200, 120));
    tft.setCursor(4, TFT_H - 14);
    tft.print("All OK — starting up...");
    delay(5000);
  }
}

// ── setup / loop ─────────────────────────────────────────────────────────────

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector
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
  tft.setRotation(3);
  tft.fillScreen(C_BG);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  Serial.println("✓ ST7789V TFT initialized (240x320)");

  pinMode(JOY_BTN_PIN, INPUT_PULLUP);
  pinMode(JOY_X_PIN, INPUT);
  pinMode(HC_SR04_TRIG_PIN, OUTPUT);
  digitalWrite(HC_SR04_TRIG_PIN, LOW);
  pinMode(HC_SR04_ECHO_PIN, INPUT);

  g_prefs.begin("voicemorph", false);
  float savedGain = g_prefs.getFloat("vol_gain", DEFAULT_PLAYBACK_GAIN);
  playback_gain  = savedGain;
  s_volumeLevel  = (savedGain - 0.5f) / 9.5f;
  if (s_volumeLevel < 0.0f) s_volumeLevel = 0.0f;
  if (s_volumeLevel > 1.0f) s_volumeLevel = 1.0f;
  Serial.printf("✓ Volume loaded: %.2fx\n", savedGain);
  g_gate_threshold = g_prefs.getFloat("gate_thresh", PASSTHROUGH_GATE_THRESHOLD);
  s_gateLevel      = g_gate_threshold / 3000.0f;
  Serial.printf("✓ Gate threshold loaded: %.0f\n", g_gate_threshold);
  g_live_gain     = g_prefs.getFloat("live_gain", 2.5f);
  s_liveGainLevel = (g_live_gain - 0.5f) / 5.5f;
  Serial.printf("✓ Live gain loaded: %.2fx\n", g_live_gain);
  g_mic_gain      = g_prefs.getFloat("mic_gain", 1.0f);
  s_micGainLevel  = (g_mic_gain - 0.1f) / 1.9f;
  if (s_micGainLevel < 0.0f) s_micGainLevel = 0.0f;
  if (s_micGainLevel > 1.0f) s_micGainLevel = 1.0f;
  Serial.printf("✓ Mic gain loaded: %.2fx\n", g_mic_gain);
  g_th_sound     = g_prefs.getInt("th_sound", 0);
  if (g_th_sound < 0 || g_th_sound >= TH_SOUND_COUNT) g_th_sound = 0;
  g_th_pitch_src = g_prefs.getInt("th_pitch_src", 0);
  if (g_th_pitch_src < 0 || g_th_pitch_src >= TH_PITCH_SRC_COUNT) g_th_pitch_src = 0;
  g_wl_max_amp = g_prefs.getFloat("wl_vol", 28000.0f);
  if (g_wl_max_amp < 2800.0f)  g_wl_max_amp = 2800.0f;
  if (g_wl_max_amp > 28000.0f) g_wl_max_amp = 28000.0f;
  s_wavLabVolLevel = (g_wl_max_amp / 28000.0f - 0.1f) / 0.9f;
  Serial.printf("✓ WaveLab max amp loaded: %.0f (%.0f%%)\n", g_wl_max_amp, g_wl_max_amp / 280.0f);
  g_mood      = g_prefs.getInt("mood", 0);
  g_mood_gain = g_prefs.getFloat("mood_vol", MOOD_MUSIC_GAIN);
  s_moodVolLevel = g_mood_gain / 0.5f;
  if (s_moodVolLevel > 1.0f) s_moodVolLevel = 1.0f;
  Serial.printf("✓ Mood loaded: %d (%s), gain=%.2f\n", g_mood, MOOD_NAMES[g_mood], g_mood_gain);

  LittleFS.begin(true);
  if (loadRecordingAuto())
    Serial.printf("✓ Auto-loaded last recording: %ds\n", active_sample_count / SAMPLE_RATE);
  loadMoodTrack(g_mood);

  initI2S();
  runStartupDiagnostics();
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
