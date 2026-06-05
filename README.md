# VoiceMorpher — ESP32-S3 Audio Effects Project

Turn your voice into something completely different! This project lets you record your voice and apply real-time sound effects like pitch shifting, reverse, echo, stutter, and ring modulation — all controlled with a joystick and displayed on a small color screen. It also includes a **Theremin mode** where you can play musical notes by moving your hand over a distance sensor.

<img width="1186" height="577" alt="voicemorpher_diagramming" src="https://github.com/user-attachments/assets/8e7c6fd9-c5b0-4fcc-9130-51b6990499ea" />


---

## How It Works

Think of the ESP32-S3 microcontroller like a brain:

- 🎙️ **The microphone** is the ear — it captures sound and converts it to numbers
- 🧠 **The ESP32-S3** processes those numbers and applies effects
- 🔊 **The amplifier and speaker** are the mouth — they convert the numbers back into sound
- 🖥️ **The color display** shows menus, waveforms, and live feedback in full color

Sound is just numbers. Once your voice is stored as a list of numbers, you can reverse the list (reverse effect), multiply the numbers by a wobbling wave (ring mod), or play it back at a different speed (pitch shift). That's the magic of digital audio!

---

## What You Need

| Part | Purpose |
|------|---------|
| ESP32-S3 DevKitC-1 (N16R8) | The brain — runs all the code |
| INMP441 microphone module | Captures your voice as a digital signal |
| MAX98357A amplifier module | Drives the speaker from a digital signal |
| Small speaker (4–8 Ω) | Makes the sound you can hear |
| ST7789V TFT display (240×320) | Full-color screen showing menus and waveforms |
| Analog joystick module | Navigates the menu and adjusts effects |
| HC-SR04 ultrasonic sensor *(optional)* | Controls theremin pitch with hand distance |
| Breadboard + jumper wires | Connects everything together |
| USB cable | Powers the board and uploads code |

---

## Wiring Guide

### 🎙️ INMP441 Microphone → ESP32-S3

| Microphone Pin | ESP32-S3 Pin | What it does |
|----------------|--------------|--------------|
| SCK | GPIO 4 | Clock — sets the timing for data |
| WS | GPIO 5 | Word Select — says which channel (left/right) |
| SD | GPIO 6 | Serial Data — the actual audio numbers |
| L/R | GND | Sets the mic to use the left audio channel |
| VDD | 3.3V | Power |
| GND | GND | Ground |

### 🔊 MAX98357A Amplifier → ESP32-S3

| Amplifier Pin | ESP32-S3 Pin | What it does |
|---------------|--------------|--------------|
| BCLK |   GPIO 47 | Bit Clock — timing for audio data |
| LRCLK | GPIO 45 | Left/Right Clock — channel selector |
| DIN | GPIO 38 | Data In — audio numbers going to the amp |
| AMP_SD | GPIO 21 | Enable pin — must be HIGH to turn the amp on |
| VIN | 5V | Power (use 5V for best volume) |
| GND | GND | Ground |

### 🖥️ ST7789V TFT Display → ESP32-S3 (SPI)

| Display Pin | ESP32-S3 Pin | What it does |
|-------------|--------------|--------------|
| VCC | 3.3V | Power |
| GND | GND | Ground |
| SCL / SCK / CLK | GPIO 12 | SPI clock |
| SDA / MOSI / DIN | GPIO 11 | SPI data |
| CS | GPIO 10 | Chip select |
| DC / A0 | GPIO 9 | Data / command select |
| RST / RES | GPIO 13 | Reset |
| BL / LED | GPIO 8 | Backlight enable |

### 🕹️ Analog Joystick → ESP32-S3

| Joystick Pin | ESP32-S3 Pin | Purpose |
|--------------|--------------|---------|
| VRx (X axis) | GPIO 17 | Adjusts effect levels / theremin volume |
| VRy (Y axis) | GPIO 20 | Scrolls up and down the main menu / theremin pitch |
| SW (Button) | GPIO 35 | Selects the highlighted menu item |
| VCC | 3.3V | Power |
| GND | GND | Ground |

### 📡 HC-SR04 Ultrasonic Sensor → ESP32-S3 *(Theremin only)*

| Sensor Pin | ESP32-S3 Pin | What it does |
|------------|--------------|--------------|
| VCC | 5V | Power |
| GND | GND | Ground |
| TRIG | GPIO 15 | Trigger — sends the ultrasonic pulse |
| ECHO | GPIO 16 | Echo — measures how long the pulse takes to return |

---

<img width="1600" height="1422" alt="mm_1780697089292_en" src="https://github.com/user-attachments/assets/7bf60f29-db03-4ee3-98d1-1078c20d0090" />

## Setting Up

### 1. Install PlatformIO

