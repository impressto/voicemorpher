#include "globals.h"
#include <math.h>

enum { WV_SINE = 0, WV_SQUARE, WV_SAW, WV_FM, WV_KS, WV_BB, WV_PD, WV_COUNT };

static const char *WV_NAMES[WV_COUNT] = {
    "Sine", "Square", "Sawtooth", "FM Synth", "Plucked", "Bytebeat", "Phase Dist"
};
static const char *WV_EQ[WV_COUNT] = {
    "y = A * sin(2*pi*f*t)",
    "y = sign(sin(2*pi*f*t))",
    "y = 2*(f*t mod 1) - 1",
    "y = sin(2*pi*f*t + 3*sin(4*pi*f*t))",
    "y[n] = (buf[n] + buf[n+1]) / 2,  N = Fs/f",
    "(t*(t>>8|t>>13))&255",
    "y = sin(distorted_phase(f,t))"
};
static const uint16_t WV_COL[WV_COUNT] = {
    0x07FF,  // cyan    — sine
    0xFFE0,  // yellow  — square
    0x07E0,  // green   — sawtooth
    0xF81F,  // magenta — FM
    0xFD20,  // orange  — plucked
    0xF800,  // red     — bytebeat
    0xB41F,  // violet  — phase distortion
};

static const int OSC_Y   = 38;    // starts right after header divider
static const int OSC_H   = 160;
static const int OSC_CY  = OSC_Y + OSC_H / 2;
static const int BOT_DIV = OSC_Y + OSC_H;     // 198 — divider below oscilloscope
static const int BOT_FML = BOT_DIV + 5;       // 203 — formula text baseline
static const int BOT_STA = BOT_DIV + 27;      // 225 — dynamic status baseline

// --- Sine LUT (1024 entries, DRAM — accessed during I2S DMA) -----------------
#define SIN_LUT_SIZE 1024
DRAM_ATTR static int16_t s_sinLUT[SIN_LUT_SIZE];
static bool s_lutReady = false;

static void initSinLUT()
{
    if (s_lutReady) return;
    for (int i = 0; i < SIN_LUT_SIZE; i++)
        s_sinLUT[i] = (int16_t)(sinf(2.0f * (float)M_PI * i / SIN_LUT_SIZE) * 32767.0f);
    s_lutReady = true;
}

// Negative radians wrap correctly via two's-complement & mask.
static inline float sinLUT(float r)
{
    int idx = (int)(r * (float)(SIN_LUT_SIZE / (2.0 * M_PI))) & (SIN_LUT_SIZE - 1);
    return s_sinLUT[idx] * (1.0f / 32767.0f);
}

// --- Karplus-Strong ring buffer (max size covers 80 Hz at 11025 Hz) ----------
#define KS_BUF_MAX 140
static int16_t s_ks_buf[KS_BUF_MAX];
static int     s_ks_pos = 0;
static int     s_ks_len = 0;

// --- Bytebeat state ----------------------------------------------------------
static uint32_t s_bb_t = 0;

// --- Phase Distortion state --------------------------------------------------
static float s_pd_bend = 0.7f;   // 0.5 = pure sine, 0.95 = near-sawtooth

// --- Oscilloscope capture buffer (~58 ms at 11025 Hz) -----------------------
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

    tft.drawRect(0, OSC_Y, TFT_W, OSC_H, tft.color565(40, 50, 100));
    tft.drawFastHLine(1, OSC_CY, TFT_W - 2, tft.color565(25, 35, 70));

    // Formula zone — colored to match waveform trace
    tft.drawFastHLine(0, BOT_DIV, TFT_W, tft.color565(30, 40, 80));
    tft.setTextSize(1);
    tft.setTextColor(WV_COL[wv]);
    tft.setCursor(4, BOT_FML);
    tft.print(WV_EQ[wv]);
}

// Draws the oscilloscope from captured audio samples.
// Zero-crossing sync keeps the display stable even as pitch changes.
// Amplitude reflects actual volume — flat line when sound is off.
static void drawOsc(uint16_t col)
{
    const int w     = TFT_W - 2;
    const int scale = OSC_H / 2 - 4;  // 82 pixels full scale at amp=28000

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
    tft.fillRect(0, BOT_STA - 2, TFT_W, TFT_H - (BOT_STA - 2), C_BG);
    tft.setTextColor(tft.color565(120, 140, 160)); tft.setTextSize(1);
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "%.0f Hz  |  Y:pitch  X:vol  tap:next  hold:exit", freq);
    tft.setCursor(4, BOT_STA);
    tft.print(tmp);
}

