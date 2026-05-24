# VoiceMorpher — ESP32-S3 I2S Recording + Effects (PlatformIO)
```````````````````````````````````````````````00`0000``0`000`0``000000000000000000```````22```````````````````0`00`````````````````````````````````````0``````````````````````````````````````````````````````0```````````````````````````````````````````````````````````````````````````````````````````54+86666666666666666666999999999999999999999``````````````````````````````````````````````````````````````````````````````````21
This small PlatformIO project demonstrates recording from an INMP441 I2S microphone and playback to a MAX98357A I2S amplifier on an ESP32-S3. It implements simple effects: pitch via resampling, reverse, echo, and ring modulation.

Wiring (pins shown in current code)

INMP441 microphone (I2S RX):
- SCK  -> ESP32 GPIO4
- WS   -> ESP32 GPIO5
- SD   -> ESP32 GPIO6
- L/R  -> GND (left channel)
- VDD  -> 3.3V
- GND  -> GND

MAX98357A amplifier (I2S TX):
- BCLK -> ESP32 GPIO47
- LRCLK/LRC -> ESP32 GPIO45
- DIN/SD -> ESP32 GPIO38
- AMP_SD -> ESP32 GPIO21
- VIN -> 5V
- GND -> GND
- GAIN -> leave per module default or wire according to your board documentation

Optional display / joystick wiring (if using UI code):
- OLED SDA -> ESP32 GPIO18
- OLED SCL -> ESP32 GPIO16
- Joystick X -> ESP32 GPIO17 (ADC)
- Joystick Y -> ESP32 GPIO20 (ADC)
- Joystick button -> ESP32 GPIO35 (digital input)

Joystick X can be used to control effect intensity for echo and ring modulation.

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
