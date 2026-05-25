# VoiceMorpher — ESP32-S3 Audio Effects Project

Turn your voice into something completely different! This project lets you record your voice and apply real-time sound effects like pitch shifting, reverse, echo, stutter, and ring modulation — all controlled with a joystick and displayed on a small screen.

<img width="1200" height="655" alt="voicemorpher_diagram" src="https://github.com/user-attachments/assets/f9d1c7ff-7ec6-4ded-a6cd-30878f4e66e6" />


---

## How It Works

Think of the ESP32-S3 microcontroller like a brain:

- 🎙️ **The microphone** is the ear — it captures sound and converts it to numbers
- 🧠 **The ESP32-S3** processes those numbers and applies effects
- 🔊 **The amplifier and speaker** are the mouth — they convert the numbers back into sound

Sound is just numbers. Once your voice is stored as a list of numbers, you can reverse the list (reverse effect), multiply the numbers by a wobbling wave (ring mod), or play it back at a different speed (pitch shift). That's the magic of digital audio!

---

## What You Need

| Part | Purpose |
|------|---------|
| ESP32-S3 DevKitC-1 (N16R8) | The brain — runs all the code |
| INMP441 microphone module | Captures your voice as a digital signal |
| MAX98357A amplifier module | Drives the speaker from a digital signal |
| Small speaker (4–8 Ω) | Makes the sound you can hear |
| SSD1306 OLED display (128×64) | Shows the menu on screen |
| Analog joystick module | Navigates the menu and adjusts effects |
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
| BCLK | GPIO 47 | Bit Clock — timing for audio data |
| LRCLK | GPIO 45 | Left/Right Clock — channel selector |
| DIN | GPIO 38 | Data In — audio numbers going to the amp |
| AMP_SD | GPIO 21 | Enable pin — must be HIGH to turn the amp on |
| VIN | 5V | Power (use 5V for best volume) |
| GND | GND | Ground |

### 🖥️ SSD1306 OLED Display → ESP32-S3 (I2C)

| Display Pin | ESP32-S3 Pin |
|-------------|--------------|
| SDA | GPIO 18 |
| SCL | GPIO 16 |
| VCC | 3.3V |
| GND | GND |

### 🕹️ Analog Joystick → ESP32-S3

| Joystick Pin | ESP32-S3 Pin | Purpose |
|--------------|--------------|---------|
| VRx (X axis) | GPIO 17 | Adjusts effect levels in sub-menus |
| VRy (Y axis) | GPIO 20 | Scrolls up and down the main menu |
| SW (Button) | GPIO 35 | Selects the highlighted menu item |
| VCC | 3.3V | Power |
| GND | GND | Ground |

---

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
4. The OLED display should light up with the main menu

> **Tip:** If the upload fails, hold the **BOOT** button on your ESP32 while clicking upload, then release it once uploading starts.

---

## Using the VoiceMorpher

### The Main Menu

Use the **joystick Y axis** (up/down) to scroll through the menu. Press the **joystick button** to select an option.

| Menu Item | What it does |
|-----------|--------------|
| **Record** | Record your voice (up to 10 seconds) |
| **Play raw** | Play back your recording without any effects |
| **Passthrough** | Hear your voice live through the speaker in real time |
| **Reverse** | Play the recording backwards |
| **Pitch up** | Make your voice higher (like a chipmunk) |
| **Pitch down** | Make your voice lower (like a giant) |
| **Echo** | Add an echo/delay effect |
| **Ring mod** | Add a robotic buzzing effect |
| **Stutter** | Chop your voice into repeating chunks |

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
| **FreeRTOS** | The operating system inside the ESP32 that keeps tasks running smoothly |

---

## Going Further

Once you're comfortable with this project, here are some ideas to explore:

- **Add more effects** — try a flanger (tiny time-varying echo) or a low-pass filter (muffle effect)
- **Save recordings to an SD card** — so you can share them
- **Add a second recording slot** — so you can layer two voices together
- **Visualize audio on the OLED** — draw a waveform on screen while recording
- **Add pitch correction** — snap the pitch to musical notes

---

## Quick Troubleshooting

| Problem | Try this |
|---------|----------|
| Nothing on the display | Check SDA/SCL wiring and that the display is getting 3.3V |
| No sound from speaker | Check AMP_SD is connected to GPIO 21 and the pin is HIGH |
| Recording sounds distorted | Make sure the microphone L/R pin is connected to GND |
| Upload fails | Hold the BOOT button while clicking upload |
| Serial monitor shows garbled text | Set baud rate to **115200** |