void runMathSynthMenu()
{
    initSinLUT();

    int   wv       = WV_SINE;
    float phase    = 0.0f;
    float phaseMod = 0.0f;
    s_bb_t    = 0;
    s_pd_bend = 0.7f;

    drawWaveLabFrame(wv);

    const int CHUNK = 256;
    int16_t   buf[CHUNK];   // 512 bytes on stack
    int       frameCount   = 0;
    int       ksPluckTimer = 0;
    const int KS_REPLUCK   = SAMPLE_RATE * 2 / 5;   // re-pluck every ~0.4 s
    unsigned long btnDownMs = 0;
    bool      btnWasUp = true;
    uint32_t  bb_step  = 1;

    while (true)
    {
        // ── Controls ──────────────────────────────────────────────────────────
        float yRaw = 1.0f - analogRead(JOY_Y_PIN) / 4095.0f;
        float xRaw = readJoystickXIntensity();
        float freq = 80.0f * powf(11.0f, yRaw);      // 80–880 Hz, logarithmic
        float dphi = 2.0f * (float)M_PI * freq / SAMPLE_RATE;
        float amp;

        if (wv == WV_PD) {
            // X controls distortion depth (0.5 = sine, 0.95 = near-sawtooth)
            s_pd_bend = 0.5f + xRaw * 0.45f;
            amp = 24000.0f;
        } else if (wv == WV_BB) {
            // Y controls t increment speed (pitch-ish), X controls volume
            bb_step = (uint32_t)(yRaw * 3.5f + 0.5f);
            if (bb_step < 1) bb_step = 1;
            amp = xRaw * 28000.0f;
        } else {
            amp = xRaw * 28000.0f;
        }
        if (amp > g_wl_max_amp) amp = g_wl_max_amp;

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
                case WV_SINE:
                    s = sinLUT(phase);
                    break;
                case WV_SQUARE:
                    s = (phase < (float)M_PI) ? 1.0f : -1.0f;
                    break;
                case WV_SAW:
                    s = phase * (float)(1.0 / M_PI) - 1.0f;
                    break;
                case WV_FM:
                    // LUT-based FM: modulator warps carrier phase index before lookup
                    s = sinLUT(phase + 3.0f * sinLUT(phaseMod));
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
                case WV_BB: {
                    // Robotic arpeggio — zero floating-point, bitwise only
                    uint8_t samp8 = (uint8_t)((s_bb_t * (s_bb_t >> 8 | s_bb_t >> 13)) & 255u);
                    s_bb_t += bb_step;
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_PD: {
                    // Phase distortion: compress first half, stretch second half
                    // bend=0.5 → pure sine; bend→0.95 → sawtooth-like harmonic stack
                    float phNorm = phase * (float)(1.0 / (2.0 * M_PI));
                    float bent = (phNorm < s_pd_bend)
                        ? (0.5f * phNorm / s_pd_bend)
                        : (0.5f + 0.5f * (phNorm - s_pd_bend) / (1.0f - s_pd_bend));
                    s = sinLUT(bent * (2.0f * (float)M_PI));
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
            if (wv == WV_BB) {
                tft.fillRect(0, BOT_STA - 2, TFT_W, TFT_H - (BOT_STA - 2), C_BG);
                tft.setTextColor(tft.color565(120, 140, 160)); tft.setTextSize(1);
                char tmp[48];
                snprintf(tmp, sizeof(tmp), "t_step=%u  |  Y:speed  X:vol  tap:next  hold:exit", (unsigned)bb_step);
                tft.setCursor(4, BOT_STA);
                tft.print(tmp);
            } else if (wv == WV_PD) {
                tft.fillRect(0, BOT_STA - 2, TFT_W, TFT_H - (BOT_STA - 2), C_BG);
                tft.setTextColor(tft.color565(120, 140, 160)); tft.setTextSize(1);
                char tmp[48];
                snprintf(tmp, sizeof(tmp), "%.0f Hz  depth=%.2f  |  Y:pitch  X:depth  hold:exit", freq, s_pd_bend);
                tft.setCursor(4, BOT_STA);
                tft.print(tmp);
            } else {
                updateFreqBar(freq);
            }
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
                s_ks_len = 0; ksPluckTimer = 0;
                s_bb_t = 0;
                drawWaveLabFrame(wv);
            }
            btnWasUp = true;
        }
    }

    stopTxAndFlush();
}
