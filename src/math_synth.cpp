#include "globals.h"
#include <math.h>

enum { WV_SINE = 0, WV_SQUARE, WV_SAW, WV_FM, WV_KS, WV_BLUEBERRY, WV_TECHNO, WV_RHYTHM, WV_DOOM, WV_NOLIMIT, WV_EASYBEAT, WV_CATGIRL, WV_NEUROFUNK, WV_STREETSURFER, WV_GROOVY2, WV_BASSLINE, WV_PD, WV_KICK, WV_SNARE, WV_HAT, WV_COUNT };

static const char *WV_NAMES[WV_COUNT] = {
    "Sine", "Square", "Sawtooth", "FM Synth", "Plucked", "Blueberry", "Techno", "Rhythm", "Doom", "No Limit", "Easybeat", "Cat-girl", "Neurofunk", "Street Surfer", "Crazy Groovy Beats 2", "Bassline", "Phase Dist", "Kick Drum", "Snare Drum", "Hi-Hat"
};
static const char *WV_EQ[WV_COUNT] = {
    "y = A * sin(2*pi*f*t)",
    "y = sign(sin(2*pi*f*t))",
    "y = 2*(f*t mod 1) - 1",
    "y = sin(2*pi*f*t + 3*sin(4*pi*f*t))",
    "y[n] = (buf[n] + buf[n+1]) / 2,  N = Fs/f",
    "t*(((t>>9)^((t>>9)-1)^1)%13)",
    "(A^A-1280)%11*t | (B^B-2)%13*t  [A=t/10,B=t/640]",
    "y = drum bytebeat pattern (Gabriel Miceli)",
    "(tanb|sinb)-sinb  [Doom E1M1, PortablePorcelain]",
    "sine sweep + bytebeat layers (mu6k 'No Limit')",
    "bytebeat 1fccccf1 (PortablePorcelain)",
    "17*t|(t>>2)+(t&32768?13:14)*t|t>>3|t>>5",
    "bytebeat 'Neurofunk' (SthephanShi)",
    "bytebeat 'Street Surfer' (skurk/raer)",
    "Crazy Groovy Beats 2 (Gabriel Miceli)",
    "bytebeat 'Bassline' (tejeez)",
    "y = sin(distorted_phase(f,t))",
    "y = sin(phi_n)*A_n,  f_n -> f_target",
    "y = sin(phi)*A_body + HPF(noise)*A_snare",
    "y = HPF(sum of 6 square osc) * A_n"
};
static const uint16_t WV_COL[WV_COUNT] = {
    0x07FF,  // cyan    — sine
    0xFFE0,  // yellow  — square
    0x07E0,  // green   — sawtooth
    0xF81F,  // magenta — FM
    0xFD20,  // orange  — plucked
    0x4810,  // indigo  — blueberry
    0xBFE0,  // lime    — techno
    0xD8A7,  // crimson — rhythm
    0xF920,  // fire red— doom
    0xF8B2,  // hot pink— no limit
    0x471A,  // turquoise—easybeat
    0xFDB8,  // light pink—cat-girl
    0x04BF,  // electric blue—neurofunk
    0xFC0E,  // coral   — street surfer
    0x07EF,  // spring green—groovy beats 2
    0x6180,  // deep amber—bassline
    0xB41F,  // violet  — phase distortion
    0xFBE0,  // gold    — kick drum
    0xC618,  // silver  — snare drum
    0x867D,  // sky blue— hi-hat
};

