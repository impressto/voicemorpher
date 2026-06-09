#include "globals.h"
#include <math.h>

enum { WV_SINE = 0, WV_SQUARE, WV_SAW, WV_FM, WV_KS, WV_COUNT };

static const char *WV_NAMES[WV_COUNT] = { "Sine", "Square", "Sawtooth", "FM Synth", "Plucked" };
static const char *WV_EQ[WV_COUNT] = {
    "y = A * sin(2*pi*f*t)",
    "y = sign(sin(2*pi*f*t))",
    "y = 2*(f*t mod 1) - 1",
    "y = sin(2*pi*f*t + 3*sin(4*pi*f*t))",
    "y[n] = (buf[n] + buf[n+1]) / 2,  N = Fs/f  (Karplus-Strong)"
};
static const uint16_t WV_COL[WV_COUNT] = {
    0x07FF,  // cyan    — sine
    0xFFE0,  // yellow  — square
    0x07E0,  // green   — sawtooth
    0xF81F,  // magenta — FM
    0xFD20,  // orange  — plucked
};

static const int OSC_Y  = 50;
static const int OSC_H  = 172;
static const int OSC_CY = OSC_Y + OSC_H / 2;

// Karplus-Strong ring buffer — max size covers 80 Hz at 11025 Hz sample rate
#define KS_BUF_MAX 140
static int16_t s_ks_buf[KS_BUF_MAX];
static int     s_ks_pos = 0;
static int     s_ks_len = 0;

// Oscilloscope capture buffer — 640 samples (~58 ms) for live waveform display
static int16_t  s_osc_buf[640];
static uint32_t s_osc_wr = 0;

static void ks_pluck(float freq)
{
    int len = (int)((float)SAMPLE_RATE / freq + 0.5f);
    if (len < 2)          len = 2;
    if (len > KS_BUF_MAX) len = KS_BUF_MAX;
    s_ks_len = len;
    s_ks_pos = 0;
    for (int i = 0; i < len; i++)
        s_ks_buf[i] = (int16_t)((rand() % 65536) - 32768);
}


static void drawWaveLabFrame(int wv)
{
    tft.fillScreen(C_BG);
    fillGradH(0, 0, TFT_W, 36, 0, 55, 140, 0, 15, 65);
    tft.drawFastHLine(0, 36, TFT_W, TFT_CYAN);
    tft.setTextColor(WV_COL[wv]); tft.setTextSize(2);
    tft.setCursor(8, 9); tft.print(WV_NAMES[wv]);

    tft.setTextSize(1);
    tft.setTextColor(tft.color565(160, 210, 255));
    tft.setCursor(4, 38); tft.print(WV_EQ[wv]);
    tft.drawFastHLine(0, 49, TFT_W, tft.color565(30, 40, 80));

    tft.drawRect(0, OSC_Y, TFT_W, OSC_H, tft.color565(40, 50, 100));
    tft.drawFastHLine(1, OSC_CY, TFT_W - 2, tft.color565(25, 35, 70));

    tft.setTextSize(1);
    tft.setTextColor(tft.color565(80, 90, 110));
    tft.setCursor(4, TFT_H - 10);
    tft.print("Y:pitch  X:vol  tap:wave  hold:exit");
}

// Draws the oscilloscope from captured audio samples.
// Zero-crossing sync keeps the display stable even as pitch changes.
// Amplitude reflects actual volume — flat line when sound is off.
static void drawOsc(uint16_t col)
{
    const int w     = TFT_W - 2;   // 318 pixels
    const int scale = OSC_H / 2 - 4;  // 82 pixels full scale at amp=28000

    // Find first upward zero-crossing in the older half of the ring buffer
    uint32_t base  = s_osc_wr - 640;
    int      start = 0;
    for (int i = 1; i < 320; i++) {
        int16_t a = s_osc_buf[(base + i - 1) & 639];
        int16_t b = s_osc_buf[(base + i)     & 639];
        if (a <= 0 && b > 0) { start = i; break; }
    }

    tft.fillRect(1, OSC_Y + 1, w, OSC_H - 2, C_BG);
    tft.drawFastHLine(1, OSC_CY, w, tft.color565(25, 35, 70));
    tft.startWrite();
    for (int x = 0; x < w - 1; x++) {
        int a  = s_osc_buf[(base + start + x)     & 639];
        int b  = s_osc_buf[(base + start + x + 1) & 639];
        int y0 = OSC_CY - a * scale / 28000;
        int y1 = OSC_CY - b * scale / 28000;
        y0 = constrain(y0, OSC_Y + 1, OSC_Y + OSC_H - 2);
        y1 = constrain(y1, OSC_Y + 1, OSC_Y + OSC_H - 2);
        tft.drawLine(x + 1, y0, x + 2, y1, col);
    }
    tft.endWrite();
}

