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

static int showStationList(int &sel)
{
  static const uint8_t *icons[] = {
    ICON_RADIO, ICON_RADIO, ICON_RADIO, ICON_RADIO,
    ICON_RADIO, ICON_RADIO, ICON_RADIO, ICON_RADIO,
  };
  return showIconList(STATION_NAMES, icons, (int)STATION_COUNT, sel);
}

static void drawNowPlayingScreen(int volume)
{
  tft.fillScreen(C_BG);
  drawHeader(g_stationName);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 44);
  tft.print(g_streamTitle);

  tft.setTextColor(COL_GRAY);
  tft.setCursor(10, TFT_H - 70);
  tft.print("Volume");

  float level = (float)(volume - RADIO_VOLUME_MIN) / (float)(RADIO_VOLUME_MAX - RADIO_VOLUME_MIN);
  drawProgressBar(10, TFT_H - 56, TFT_W - 20, 16, level);

  drawHints("X: volume", "Hold button: station list");
}

void runRadioMenu()
{
  while (isJoystickButtonPressed()) delay(10);

  if (!isWiFiConnected()) {
    drawStatus("Web Radio", "WiFi unavailable");
    delay(300);
    while (!isJoystickButtonPressed()) delay(50);
    while ( isJoystickButtonPressed()) delay(50);
    return;
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

  {
    // Heap-allocated: Audio's internal decode buffer (~8KB) is too large
    // for a stack object in this call chain.
    Audio *audio = new Audio(false, 3, I2S_TX_PORT);
    audio->setPinout(I2S_TX_BCK, I2S_TX_WS, I2S_TX_SD);
    audio->setVolume((uint8_t)g_radio_volume);

    int sel = g_radio_station;
    while (true)
    {
      int chosen = showStationList(sel);
      if (chosen < 0) {
        audio->stopSong();
        break;
      }

      sel = chosen;
      g_radio_station = sel;
      g_prefs.putInt("rd_station", g_radio_station);

      strncpy(g_stationName, STATIONS[sel].name, sizeof(g_stationName) - 1);
      g_stationName[sizeof(g_stationName) - 1] = '\0';
      g_streamTitle[0] = '\0';
      audio->connecttohost(STATIONS[sel].url);

      drawNowPlayingScreen(g_radio_volume);
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

        if (isJoystickButtonPressed()) {
          unsigned long pressStart = millis();
          while (isJoystickButtonPressed()) delay(10);
          if (millis() - pressStart >= 500) break;
        }

        if (strcmp(g_streamTitle, lastTitle) != 0 || g_radio_volume != lastVol) {
          strncpy(lastTitle, g_streamTitle, sizeof(lastTitle) - 1);
          lastTitle[sizeof(lastTitle) - 1] = '\0';
          lastVol = g_radio_volume;
          drawNowPlayingScreen(g_radio_volume);
        }
      }
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
}
