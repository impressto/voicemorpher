#pragma once

// ── Common includes ──────────────────────────────────────────────────────────
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "menu_icons.h"
#include <LittleFS.h>
using namespace fs;
#include <Preferences.h>
#include "driver/i2s.h"
#include "config.h"

// ── Pin definitions ──────────────────────────────────────────────────────────
// INMP441 (microphone - RX)
#define I2S_RX_BCK 4
#define I2S_RX_WS  5
#define I2S_RX_SD  6

// MAX98357A (amplifier - TX)
#define I2S_TX_BCK 47
#define I2S_TX_WS  45
#define I2S_TX_SD  38
#define AMP_SD     21

// ST7789V TFT display
#define TFT_W    320
#define TFT_H    240
#define ITEM_H    26
#define COL_GRAY 0x7BEF

// Waveform display geometry
#define WF_X   8
#define WF_Y   38
#define WF_W   (TFT_W - 16)
#define WF_H   WAVEFORM_HEIGHT
#define WF_CY  (WF_Y + WF_H / 2)

// Joystick pins
#define JOY_X_PIN           17
#define JOY_Y_PIN           20
#define JOY_BTN_PIN         35
#define JOY_LOW_THRESHOLD  1200
#define JOY_HIGH_THRESHOLD 2800

#define HC_SR04_TRIG_PIN 15
#define HC_SR04_ECHO_PIN 16

// ── FA icon defines ───────────────────────────────────────────────────────────
#define FA_BOLT      "\xEF\x83\xA7"
#define FA_MIC       "\xEF\x84\xB0"
#define FA_PLAY      "\xEF\x81\x8B"
#define FA_MAGIC     "\xEF\x83\x90"
#define FA_PHONES    "\xEF\x80\xA5"
#define FA_SAVE      "\xEF\x83\x87"
#define FA_FOLDER    "\xEF\x81\xBC"
#define FA_VOLUME    "\xEF\x80\xA8"
#define FA_BACK      "\xEF\x81\x8A"
#define FA_MUSIC     "\xEF\x80\x81"
#define FA_ARROWS    "\xEF\x81\x87"
#define FA_CIRCLE    "\xEF\x84\x8C"
#define FA_FFWD      "\xEF\x81\x90"
#define FA_BARCHART  "\xEF\x82\x80"
#define FA_MOON      "\xEF\x86\x86"
#define FA_ROCKET    "\xEF\x84\xB5"
#define FA_BUG       "\xEF\x86\x88"
#define FA_GROUP     "\xEF\x83\x80"
#define FA_UP        "\xEF\x81\xB7"
#define FA_DOWN      "\xEF\x81\xB8"

// ── Inline constants ─────────────────────────────────────────────────────────
#define C_BG ((uint16_t)0x0821)  // dark navy background

constexpr int TH_SOUND_COUNT    = 4;
constexpr int TH_PITCH_SRC_COUNT = 2;
constexpr int MOOD_COUNT         = 7;

// ── MenuItem enum ─────────────────────────────────────────────────────────────
enum MenuItem
{
  MENU_RECORD,
  MENU_PLAY,
  MENU_PASSTHROUGH,
  MENU_LONG_REC,
  MENU_LONG_PLAY,
  MENU_MOOD,
  MENU_THEREMIN,
  MENU_MATHSYNTH,
  MENU_SETTINGS,
  MENU_ROOT_COUNT,

  MENU_STORAGE_COUNT = MENU_ROOT_COUNT,

  MENU_VOLUME = MENU_STORAGE_COUNT,
  MENU_FEEDBACK,
  MENU_LIVE_GAIN,
  MENU_MIC_GAIN,
  MENU_CALIBRATE_JOY,
  MENU_WAVELAB_VOL,
  MENU_SETTINGS_COUNT,

  MENU_REVERSE = MENU_SETTINGS_COUNT,
  MENU_PITCH,
  MENU_ECHO,
  MENU_RINGMOD,
  MENU_STUTTER,
  MENU_TREMOLO,
  MENU_HAUNTED,
  MENU_ALIEN,
  MENU_MONSTER,
  MENU_CHORUS,
  MENU_TELEPHONE,
  MENU_WAVEFOLD,
  MENU_COUNT
};

// ── Global variable externs ───────────────────────────────────────────────────
extern Preferences        g_prefs;
extern TFT_eSPI           tft;
extern const i2s_port_t   I2S_RX_PORT;
extern const i2s_port_t   I2S_TX_PORT;

extern int     active_sample_count;
extern int     g_max_record_secs;
extern int16_t *record_buffer;
extern bool    g_has_recording;

extern float   playback_gain;

extern int     currentMenu;
extern int     lastMenu;

extern bool    g_waveform_visible;
extern int32_t g_wf_total;
extern int     g_wf_last_px;
extern bool    g_wf_use_peaks;
extern int16_t g_wf_peaks[TFT_W];
extern uint8_t g_wf_freqt[TFT_W];
extern int     g_wf_peak;