// All wave types share the Wave Lab menu icon — picker is text-driven.
static const uint8_t *WV_ICONS[WV_COUNT] = {
    ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN,
    ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN,
    ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN,
    ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN
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
static uint32_t s_bb_t    = 0;
static float    s_bb_frac = 0.0f;          // fractional part of t, for sub-1 step rates
static const float BB_BASE_HZ = 8000.0f;   // most bytebeat formulas online are tuned for an 8kHz t-clock

// Advances t at bb_step * (8kHz / SAMPLE_RATE) per output sample, so the
// Y-axis "speed" range matches the pitch/tempo these formulas were composed
// at, regardless of our 11025 Hz output rate.
static inline void bbAdvance(uint32_t bb_step)
{
    s_bb_frac += (BB_BASE_HZ / SAMPLE_RATE) * (float)bb_step;
    while (s_bb_frac >= 1.0f) {
        s_bb_frac -= 1.0f;
        s_bb_t++;
    }
}

// floor(255 / (5 - k) / 2) for k = (t>>17)&3, used by "The Rhythm".
static const int32_t RHYTHM_Y_MASK[4] = { 25, 31, 42, 63 };

// JS ToInt32: truncate toward zero, then wrap into the int32 range.
// Needed for bytebeat formulas that feed huge/fractional doubles (e.g. tan())
// through bitwise operators.
static inline int32_t jsToInt32(double x)
{
    if (!isfinite(x)) return 0;
    double m = fmod(trunc(x), 4294967296.0);   // mod 2^32
    if (m < 0.0) m += 4294967296.0;
    if (m >= 4294967296.0) m -= 4294967296.0;  // guard fp rounding at the boundary
    return (int32_t)(uint32_t)m;
}

// --- Phase Distortion state --------------------------------------------------
static float s_pd_bend = 0.7f;   // 0.5 = pure sine, 0.95 = near-sawtooth

// --- Drum engine utilities ----------------------------------------------------

// Fast 32-bit xorshift white noise, output range [-1, 1].
static inline float whiteNoise()
{
    static uint32_t x = 123456789;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return ((float)x / 4294967296.0f) * 2.0f - 1.0f;
}

// One-pole high-pass filter — strips the low end so noise sounds snappy, not static.
struct OnePoleHPF {
    float prevIn = 0.0f, prevOut = 0.0f;
    float process(float in)
    {
        float out = 0.85f * (prevOut + in - prevIn);
        prevIn = in;
        prevOut = out;
        return out;
    }
};

// --- Kick drum state (pitch sweep + amplitude decay) --------------------------
static float s_kick_amp   = 0.0f;
static float s_kick_phase = 0.0f;
static float s_kick_freq  = 45.0f;
static const float KICK_F_TARGET = 45.0f;

static void triggerKick(float fStart)
{
    s_kick_amp   = 1.0f;
    s_kick_phase = 0.0f;
    s_kick_freq  = fStart;
}

// --- Snare drum state (tone body + filtered noise) ----------------------------
static float     s_snr_body_amp  = 0.0f;
static float     s_snr_noise_amp = 0.0f;
static float     s_snr_phase     = 0.0f;
static OnePoleHPF s_snr_hpf;

static void triggerSnare()
{
    s_snr_body_amp  = 1.0f;
    s_snr_noise_amp = 0.85f;
    s_snr_phase     = 0.0f;
}

// --- Hi-hat state (6 metallic square oscillators + HPF) ------------------------
static float      s_hat_amp   = 0.0f;
static float      s_hat_phase[6] = {0,0,0,0,0,0};
static OnePoleHPF  s_hat_hpf;
static const float HAT_FREQS[6] = {245.0f, 306.0f, 384.0f, 421.0f, 511.0f, 725.0f};

static void triggerHat()
{
    s_hat_amp = 1.0f;
}

// One sample of sine-pitch-sweep kick. liveFStart is the f_start used on
// the next auto re-trigger (lets the joystick retune the kick live).
static inline float kickNextSample(float liveFStart)
{
    float s = sinLUT(s_kick_phase) * s_kick_amp;
    s_kick_phase += 2.0f * (float)M_PI * s_kick_freq / SAMPLE_RATE;
    if (s_kick_phase >= 2.0f * (float)M_PI) s_kick_phase -= 2.0f * (float)M_PI;
    s_kick_freq += (KICK_F_TARGET - s_kick_freq) * 0.01f;  // pitch sweep, ~25 ms
    s_kick_amp  *= 0.9992f;                                 // amplitude tail, ~0.8 s
    if (s_kick_amp < 0.001f) triggerKick(liveFStart);       // re-trigger with current pitch
    return s;
}

// One sample of tone-body + filtered-noise snare. dphi is the body
// oscillator's phase increment for this sample.
static inline float snrNextSample(float dphi)
{
    float body  = sinLUT(s_snr_phase) * s_snr_body_amp * 0.6f;
    float noise = s_snr_hpf.process(whiteNoise()) * s_snr_noise_amp * 0.6f;
    float s = body + noise;
    if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
    s_snr_phase += dphi;
    if (s_snr_phase >= 2.0f * (float)M_PI) s_snr_phase -= 2.0f * (float)M_PI;
    s_snr_body_amp  *= 0.997f;  // ~0.2 s
    s_snr_noise_amp *= 0.998f;  // ~0.3 s
    if (s_snr_noise_amp < 0.001f) triggerSnare();
    return s;
}

// One sample of 6-oscillator metallic hi-hat. alpha is this sample's
// amplitude decay factor (closed = fast, open = slow).
static inline float hatNextSample(float alpha)
{
    float sum = 0.0f;
    for (int h = 0; h < 6; h++) {
        s_hat_phase[h] += 2.0f * (float)M_PI * HAT_FREQS[h] / SAMPLE_RATE;
        if (s_hat_phase[h] >= 2.0f * (float)M_PI) s_hat_phase[h] -= 2.0f * (float)M_PI;
        sum += (s_hat_phase[h] < (float)M_PI) ? 1.0f : -1.0f;
    }
    float s = s_hat_hpf.process(sum / 6.0f) * s_hat_amp;
    if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
    s_hat_amp *= alpha;
    if (s_hat_amp < 0.001f) triggerHat();
    return s;
}

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
    snprintf(tmp, sizeof(tmp), "%.0f Hz  |  Y:pitch  X:vol  tap:next  hold:list", freq);
    tft.setCursor(4, BOT_STA);
    tft.print(tmp);
}

