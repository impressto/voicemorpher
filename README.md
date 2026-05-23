# VoiceMorpher — ESP32-S3 I2S Recording + Effects (PlatformIO)

This small PlatformIO project demonstrates recording from an INMP441 I2S microphone and playback to a MAX98357A I2S amplifier on an ESP32-S3. It implements simple effects: pitch via resampling, reverse, echo, and ring modulation.

Wiring (example pins shown in code)
- INMP441 SCK -> ESP32 GPIO4 (shared BCLK)
- INMP441 WS  -> ESP32 GPIO5 (shared LRCLK/WS)
- INMP441 SD  -> ESP32 GPIO6 (data out -> input)
- INMP441 L/R -> GND (left channel)
- VDD / GND   -> 3.3V / GND

- MAX98357A BCLK -> GPIO4 (shared)
- MAX98357A LRC  -> GPIO5 (shared)
- MAX98357A DIN  -> GPIO7 (data in from ESP32)
- VIN / GND      -> 5V / GND (speaker power)

Build & upload

1. Install PlatformIO in VS Code.
2. Open this folder as a PlatformIO project.
3. Connect your ESP32-S3 board and run the usual build/upload.

Serial UI
- Open serial monitor at 115200.
- Commands:
  - `r` record (RECORD_SECONDS defined in code)
  - `p` play raw recording
  - `v` passthrough (live input -> output)
  - `1` play reverse
  - `2` pitch up (faster)
  - `3` pitch down (slower)
  - `4` echo (200 ms)
  - `5` ring modulation (robot)

Notes & Next Steps
- The code uses a simple resampling approach for pitch (changes speed). For time-preserving pitch-shift, integrate a proper algorithm (e.g., phase vocoder or libraries).
- For longer recordings, enable PSRAM or reduce `RECORD_SECONDS`.
- Consider using `arduino-audio-tools` or ESP-ADF for more advanced effects and pipelines.