extern float   s_pitchLevel;
extern float   s_echoLevel;
extern float   s_ringmodLevel;
extern float   s_stutterLevel;
extern float   s_tremoloLevel;
extern float   s_hauntedLevel;
extern float   s_alienLevel;
extern float   s_monsterLevel;
extern float   s_chorusLevel;
extern float   s_telephoneLevel;
extern float   s_wavefoldLevel;
extern float   g_gate_threshold;
extern float   s_gateLevel;
extern float   g_live_gain;
extern float   s_liveGainLevel;
extern float   g_mic_gain;
extern float   s_micGainLevel;
extern float   s_volumeLevel;
extern float   g_wl_max_amp;
extern float   s_wavLabVolLevel;
extern int     g_long_rec_secs;

extern int      g_th_pitch_src;
extern int      g_th_sound;

extern int      g_mood;
extern uint32_t g_mood_data_start;
extern uint32_t g_mood_data_size;
extern uint32_t g_mood_byte_pos;
extern File     g_mood_file;
extern float    g_mood_gain;
extern float    s_moodVolLevel;

// ── Const array externs ───────────────────────────────────────────────────────
extern const char *menuLabels[];
// Per-item accent RGB — indexed by MenuItem enum (covers root + settings range)
extern const uint8_t ITEM_RGB[][3];
extern const char *TH_SOUND_NAMES[];
extern const char *TH_PITCH_SRC_NAMES[];
extern const char *TH_SOUND_PATHS[];
extern const char *MOOD_NAMES[];
extern const char *MOOD_PATHS[];

// ── Inline function: applyPlaybackGain ───────────────────────────────────────
inline int16_t applyPlaybackGain(int16_t sample)
{
  int32_t scaled = (int32_t)(sample * playback_gain);
  if (scaled > INT16_MAX) scaled = INT16_MAX;
  else if (scaled < INT16_MIN) scaled = INT16_MIN;
  return (int16_t)scaled;
}

// ── Forward declarations ──────────────────────────────────────────────────────

// display.cpp
void fillGradH(int16_t x, int16_t y, int16_t w, int16_t h,
               uint8_t r1, uint8_t g1, uint8_t b1,
               uint8_t r2, uint8_t g2, uint8_t b2);
void fillGradV(int16_t x, int16_t y, int16_t w, int16_t h,
               uint8_t r1, uint8_t g1, uint8_t b1,
               uint8_t r2, uint8_t g2, uint8_t b2);
void drawHeader(const char *title);
void drawHints(const char *line1, const char *line2 = nullptr);
void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, float level);
void drawMenu();
void drawStatus(const char *line1, const char *line2 = nullptr);
void drawWfColumn(int px);
void fillStoredWaveformBuf(File &f, int32_t totalSamples);
void drawWaveformPlayhead(int samplePos);
int  showDurationSubMenu(int currentSecs, int maxSecs = 10);
float showLevelSubMenu(const char *title, const char *paramLabel,
                       float current, float minVal, float maxVal, const char *unit);
int  showIconList(const char *const *labels, const uint8_t *const *icons,
                  int count, int &sel);

// joystick.cpp
int   readJoystickAxis(int pin);
float readJoystickXIntensity();
bool  isJoystickButtonPressed();
float readHCSR04cm();
void  handleJoystickMenu();

// recording.cpp
void saveRecordingAuto();
bool loadRecordingAuto();
void recordToBuffer();
void playBufferSimple();
void cleanRecording();
void normalizeRecording(float targetPeak = 0.95f);
void playRecordingDoneBlips();

// audio_effects.cpp
void writeSamplesWithGain(const int16_t *src, size_t sampleCount);
void stopTxAndFlush();
void playReverse();
void playResample(float speed);
void playEcho(float delaySec, float decay);
void playRingMod(float freq);
void playStutter(float chunkSec, int repeats);
void playTremolo(float rate);
void playHaunted(float delaySec, float decay);
void playAlien(float speed, float ringFreq);
void playMonster(float speed, float delaySec, float decay);
void playChorus(float rate, float depth);
void playTelephone(float hpHz);
void playWavefold(float threshold);
void passthrough();
void passthroughWithEffect(int fx);

// mood.cpp
bool loadMoodTrack(int mood);
void openMoodPlayback();
void closeMoodPlayback();
void mixMoodInto(int16_t *buf, int n);

// math_synth.cpp
void runMathSynthMenu();

// theremin.cpp
void calibrateThereminJoy();
void thereminMode();
void runThereminMenu();

// menu.cpp
int  showPassthroughFxSubMenu();
int  showLongPlayFxSubMenu();
int  showSettingsSubMenu();
int  showPlaySubMenu();
int  showMoodPickerScreen();
void runMoodMenu();
void runMenuAction(int item);