PlatformIO is a tool inside VS Code that manages all the libraries and compiling for you.

1. Install [VS Code](https://code.visualstudio.com/)
2. Open VS Code → go to the Extensions panel (the blocks icon on the left)
3. Search for **PlatformIO IDE** and install it
4. Restart VS Code

### 2. Open the Project

1. Open VS Code
2. Click **File → Open Folder**
3. Select the `voicemorpher` folder
4. PlatformIO will automatically detect the project

### 3. Upload to the Board

1. Plug in your ESP32-S3 via USB
2. Click the **→ Upload** arrow at the bottom of VS Code (or press the PlatformIO upload button)
3. Wait for it to compile and flash — this takes about 30–60 seconds the first time
4. The color display should light up with the main menu

> **Tip:** If the upload fails, hold the **BOOT** button on your ESP32 while clicking upload, then release it once uploading starts.

---

## The Color Display

The VoiceMorpher uses a **240×320 ST7789V IPS TFT display** — the same kind of screen used in many game consoles and smartwatches. It shows:

- **Gradient menu bars** — the selected item is highlighted with a blue-to-teal gradient
- **Live waveforms** — when recording or playing back, a real-time oscilloscope trace is drawn in the color of the current effect
- **Progress bars** — for recording time, effect level, and theremin pitch/volume
- **Note name and frequency** — in theremin mode, the current musical note (e.g. `A4 — 440.0 Hz`) is shown in large text

The display uses the SPI bus (a 4-wire high-speed data connection). The ESP32-S3 sends new pixels to the screen using DMA (Direct Memory Access), so drawing doesn't slow down the audio processing.

> **Why color?** Color makes it much easier to read at a glance which mode you're in. The menus use a dark navy background with cyan accents — easy to see in a classroom or stage environment.

---

## Using the VoiceMorpher

### The Main Menu

Use the **joystick Y axis** (up/down) to scroll through the menu. Press the **joystick button** to select an option.

| Menu Item | What it does |
|-----------|--------------|
| **Record** | Record your voice (up to 10 seconds) |
| **Play raw** | Play back your recording without any effects |
| **Passthrough** | Hear your voice live through the speaker in real time |
| **Long Record** | Record longer clips stored on flash memory |
| **Long Play** | Play back long recordings with effects |
| **Mood Music** | Play background music while you perform |
| **Theremin** | Play musical notes by moving your hand *(see below)* |
| **Settings** | Adjust volume, mic sensitivity, and other options |

### Recording Your Voice

1. Select **Record** from the menu and press the button
2. A sub-menu appears — use **joystick X** (left/right) to choose how many seconds to record (1–10 seconds)
3. Press the button to start recording — speak into the microphone!
4. When done, press the button to return to the menu

### Adjusting Effect Levels

When you select an effect like Pitch up, Echo, Ring mod, or Stutter, a level sub-menu appears before the effect plays:

- A progress bar shows the current level
- Push **joystick X left** to decrease the effect
- Push **joystick X right** to increase the effect
- Press the **button** to confirm and play the effect

The device remembers your last setting for each effect, so you don't have to re-adjust every time.

---

## Theremin Mode

A **theremin** is a musical instrument you play without touching it — you move your hands through the air to control pitch and volume. The VoiceMorpher has a built-in theremin!

<iframe width="560" height="315" src="https://www.youtube.com/embed/X2Z6hq8bkvU?si=OjuTx5EZq2rb5nJf" title="YouTube video player" frameborder="0" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" referrerpolicy="strict-origin-when-cross-origin" allowfullscreen></iframe>

### How to Enter Theremin Mode

Select **Theremin** from the main menu. A sub-menu appears with three options:

| Option | What it does |
|--------|--------------|
| **Play** | Start playing the theremin |
| **Sound** | Choose the theremin's voice (Sine, Kitten, Beavis, Nana, String) |
| **Pitch** | Choose the pitch controller (Joystick or Sonar) |

### Playing the Theremin

Once in Play mode, the screen shows:
- The current **note name** (e.g. `C4`) in large cyan text
- The current **frequency** in Hz
- A **pitch bar** — shows where in the range your hand is
- A **volume bar** — shows the current volume
- A badge showing **FREE** (continuous pitch) or **NOTES** (snaps to musical notes)

### Controls

| Action | Result |
|--------|--------|
| Move hand closer to sensor (or joystick Y up) | Higher pitch |
| Move hand further from sensor (or joystick Y down) | Lower pitch |
| Joystick X right | Volume up one step |
| Joystick X left | Volume down one step |
| Short button press | Toggle FREE ↔ NOTES (chromatic quantize) |
| Hold button for 0.5 s | Exit theremin mode |

> **NOTES mode** snaps the pitch to the nearest semitone on a standard musical scale — great for playing recognisable melodies. **FREE mode** lets you slide smoothly between pitches like a real theremin.

### Pitch Source: Joystick vs Sonar

Go to **Theremin → Pitch** to choose how pitch is controlled:

**Joystick** — push the joystick Y axis up for high notes, down for low notes. Quick and simple, no extra hardware needed.

**Sonar** — uses the HC-SR04 ultrasonic sensor. Hold your hand above the sensor and move it up and down:
- Hand **close** (≈ 5 cm) → highest pitch
- Hand **far** (≈ 30 cm) → lowest pitch

The pitch range covers **C3 to C6** — three full octaves, plenty for melodies. The sonar range (5–30 cm) can be changed in `config.h` by editing `TH_SONAR_MIN_CM` and `TH_SONAR_MAX_CM`.

### Volume Control in Sonar Mode

In sonar mode both hands are busy — one controlling pitch. So volume works differently:
- **Joystick right** → step volume up (it stays there)
- **Joystick left** → step volume down (it stays there)
- Volume starts at 50% each time you enter theremin mode

This means you can set the volume once and then forget about it, focusing both hands on playing.

### Theremin Sounds

| Sound | Description |
|-------|-------------|
| **Sine** | A pure, smooth electronic tone — classic theremin sound |
| **Kitten** | A cat meow sampled and pitch-shifted |
| **Beavis** | The famous laugh, looped and resampled |
| **Nana** | A vocal "na-na" sample |
| **String** | A string instrument sample for a more musical feel |

---

## How Each Effect Works

### Pitch Up / Pitch Down
Your recording is played back at a different speed. Faster playback = higher pitch (chipmunk). Slower playback = lower pitch (monster). This is the same trick used in old tape recorders — changing the speed of the tape.

### Reverse
The list of audio numbers is simply read backwards. The last sound you made becomes the first thing you hear.

### Echo
A delayed copy of the audio is mixed back in with the original. The delay time and how many repeats you hear are both adjustable with the level knob.

### Ring Modulation (Ring Mod)
The audio is multiplied by a sine wave (a smooth oscillating signal). This creates a buzzing, robot-like quality. The speed of the oscillation controls the character of the effect — low frequencies sound like tremolo, high frequencies sound robotic.

### Stutter
Your audio is chopped into short chunks and each chunk is repeated several times before moving on. Short chunks with many repeats = heavy stutter. Long chunks with few repeats = a subtle rhythmic effect.

---

## Concepts You're Learning

| Concept | Where you see it |
|---------|-----------------|
| **Digital audio** | The microphone converts air pressure into 11,025 numbers per second |
| **I2S protocol** | The 3-wire bus (CLK, WS, DATA) that streams audio between chips |
| **Sampling rate** | 11,025 Hz means 11,025 samples every second — enough for clear voice |
| **Memory management** | Fitting 10 seconds of audio (220,500 samples) into 512 KB of RAM |
| **Signal processing** | Changing audio by doing math on those numbers |
| **SPI & DMA** | How the ESP32 sends pixel data to the color display without slowing down audio |
| **Ultrasonic sensing** | The HC-SR04 sends a sound pulse and times how long the echo takes to return — distance = speed × time |
| **Theremin / continuous controller** | Playing music by changing a physical quantity (distance) rather than pressing keys |
| **FreeRTOS** | The operating system inside the ESP32 that keeps tasks running smoothly |

---

## Going Further

Once you're comfortable with this project, here are some ideas to explore:

- **Add more effects** — try a flanger (tiny time-varying echo) or a low-pass filter (muffle effect)
- **Save recordings to an SD card** — so you can share them
- **Add a second recording slot** — so you can layer two voices together
- **Add a second HC-SR04** — control volume with one hand and pitch with the other, like a real theremin
- **Add pitch correction** — snap the pitch to a specific musical scale (major, minor, pentatonic)
- **Draw a frequency spectrum** — use an FFT to show which frequencies are loudest on the color display

---

## Quick Troubleshooting

| Problem | Try this |
|---------|----------|
| Nothing on the display | Check SCL/MOSI/CS/DC wiring, that BL (GPIO 8) is HIGH, and that VCC is 3.3V |
| Colors look wrong (red and blue swapped) | Check `TFT_RGB_ORDER=0` is set in `platformio.ini` |
| No sound from speaker | Check AMP_SD is connected to GPIO 21 and the pin is HIGH |
| Recording sounds distorted | Make sure the microphone L/R pin is connected to GND |
| Theremin sonar has no effect | Check TRIG→GPIO 15 and ECHO→GPIO 16; sensor needs 5V |
| Theremin pitch stops changing before full range | This is normal if hand is beyond 30 cm — the range is intentionally short for easy play |
| Upload fails | Hold the BOOT button while clicking upload |
| Serial monitor shows garbled text | Set baud rate to **115200** |