// Bytebeat formulas occupy a contiguous block of the WV_* enum
// (WV_BLUEBERRY..WV_BASSLINE). The top-level picker collapses that block
// into one "Bytebeats" entry; picking it opens a second list scoped to
// just those formulas.
static const int WV_BB_FIRST  = WV_BLUEBERRY;
static const int WV_BB_COUNT  = WV_BASSLINE - WV_BLUEBERRY + 1;
static const int WV_TOP_COUNT = WV_COUNT - WV_BB_COUNT + 1;

// Lets the user jump straight to any wave/drum type instead of cycling
// through them one tap at a time. Returns the picked index, or -1 (X = back).
static int showWaveLabPicker(int wv)
{
    const char    *topNames[WV_TOP_COUNT];
    const uint8_t *topIcons[WV_TOP_COUNT];
    int t = 0;
    for (int i = 0; i < WV_BB_FIRST; i++) { topNames[t] = WV_NAMES[i]; topIcons[t] = WV_ICONS[i]; t++; }
    topNames[t] = "Bytebeats"; topIcons[t] = ICON_THEREMIN; t++;
    for (int i = WV_BB_FIRST + WV_BB_COUNT; i < WV_COUNT; i++) { topNames[t] = WV_NAMES[i]; topIcons[t] = WV_ICONS[i]; t++; }

    int topSel = (wv >= WV_BB_FIRST && wv < WV_BB_FIRST + WV_BB_COUNT)
                  ? WV_BB_FIRST
                  : (wv < WV_BB_FIRST ? wv : wv - WV_BB_COUNT + 1);

    while (true) {
        int picked = showIconList(topNames, topIcons, WV_TOP_COUNT, topSel);
        if (picked < 0) return -1;   // X = back to main menu
        if (picked != WV_BB_FIRST) {
            return (picked < WV_BB_FIRST) ? picked : picked - 1 + WV_BB_COUNT;
        }

        int bbSel = (wv >= WV_BB_FIRST && wv < WV_BB_FIRST + WV_BB_COUNT) ? (wv - WV_BB_FIRST) : 0;
        int bbPicked = showIconList(WV_NAMES + WV_BB_FIRST, WV_ICONS + WV_BB_FIRST, WV_BB_COUNT, bbSel);
        if (bbPicked < 0) continue;  // X = back to top-level list
        return WV_BB_FIRST + bbPicked;
    }
}

