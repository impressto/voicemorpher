#pragma once

// Set to 0 to silence the startup WAV on boot.
#ifndef PLAY_STARTUP_WAV
#define PLAY_STARTUP_WAV 1
#endif

// Maximum gain applied to the startup WAV, regardless of the stored volume level.
#ifndef STARTUP_WAV_MAX_GAIN
#define STARTUP_WAV_MAX_GAIN 3.0f
#endif

// Noise gate threshold for Live FX passthrough (0–32767 scale).
// Output is silenced when the smoothed input envelope drops below this level,
// breaking the acoustic feedback loop. Raise if gate triggers on real speech.
#ifndef PASSTHROUGH_GATE_THRESHOLD
#define PASSTHROUGH_GATE_THRESHOLD 1500.0f
#endif

// Default playback volume multiplier for MAX98357A output.
// Increase this value to make playback louder, but avoid values that clip badly.
#ifndef DEFAULT_PLAYBACK_GAIN
#define DEFAULT_PLAYBACK_GAIN 6.0f
#endif

// Audio sample rate in Hz. Higher = better frequency response, shorter max RAM buffer.
// 11025 Hz → Nyquist 5.5 kHz, 10 s max RAM buffer.
// 22050 Hz → Nyquist 11 kHz,   5 s max RAM buffer (same bytes, better quality).
#ifndef SAMPLE_RATE
#define SAMPLE_RATE 11025
#endif

// Waveform display height in pixels.
// The waveform box sits just below the title bar (y=38) and fills downward.
// Max usable value is roughly TFT_H - 42 = 198. Decrease for a smaller box.
#ifndef WAVEFORM_HEIGHT
#define WAVEFORM_HEIGHT 196
#endif

// Background mood music mix level (0.0 = silent, 1.0 = full scale).
// Lower values keep music clearly in the background behind the voice.
#ifndef MOOD_MUSIC_GAIN
#define MOOD_MUSIC_GAIN 0.15f
#endif

// Enable a light post-recording smoothing filter to reduce microphone crackle.
#ifndef ENABLE_AUDIO_CLEANING
#define ENABLE_AUDIO_CLEANING 1
#endif

// Filter strength: 1 = light smoothing, 2 = stronger smoothing.
#ifndef AUDIO_CLEANING_STRENGTH
#define AUDIO_CLEANING_STRENGTH 1
#endif

// HC-SR04 sonar pitch source range (cm). Hand closest = highest pitch.
// Clamp to [TH_SONAR_MIN_CM, TH_SONAR_MAX_CM] before mapping to pitchT.
#ifndef TH_SONAR_MIN_CM
#define TH_SONAR_MIN_CM 5.0f
#endif
#ifndef TH_SONAR_MAX_CM
#define TH_SONAR_MAX_CM 30.0f
#endif

// Web Radio: ESP32-audioI2S volume range (0-21) and default level.
#ifndef RADIO_VOLUME_MIN
#define RADIO_VOLUME_MIN 0
#endif
#ifndef RADIO_VOLUME_MAX
#define RADIO_VOLUME_MAX 21
#endif
#ifndef RADIO_DEFAULT_VOLUME
#define RADIO_DEFAULT_VOLUME 8
#endif

// WiFi connect timeout during boot (ms). If this elapses without connecting,
// the radio turns off and Web Radio mode reports "WiFi unavailable".
#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 10000
#endif

// Alarm Clock: local timezone offset for NTP sync (no automatic DST -
// update this twice a year if your area observes DST).
#ifndef TZ_OFFSET_SEC
#define TZ_OFFSET_SEC (-5 * 3600) // UTC-5 (EST)
#endif
#ifndef NTP_SERVER1
#define NTP_SERVER1 "pool.ntp.org"
#endif
#ifndef NTP_SERVER2
#define NTP_SERVER2 "time.nist.gov"
#endif
