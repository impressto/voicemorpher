#include "globals.h"
#include "stations.h"
#include <Audio.h>

// ===== ESP32-audioI2S callbacks (weak symbols overridden here) =====
//
// IMPORTANT: never call Serial.print() from these. They can be invoked
// from deep inside audio->loop() (e.g. repeated MP3 decode-error reporting
// during a stream hiccup), and Serial's UART driver takes a FreeRTOS
// queue spinlock that can collide with WiFi/lwIP activity on the other
// core and trip a "spinlock_acquire" assert/crash. Buffer messages here
// and print them from runRadioMenu()'s loop via printPendingAudioInfo().

char g_stationName[64]  = "";
char g_streamTitle[128] = "";

static char g_lastAudioInfo[160] = "";
static volatile bool g_audioInfoDirty = false;

static void setAudioInfo(const char *prefix, const char *info)
{
  snprintf(g_lastAudioInfo, sizeof(g_lastAudioInfo), "%s%s", prefix, info);
  g_audioInfoDirty = true;
}

void printPendingAudioInfo()
{
  if (g_audioInfoDirty) {
    g_audioInfoDirty = false;
    Serial.println(g_lastAudioInfo);
  }
}

void audio_info(const char *info)        { setAudioInfo("audio_info: ", info); }
void audio_showstation(const char *info) { setAudioInfo("station: ", info); }
void audio_bitrate(const char *info)     { setAudioInfo("bitrate: ", info); }
void audio_eof_stream(const char *info)  { setAudioInfo("eof_stream: ", info); }

void audio_showstreamtitle(const char *info)
{
  strncpy(g_streamTitle, info, sizeof(g_streamTitle) - 1);
  g_streamTitle[sizeof(g_streamTitle) - 1] = '\0';
  setAudioInfo("streamtitle: ", info);
}

// ===== Web Radio menu =====

// Station list plus a trailing synthetic "Alarm Clock" entry (index
// STATION_COUNT) that opens runAlarmClockMenu() instead of playing a stream.
static int showStationList(int &sel)
{
  static const char    *labels[STATION_COUNT + 1];
  static const uint8_t *icons[STATION_COUNT + 1];
  static bool initDone = false;
  if (!initDone)
  {
    for (size_t i = 0; i < STATION_COUNT; ++i)
    {
      labels[i] = STATION_NAMES[i];
      icons[i]  = ICON_RADIO;
    }
    labels[STATION_COUNT] = "Alarm Clock";
    icons[STATION_COUNT]  = ICON_ALARM;
    initDone = true;
  }
  return showIconList(labels, icons, (int)STATION_COUNT + 1, sel);
}

static void drawVolumeBar(int volume)
{
  tft.fillRect(0, TFT_H - 78, TFT_W, 40, tft.color565(0, 5, 20));
  tft.setTextSize(1);
  tft.setTextColor(COL_GRAY);
  tft.setCursor(10, TFT_H - 70);
  tft.print("Volume");
  float level = (float)(volume - RADIO_VOLUME_MIN) / (float)(RADIO_VOLUME_MAX - RADIO_VOLUME_MIN);
  drawProgressBar(10, TFT_H - 56, TFT_W - 20, 16, level);
}

static void drawNowPlayingScreen(int volume, bool alarmMode = false)
{
  // Try station-specific art, then the generic fallback, then plain background
  const char *art = (g_radio_station >= 0 && g_radio_station < (int)STATION_COUNT)
                    ? STATIONS[g_radio_station].art
                    : nullptr;
  if (!art) art = "/jazz-small.jpg";

  tft.fillScreen(C_BG);
  if (LittleFS.exists(art))
    drawJpegFromFS(art, 0, 38);  // 320x160 image sits below the header

  drawHeader(g_stationName);

  // Dark strip behind stream title for readability over the image
  tft.fillRect(0, 38, TFT_W, 22, tft.color565(0, 5, 20));
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 44);
  tft.print(g_streamTitle);

  drawVolumeBar(volume);
  if (alarmMode)
    drawHints("X: vol  Y: snooze 5min", "Btn: dismiss alarm");
  else
    drawHints("X: volume", "Hold button: station list");
}