static void updateFreqBar(float freq)
{
    tft.fillRect(0, TFT_H - 18, TFT_W, 18, C_BG);
    tft.setTextColor(tft.color565(160, 180, 200)); tft.setTextSize(1);
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "%.0f Hz  |  tap:wave  hold:exit", freq);
    tft.setCursor(4, TFT_H - 10);
    tft.print(tmp);
}

void runMathSynthMenu()
{
    int   wv       = WV_SINE;
    float phase    = 0.0f;
    float phaseMod = 0.0f;

    drawWaveLabFrame(wv);

    const int CHUNK = 256;
    int16_t   buf[CHUNK];   // 512 bytes on stack — the only audio buffer needed
    int       frameCount   = 0;
    int       ksPluckTimer = 0;
    const int KS_REPLUCK   = SAMPLE_RATE * 2 / 5;   // re-pluck every ~0.4 s
    unsigned long btnDownMs = 0;
    bool      btnWasUp = true;

    while (true)
    {
        // ── Controls ──────────────────────────────────────────────────────────
        float yRaw = 1.0f - analogRead(JOY_Y_PIN) / 4095.0f;
        float xRaw = readJoystickXIntensity();
        float freq = 80.0f * powf(11.0f, yRaw);      // 80–880 Hz, logarithmic
        float amp  = xRaw * 28000.0f;
        float dphi = 2.0f * (float)M_PI * freq / SAMPLE_RATE;

        // ── Karplus-Strong: pluck on entry or timer ───────────────────────────
        if (wv == WV_KS) {
            if (s_ks_len == 0 || (ksPluckTimer += CHUNK) >= KS_REPLUCK) {
                ks_pluck(freq);
                ksPluckTimer = 0;
            }
        }

        // ── Synthesise ────────────────────────────────────────────────────────
        for (int i = 0; i < CHUNK; i++) {
            float s = 0.0f;
            switch (wv) {
                case WV_SINE:   s = sinf(phase);                              break;
                case WV_SQUARE: s = (phase < (float)M_PI) ? 1.0f : -1.0f;   break;
                case WV_SAW:    s = phase * (float)(1.0/M_PI) - 1.0f;        break;
                case WV_FM:
                    s = sinf(phase + 3.0f * sinf(phaseMod));
                    phaseMod += dphi * 2.0f;
                    if (phaseMod >= 2.0f * (float)M_PI) phaseMod -= 2.0f * (float)M_PI;
                    break;
                case WV_KS: {
                    int16_t a = s_ks_buf[s_ks_pos];
                    int16_t b = s_ks_buf[(s_ks_pos + 1) % s_ks_len];
                    int16_t v = (int16_t)(((int32_t)a + b) >> 1);
                    s_ks_buf[s_ks_pos] = v;
                    s_ks_pos = (s_ks_pos + 1) % s_ks_len;
                    s = v / 32767.0f;
                    break;
                }
            }
            buf[i] = (int16_t)(s * amp);
            s_osc_buf[s_osc_wr & 639] = buf[i];
            s_osc_wr++;
            phase += dphi;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }

        size_t written = 0;
        i2s_write(I2S_TX_PORT, buf, CHUNK * sizeof(int16_t), &written, portMAX_DELAY);

        // ── Oscilloscope refresh every 4 chunks (~93 ms) ──────────────────────
        if (++frameCount >= 4) {
            frameCount = 0;
            drawOsc(WV_COL[wv]);
            updateFreqBar(freq);
        }

        // ── Button: tap = cycle wave, hold ≥500 ms = exit ─────────────────────
        bool btnNow = isJoystickButtonPressed();
        if (btnNow && btnWasUp) {
            btnDownMs = millis();
            btnWasUp  = false;
        } else if (!btnNow && !btnWasUp) {
            if (millis() - btnDownMs >= 500) {
                break;
            } else {
                wv = (wv + 1) % WV_COUNT;
                phase = 0.0f; phaseMod = 0.0f;
                s_ks_len = 0; ksPluckTimer = 0;  // force immediate pluck if KS selected
                drawWaveLabFrame(wv);
            }
            btnWasUp = true;
        }
    }

    stopTxAndFlush();
}