void runMathSynthMenu()
{
    initSinLUT();

    int   wv       = WV_SINE;
    float phase    = 0.0f;
    float phaseMod = 0.0f;
    s_bb_t    = 0;
    s_pd_bend = 0.7f;

    const int CHUNK = 256;
    int16_t   buf[CHUNK];   // 512 bytes on stack
    int       frameCount   = 0;
    int       ksPluckTimer = 0;
    const int KS_REPLUCK   = SAMPLE_RATE * 2 / 5;   // re-pluck every ~0.4 s
    unsigned long btnDownMs = 0;
    bool      btnWasUp = true;
    uint32_t  bb_step  = 1;
    bool      needPicker = true;

    while (true)
    {
        // ── Wave picker: shown on entry and after a hold-to-exit ─────────────────
        if (needPicker) {
            int picked = showWaveLabPicker(wv);
            if (picked < 0) break;   // X = back to main menu
            wv = picked;

            phase = 0.0f; phaseMod = 0.0f;
            s_ks_len = 0; ksPluckTimer = 0;
            s_bb_t = 0; s_bb_frac = 0.0f;
            frameCount = 0;
            btnWasUp = true;

            float yRaw0 = 1.0f - analogRead(JOY_Y_PIN) / 4095.0f;
            float freq0 = 80.0f * powf(11.0f, yRaw0);
            if (wv == WV_KICK)  triggerKick(freq0);
            if (wv == WV_SNARE) triggerSnare();
            if (wv == WV_HAT)   triggerHat();

            drawWaveLabFrame(wv);
            needPicker = false;
        }

        // ── Controls ──────────────────────────────────────────────────────────
        float yRaw = 1.0f - analogRead(JOY_Y_PIN) / 4095.0f;
        float xRaw = readJoystickXIntensity();
        float freq = 80.0f * powf(11.0f, yRaw);      // 80–880 Hz, logarithmic
        float dphi = 2.0f * (float)M_PI * freq / SAMPLE_RATE;
        float amp;

        float snrDphi   = 0.0f;
        float hatAlpha  = 0.99f;

        if (wv == WV_PD) {
            // X controls distortion depth (0.5 = sine, 0.95 = near-sawtooth)
            s_pd_bend = 0.5f + xRaw * 0.45f;
            amp = 24000.0f;
        } else if (wv == WV_BLUEBERRY || wv == WV_TECHNO || wv == WV_RHYTHM || wv == WV_DOOM ||
                   wv == WV_NOLIMIT || wv == WV_EASYBEAT || wv == WV_CATGIRL || wv == WV_NEUROFUNK ||
                   wv == WV_STREETSURFER || wv == WV_GROOVY2 || wv == WV_BASSLINE) {
            // All bytebeat formulas are tuned for bb_step==1 (their native 8kHz
            // tempo); bb_step==2 plays them back at double speed. Default to
            // normal tempo, only doubling when the joystick is pushed near the top.
            bb_step = (yRaw >= 0.9f) ? 2 : 1;
            amp = xRaw * 28000.0f;
        } else if (wv == WV_SNARE) {
            // Y controls snare body tone (80–400 Hz), X controls volume
            float bodyFreq = 80.0f + yRaw * 320.0f;
            snrDphi = 2.0f * (float)M_PI * bodyFreq / SAMPLE_RATE;
            amp = xRaw * 28000.0f;
        } else if (wv == WV_HAT) {
            // Y controls decay length: forward = closed (fast), back = open (slow); X controls volume
            hatAlpha = 0.9975f - yRaw * 0.0125f;
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
                case WV_BLUEBERRY: {
                    // Blueberry (Stephen Boak, 2011) — t*(((t>>9)^((t>>9)-1)^1)%13)
                    uint32_t x = s_bb_t >> 9;
                    uint8_t samp8 = (uint8_t)((s_bb_t * (((x ^ (x - 1)) ^ 1u) % 13u)) & 255u);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_TECHNO: {
                    // "Techno" stereo bytebeat — left/right channels mixed to mono.
                    // L = ((A^(A-1280))%11)*t, A=t/10 ;  R = ((B^(B-2))%13)*t, B=t/640
                    int32_t t = (int32_t)s_bb_t;
                    int32_t a = t / 10;
                    int32_t b = t / 640;
                    int32_t lMod = (a ^ (a - 1280)) % 11;
                    int32_t rMod = (b ^ (b - 2)) % 13;
                    uint8_t l8 = (uint8_t)((lMod * s_bb_t) & 255u);
                    uint8_t r8 = (uint8_t)((rMod * s_bb_t) & 255u);
                    uint8_t samp8 = (uint8_t)(((uint16_t)l8 + r8) / 2);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_RHYTHM: {
                    // The Rhythm (Gabriel Miceli) — a=t-256
                    // X = (44*((t/256-28)|3) | (t*8 & t>>11 & t>>5) | (t*(a>>3&a>>4&a>>5&64) >> (t/16))) /2 & 127
                    // Y = (t ^ (t + t/256)) & RHYTHM_Y_MASK[(t>>17)&3]
                    int32_t t = (int32_t)s_bb_t;
                    int32_t a = t - 256;

                    int32_t p1 = 44 * ((int32_t)(t / 256.0f - 28.0f) | 3);
                    int32_t p2 = (t * 8) & (t >> 11) & (t >> 5);
                    int32_t shiftAmt = (t >> 4) & 31;
                    int32_t p3 = (t * ((a >> 3) & (a >> 4) & (a >> 5) & 64)) >> shiftAmt;
                    int32_t x = ((p1 | p2 | p3) / 2) & 127;

                    int32_t e1 = t ^ (t + (t >> 8));
                    int32_t y = e1 & RHYTHM_Y_MASK[(t >> 17) & 3];

                    uint8_t samp8 = (uint8_t)((x + y) & 255);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_DOOM: {
                    // "Doom E1M1" recreation (PortablePorcelain) — q[] is the melody,
                    // tan()/sin() of a huge angle produce the noisy/percussive texture.
                    static const char DRAM_ATTR DOOM_Q[] =
                        "5 5 JJ5 5 FF5 5 AA5 5 ==5 5 ==??5 5 JJ5 5 FF5 5 AA5 5 ========  "
                        "5 5 JJ5 5 FF5 5 AA5 5 ==5 5 ==??5 5 JJ5 5 FF5 5 AA5 5 ========  "
                        "< < XX< < RR< < MM< < II< < IILL< < XX< < RR< < MM< < IIIIIIII  "
                        "5 5 JJ5 5 FF5 5 AA5 5 ==5 5 ==??5 5 JJ5 5 FF5 5 AA5 5 ========  ";
                    static const int32_t DOOM_Q_LEN = (int32_t)sizeof(DOOM_Q) - 1;

                    int32_t t    = (int32_t)s_bb_t;
                    int32_t sIdx = (t / 540) % DOOM_Q_LEN;
                    int32_t charVal = (int32_t)(uint8_t)DOOM_Q[sIdx] - 32;
                    double  b = (double)charVal * ((double)t / 8.0);

                    double tanb = tan(b * M_PI / 512.0) * 64.0 - 128.0;
                    double sinb = sin(b * M_PI / 64.0) * 64.0 - 128.0;

                    int32_t combinedBits = jsToInt32(tanb) | jsToInt32(sinb);
                    double  combined = (double)combinedBits - sinb;

                    uint8_t samp8 = (uint8_t)(jsToInt32(combined) & 255);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_NOLIMIT: {
                    // "No Limit" (mu6k) — sine sweep + two derived bytebeat layers.
                    int32_t t   = (int32_t)s_bb_t;
                    int32_t t12 = t & 0xfff;
                    int32_t t16 = t & 0xffff;

                    double mult = 1.0;
                    if (t16 > 0x7fff) mult += 0.333;
                    if (t16 > 0xbfff) mult += 0.177;

                    // sin(2000/0) -> sin(inf) -> NaN -> jsToInt32 -> 0, same as JS.
                    double a = sin(2000.0 / (double)t12) * 127.0 * 0.2;

                    int32_t t2    = (int32_t)((uint32_t)t << 1);
                    int32_t bByte = jsToInt32((double)t2 * mult) & 0xff;
                    double  b = (double)bByte * ((double)t12 / (double)0x1fff) * 0.4;

                    int32_t cBits = ((t >> 4) ^ (t >> 6)) | (t >> 10);
                    int32_t cMix  = cBits | jsToInt32((double)t * 3.0 * mult);
                    double  c = (double)(cMix & 0xff) * 0.25;

                    uint8_t samp8 = (uint8_t)(jsToInt32(a + b + c) & 255);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_EASYBEAT: {
                    // "1fccccf1" / "Easybeat" (PortablePorcelain)
                    static const double EASYBEAT_POW[4] = { 0.5, 1.0, 2.0, 4.0 };  // 2^((t>>16&3)-1)

                    int32_t t = (int32_t)s_bb_t;

                    int32_t shift1  = (t >> 16) & 3;
                    double  tScaled = (double)t * EASYBEAT_POW[shift1];

                    int32_t shiftAmt = ((t >> 10) * (t >> 11)) & 31;
                    int32_t mod8     = (0x1fccccf1 >> shiftAmt) % 8;

                    int32_t a = jsToInt32(tScaled * (double)mod8) | (t >> 3);
                    int32_t b = (a % 128) - 32;

                    // sin(5000/0) -> sin(inf) -> NaN -> jsToInt32 -> 0, same as JS.
                    int32_t t12 = t & 4095;
                    double  c = sin(5000.0 / (double)t12) * 32.0;

                    uint8_t samp8 = (uint8_t)(jsToInt32((double)b + c) & 255);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_CATGIRL: {
                    // "Cat-girl. Nya!" (SthephanShi)
                    // 17*t | ((t>>2) + ((t&32768?13:14)*t)) | (t>>3) | (t>>5)
                    // All-integer; uint32_t wraparound matches JS ToInt32 for
                    // non-negative operands, so no float/jsToInt32 needed.
                    uint32_t t = s_bb_t;
                    uint32_t mulFactor = (t & 32768u) ? 13u : 14u;
                    uint32_t result = (17u * t) | ((t >> 2) + (mulFactor * t)) | (t >> 3) | (t >> 5);
                    uint8_t samp8 = (uint8_t)(result & 0xff);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_NEUROFUNK: {
                    // "Neurofunk" (SthephanShi)
                    // t*((t&4096?t%65536<59392?7:t&7:16)+(1&t>>14))
                    //   >> (3&(-t>>(t&2048?2:10)))
                    //   | t>>(t&16384?t&4096?10:3:2)
                    // All-integer, uint32_t throughout: the two shift amounts that
                    // feed into "& 3" (2 and 10) only ever expose bits below bit 31,
                    // so logical vs arithmetic shift makes no difference there, and
                    // the final "& 0xff" similarly hides the sign-bit difference in
                    // (aVal >> b). No jsToInt32/double needed.
                    uint32_t t = s_bb_t;

                    uint32_t aTerm1;
                    if (t & 4096u) {
                        aTerm1 = ((t % 65536u) < 59392u) ? 7u : (t & 7u);
                    } else {
                        aTerm1 = 16u;
                    }
                    uint32_t aTerm2 = 1u & (t >> 14);
                    uint32_t aVal = t * (aTerm1 + aTerm2);

                    uint32_t shiftB = (t & 2048u) ? 2u : 10u;
                    uint32_t b = 3u & ((-t) >> shiftB);

                    uint32_t shiftC = (t & 16384u) ? ((t & 4096u) ? 10u : 3u) : 2u;
                    uint32_t c = t >> shiftC;

                    uint8_t samp8 = (uint8_t)(((aVal >> b) | c) & 0xffu);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_STREETSURFER: {
                    // "Street Surfer" (skurk / raer)
                    // (t&4096) ? ((t*(t^t%255) | (t>>4)) >> 1)
                    //          : (t>>3) | ((t&8192) ? t<<2 : t)
                    // All-integer, uint32_t throughout: t%255 for non-negative t
                    // matches JS, and the final ">>1" / "&0xff" hide any
                    // logical-vs-arithmetic shift difference (only bit 31 of the
                    // shifted value is affected, well above the bits kept by &0xff).
                    uint32_t t = s_bb_t;

                    uint32_t val;
                    if (t & 4096u) {
                        uint32_t x = t ^ (t % 255u);
                        uint32_t prod = t * x;
                        val = (prod | (t >> 4)) >> 1;
                    } else {
                        uint32_t orVal = (t & 8192u) ? (t << 2) : t;
                        val = (t >> 3) | orVal;
                    }

                    uint8_t samp8 = (uint8_t)(val & 0xffu);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_GROOVY2: {
                    // "Crazy Groovy Beats 2" (Gabriel Miceli)
                    // d=t>>12&1, h=(t>>9)+4
                    // (t*t*(t&255)*d/156
                    //   + (t*(t^15)+t)*((h|t/2048+1&127)-h)/64)
                    //  & (127-d*((t>>5&127)*2/3+32))
                    //
                    // (t/2048+1)&127 floors exactly via t>>11 (2048=2^11), so
                    // ((h|t/2048+1&127)-h) is an exact non-negative integer
                    // ("factor"). The big t*t*... and .../64 products still need
                    // double + jsToInt32 to match JS's ToInt32-on-a-fraction
                    // behaviour for "&".
                    uint32_t t = s_bb_t;
                    uint32_t d = (t >> 12) & 1u;
                    uint32_t h = (t >> 9) + 4u;

                    uint32_t innerMask = ((t >> 11) + 1u) & 127u;
                    uint32_t factor = (h | innerMask) - h;

                    double td = (double)t;
                    double A = td * td * (double)(t & 255u) * (double)d / 156.0;
                    double B = (td * (double)(t ^ 15u) + td) * (double)factor / 64.0;

                    uint32_t t5 = (t >> 5) & 127u;
                    double C = 127.0 - (double)d * ((double)t5 * 2.0 / 3.0 + 32.0);

                    int32_t result = jsToInt32(A + B) & jsToInt32(C);
                    uint8_t samp8 = (uint8_t)(result & 0xff);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_BASSLINE: {
                    // "Bassline" (tejeez)
                    // (~t>>2)*((127&t*(7&t>>10))<(245&t*(2+(5&t>>14))))
                    //
                    // The two masked products only need their low bits, which
                    // survive uint32_t wraparound exactly, so plain integer
                    // math reproduces JS's ToInt32-then-mask here. ~t>>2 is
                    // computed on a signed int32 so >> sign-extends (arithmetic
                    // shift), matching JS's >> on a negative number.
                    uint32_t t = s_bb_t;
                    uint32_t lhs = (t * (7u & (t >> 10))) & 127u;
                    uint32_t rhs = (t * (2u + (5u & (t >> 14)))) & 245u;

                    int32_t notT2 = ~(int32_t)t >> 2;
                    int32_t result = (lhs < rhs) ? notT2 : 0;

                    uint8_t samp8 = (uint8_t)(result & 0xff);
                    bbAdvance(bb_step);
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
                case WV_KICK:
                    s = kickNextSample(freq);
                    break;
                case WV_SNARE:
                    s = snrNextSample(snrDphi);
                    break;
                case WV_HAT:
                    s = hatNextSample(hatAlpha);
                    break;
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
            if (wv == WV_BLUEBERRY || wv == WV_TECHNO || wv == WV_RHYTHM || wv == WV_DOOM || wv == WV_NOLIMIT || wv == WV_EASYBEAT || wv == WV_CATGIRL || wv == WV_NEUROFUNK || wv == WV_STREETSURFER || wv == WV_GROOVY2 || wv == WV_BASSLINE) {
                tft.fillRect(0, BOT_STA - 2, TFT_W, TFT_H - (BOT_STA - 2), C_BG);
                tft.setTextColor(tft.color565(120, 140, 160)); tft.setTextSize(1);
                char tmp[48];
                snprintf(tmp, sizeof(tmp), "t_step=%u  |  Y:speed  X:vol  tap:next  hold:list", (unsigned)bb_step);
                tft.setCursor(4, BOT_STA);
                tft.print(tmp);
            } else if (wv == WV_PD) {
                tft.fillRect(0, BOT_STA - 2, TFT_W, TFT_H - (BOT_STA - 2), C_BG);
                tft.setTextColor(tft.color565(120, 140, 160)); tft.setTextSize(1);
                char tmp[48];
                snprintf(tmp, sizeof(tmp), "%.0f Hz  depth=%.2f  |  Y:pitch  X:depth  hold:list", freq, s_pd_bend);
                tft.setCursor(4, BOT_STA);
                tft.print(tmp);
            } else if (wv == WV_KICK) {
                tft.fillRect(0, BOT_STA - 2, TFT_W, TFT_H - (BOT_STA - 2), C_BG);
                tft.setTextColor(tft.color565(120, 140, 160)); tft.setTextSize(1);
                char tmp[48];
                snprintf(tmp, sizeof(tmp), "%.0f->%.0fHz | Y:pitch X:vol tap:next hold:list", freq, KICK_F_TARGET);
                tft.setCursor(4, BOT_STA);
                tft.print(tmp);
            } else if (wv == WV_SNARE) {
                tft.fillRect(0, BOT_STA - 2, TFT_W, TFT_H - (BOT_STA - 2), C_BG);
                tft.setTextColor(tft.color565(120, 140, 160)); tft.setTextSize(1);
                char tmp[48];
                snprintf(tmp, sizeof(tmp), "body=%.0fHz | Y:tone X:vol tap:next hold:list", 80.0f + yRaw * 320.0f);
                tft.setCursor(4, BOT_STA);
                tft.print(tmp);
            } else if (wv == WV_HAT) {
                tft.fillRect(0, BOT_STA - 2, TFT_W, TFT_H - (BOT_STA - 2), C_BG);
                tft.setTextColor(tft.color565(120, 140, 160)); tft.setTextSize(1);
                char tmp[48];
                snprintf(tmp, sizeof(tmp), "decay=%s | Y:decay X:vol tap:next hold:list", (yRaw < 0.5f) ? "open" : "closed");
                tft.setCursor(4, BOT_STA);
                tft.print(tmp);
            } else {
                updateFreqBar(freq);
            }
        }

        // ── Button: tap = cycle wave, hold ≥500 ms = back to picker ───────────
        bool btnNow = isJoystickButtonPressed();
        if (btnNow && btnWasUp) {
            btnDownMs = millis();
            btnWasUp  = false;
        } else if (!btnNow && !btnWasUp) {
            if (millis() - btnDownMs >= 500) {
                needPicker = true;
            } else {
                wv = (wv + 1) % WV_COUNT;
                phase = 0.0f; phaseMod = 0.0f;
                s_ks_len = 0; ksPluckTimer = 0;
                s_bb_t = 0; s_bb_frac = 0.0f;
                if (wv == WV_KICK)  triggerKick(freq);
                if (wv == WV_SNARE) triggerSnare();
                if (wv == WV_HAT)   triggerHat();
                drawWaveLabFrame(wv);
            }
            btnWasUp = true;
        }
    }

    stopTxAndFlush();
}