bool runRadioMenu(bool autoStart)
{
  while (isJoystickButtonPressed()) delay(10);

  if (!isWiFiConnected()) {
    drawStatus("Web Radio", "WiFi unavailable");
    delay(300);
    while (!isJoystickButtonPressed()) delay(50);
    while ( isJoystickButtonPressed()) delay(50);
    return false;
  }

  // record_buffer (215KB, permanently held in internal DRAM) plus WiFi's
  // own internal-DRAM footprint leaves too little headroom for
  // ESP32-audioI2S's I2S DMA buffers (~32KB). Free it for the duration of
  // Web Radio - any unsaved recording is lost (use "Stored Rec" to keep it).
  g_has_recording = false;
  free(record_buffer);
  record_buffer = nullptr;

  // Free I2S_NUM_1 so ESP32-audioI2S can claim it for the stream decoder.
  i2s_driver_uninstall(I2S_TX_PORT);

  bool snooze = false;

  {
    // Heap-allocated: Audio's internal decode buffer (~8KB) is too large
    // for a stack object in this call chain.
    Audio *audio = new Audio(false, 3, I2S_TX_PORT);
    audio->setPinout(I2S_TX_BCK, I2S_TX_WS, I2S_TX_SD);
    audio->setVolume((uint8_t)g_radio_volume);

    int sel = g_radio_station;
    bool first = true;
    while (true)
    {
      int chosen = (first && autoStart) ? sel : showStationList(sel);
      first = false;

      if (chosen < 0) {
        audio->stopSong();
        break;
      }

      if (chosen == (int)STATION_COUNT) {  // "Alarm Clock" entry
        runAlarmClockMenu();
        continue;
      }

      sel = chosen;
      g_radio_station = sel;
      g_prefs.putInt("rd_station", g_radio_station);

      strncpy(g_stationName, STATIONS[sel].name, sizeof(g_stationName) - 1);
      g_stationName[sizeof(g_stationName) - 1] = '\0';
      g_streamTitle[0] = '\0';
      audio->connecttohost(STATIONS[sel].url);

      drawNowPlayingScreen(g_radio_volume, autoStart);
      char lastTitle[sizeof(g_streamTitle)] = "";
      int  lastVol = g_radio_volume;
      unsigned long lastMoveMs = 0;
      while (isJoystickButtonPressed()) delay(10);

      while (true)
      {
        audio->loop();
        printPendingAudioInfo();

        unsigned long now = millis();
        int x = readJoystickAxis(JOY_X_PIN);
        if (x != 0 && now - lastMoveMs > 200) {
          g_radio_volume += x;
          if (g_radio_volume < RADIO_VOLUME_MIN) g_radio_volume = RADIO_VOLUME_MIN;
          if (g_radio_volume > RADIO_VOLUME_MAX) g_radio_volume = RADIO_VOLUME_MAX;
          audio->setVolume((uint8_t)g_radio_volume);
          g_prefs.putInt("rd_vol", g_radio_volume);
          lastMoveMs = now;
        }

        // Alarm mode: joystick Y = snooze
        if (autoStart && readJoystickAxis(JOY_Y_PIN) != 0) {
          snooze = true;
          break;
        }

        if (isJoystickButtonPressed()) {
          unsigned long pressStart = millis();
          while (isJoystickButtonPressed()) delay(10);
          if (autoStart) break;  // any click = dismiss alarm
          if (millis() - pressStart >= 500) break;  // long press = station list
        }

        if (strcmp(g_streamTitle, lastTitle) != 0) {
          strncpy(lastTitle, g_streamTitle, sizeof(lastTitle) - 1);
          lastTitle[sizeof(lastTitle) - 1] = '\0';
          lastVol = g_radio_volume;
          drawNowPlayingScreen(g_radio_volume, autoStart);
        } else if (g_radio_volume != lastVol) {
          lastVol = g_radio_volume;
          drawVolumeBar(g_radio_volume);
        }
      }

      if (autoStart) break;  // alarm mode: exit entirely, no station list
    }

    delete audio; // destructor calls i2s_driver_uninstall(I2S_TX_PORT)
  }

  // Restore TX to normal DMA config (8x256) for voice playback
  {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = SAMPLE_RATE;
    cfg.bits_per_sample      = (i2s_bits_per_sample_t)I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 256;
    cfg.use_apll             = false;
    i2s_driver_install(I2S_TX_PORT, &cfg, 0, NULL);
    i2s_pin_config_t pins = {};
    pins.bck_io_num   = I2S_TX_BCK;
    pins.ws_io_num    = I2S_TX_WS;
    pins.data_out_num = I2S_TX_SD;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin(I2S_TX_PORT, &pins);
  }
  i2s_zero_dma_buffer(I2S_TX_PORT);

  record_buffer = (int16_t *)heap_caps_malloc(
      SAMPLE_RATE * g_max_record_secs * sizeof(int16_t),
      MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  if (!record_buffer)
    Serial.println("ERROR: Failed to re-allocate record buffer after Web Radio");

  return snooze;
}
