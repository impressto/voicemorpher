import os

markdown_content = """# 🚀 ESP32 Audio Foundations Workshop
**Duration:** 2 Hours | **Target Audience:** High School Students (Beginner Level)

Welcome to the ESP32 Audio Foundations Workshop! This document serves as your complete guide, curriculum layout, and technical reference for today's 2-hour hands-on session. Because the physical hardware is pre-soldered, this session maximizes student engagement by diving straight into mental models, hardware-to-code mapping, and immediate sensory feedback.

---

## 📅 Workshop Agenda Overview

| Time | Phase | Focus | Core Objective |
| :--- | :--- | :--- | :--- |
| **00:00 - 00:20** | **Phase 1** | The Mental Model & Hello World | Configure IDE, upload first sketch, flash internal LED. |
| **00:20 - 00:55** | **Phase 2** | The Digital Ear (I2S Input) | Wire up the INMP441, read raw audio, plot wave data. |
| **00:55 - 01:30** | **Phase 3** | The Digital Mouth (I2S Output) | Wire up the MAX98357A, generate synthetic audio tones. |
| **01:30 - 01:50** | **Phase 4** | The Audio Passthrough Loop | Read from microphone and pipe directly to speaker in real-time. |
| **01:50 - 02:00** | **Wrap-Up** | The Next Milestone Teaser | Showcase digital sound manipulation (pitch/echo) for Session 2. |

---

## 🛠️ Hardware Reference & Pin Mapping

Students should be visually guided to locate these specific pins on their ESP32 development boards. 

### 1. INMP441 Microphone (I2S RX)
* **SCK** ➡️ ESP32 **GPIO 4** (Serial Clock)
* **WS** ➡️ ESP32 **GPIO 5** (Word Select / Left-Right Clock)
* **SD** ➡️ ESP32 **GPIO 6** (Serial Data Out from Mic)
* **L/R** ➡️ **GND** (Configures the microphone to output on the Left channel)
* **VDD** ➡️ **3.3V**
* **GND** ➡️ **GND**

### 2. MAX98357A Amplifier (I2S TX)
* **BCLK** ➡️ ESP32 **GPIO 47** (Bit Clock)
* **LRCLK**➡️ ESP32 **GPIO 45** (Left-Right Clock)
* **DIN** ➡️ ESP32 **GPIO 38** (Data In to Amplifier)
* **AMP_SD**➡️ ESP32 **GPIO 21** (Shutdown/Mode pin - Keep high/enabled)
* **VIN** ➡️ **5V** (For maximum amplification headroom)
* **GND** ➡️ **GND**

### 3. Future UI Components (Parked for Session 2)
* *OLED Display:* SDA ➡️ GPIO 18 | SCL ➡️ GPIO 16
* *Joystick:* Axis Y ➡️ GPIO 20 (ADC) | Button ➡️ GPIO 35 (Digital Input)

---

## 📖 Step-by-Step Curriculum Modules

### 🛠️ Phase 1: The Mental Model & Hello World (20 Mins)
* **The Analogy:** Explain the micro-controller using a human anatomy framework:
  * **The ESP32** is the central nervous system and brain.
  * **The Microphone** is the ear (Input converting air pressure waves to numeric values).
  * **The Amplifier & Speaker** are the vocal cords and mouth (Output converting numbers back into physical vibrations).
  * **The GPIO Pins** are the individual nerve pathways.
* **Activity:**
  1. Open the Arduino IDE / VS Code.
  2. Select the matching ESP32 board target and correct serial COM port.
  3. Load the standard `Blink` example sketch.
  4. Edit the timing variable (`delay(1000);` to `delay(200);`) to prove control over the hardware clock.
* **Objective Achieved:** Confirmed USB communication, driver stability, and basic compiling mechanics before dealing with complex I2S buses.

---

### 🎙️ Phase 2: The Digital Ear (35 Mins)
* **Concept:** Introduce **I2S (Inter-IC Sound)**. Explain it simply as a 3-lane data highway optimized specifically for streaming digital audio smoothly without choking the processor.
  * **SCK (Clock):** The drummer setting the steady data pace.
  * **WS (Word Select):** Pointing to whether data belongs to the left or right audio frame.
  * **SD (Serial Data):** The continuous flow of sound volume amplitudes.
* **Activity:** Load a minimal I2S RX capture routine.
* **The Interactivity Hook:** Instruct students to open the **Arduino Serial Plotter** (Tools > Serial Plotter) instead of the text monitor. Have them clap, snap, or whistle. They will watch the raw waveform dynamically scale, demonstrating how physical acoustic energy transforms into real-time graphs.

---

### 🔊 Phase 3: The Digital Mouth (35 Mins)
* **Concept:** Transition from capture to generation. The amplifier decodes sequential binary metrics back into a smooth analog current that physically moves the speaker cone.
* **Activity:** Upload a frequency generator routine that populates an array with a calculated sine wave pattern (e.g., standard $440\\\\text{ Hz}$ tone).
* **Code Experimentation:** Instruct students to locate the frequency variable in the source code. Have them double it ($880\\\\text{ Hz}$) or halve it ($220\\\\text{ Hz}$) and re-upload. This creates an immediate cognitive link between programmatic mathematical variables and the perceived pitch of sound.

---

### 🔀 Phase 4: The Real-Time Passthrough Loop (20 Mins)
* **Concept:** Merging both independent pipelines. The ESP32 acts as a transparent bridge, reading a batch of data from the ear, holding it in memory briefly, and instantly shoving it out the mouth.
* **Activity:** Deploy the unified I2S Passthrough firmware.
* **The Big Win:** Students speak directly into the microphone and instantly hear their amplified voice output from the connected speaker.

---

## 💻 Complete Production Code Blueprint (Phase 4)
This clean implementation uses the modern native ESP32 I2S driver architecture (compatible with modern ESP-IDF v5.x wrappers and Arduino ESP32 core v3+ frameworks). It matches your exact pin configuration definitions: