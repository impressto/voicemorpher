#include "globals.h"
#include <math.h>

enum { WV_SINE = 0, WV_SQUARE, WV_SAW, WV_FM, WV_KS, WV_BLUEBERRY, WV_TECHNO, WV_RHYTHM, WV_DOOM, WV_EASYBEAT, WV_CATGIRL, WV_NEUROFUNK, WV_STREETSURFER, WV_GROOVY2, WV_BASSLINE, WV_DAFT, WV_BLIPPY, WV_REBEL, WV_GORT, WV_SMOOTH, WV_SADNESS, WV_DRUMKIT, WV_SWAG, WV_BERLIN, WV_PD, WV_KICK, WV_SNARE, WV_HAT, WV_COUNT };

static const char *WV_NAMES[WV_COUNT] = {
    "Sine", "Square", "Sawtooth", "FM Synth", "Plucked", "Blueberry", "Techno", "Rhythm", "Doom", "Easybeat", "Cat-girl", "Neurofunk", "Street Surfer", "Crazy Groovy Beats 2", "Bassline", "Daft", "Blippy", "Rebel", "Gort Dance", "Smooth Thing", "Sadness", "Drumkit", "Swag", "Berlin Dance", "Phase Dist", "Kick Drum", "Snare Drum", "Hi-Hat"
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
    "bytebeat 1fccccf1 (PortablePorcelain)",
    "17*t|(t>>2)+(t&32768?13:14)*t|t>>3|t>>5",
    "bytebeat 'Neurofunk' (SthephanShi)",
    "bytebeat 'Street Surfer' (skurk/raer)",
    "Crazy Groovy Beats 2 (Gabriel Miceli)",
    "bytebeat 'Bassline' (tejeez)",
    "bytebeat 'Daft'",
    "bytebeat 'Blippy'",
    "bytebeat 'Rebel'",
    "bytebeat 'Gort Dance'",
    "bytebeat 'Smooth Thing'",
    "bytebeat 'Sadness'",
    "bytebeat 'Drumkit'",
    "bytebeat 'Swag'",
    "bytebeat 'Berlin Dance'",
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
    0x471A,  // turquoise—easybeat
    0xFDB8,  // light pink—cat-girl
    0x04BF,  // electric blue—neurofunk
    0xFC0E,  // coral   — street surfer
    0x07EF,  // spring green—groovy beats 2
    0x6180,  // deep amber—bassline
    0x4A69,  // steel blue—daft
    0x6FFF,  // light blue—blippy
    0xF884,  // red     — rebel
    0x4DF7,  // teal    — gort dance
    0xCE9F,  // periwinkle—smooth thing
    0x051F,  // ocean blue — sadness
    0xFB20,  // burnt orange — drumkit
    0xFE19,  // gold     — swag
    0xC0FF,  // neon lavender — berlin dance
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
    ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN,
    ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN,
    ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN, ICON_THEREMIN
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

// One-pole low-pass applied to the raw bytebeat sample before amp scaling /
// pitch shift. Several formulas (e.g. EasyBeat's sin(5000/(t&4095)) term)
// produce brief bursts of near-impulse, near-Nyquist content that alias into
// harsh "choppy" clicks at 11025 Hz — this smooths those transients.
static float s_bbLpf = 0.0f;
static const float BB_LPF_ALPHA = 0.5f;

// Advances t at bb_step * (8kHz / SAMPLE_RATE) per output sample, so the
// Y-axis "pitch" range matches the tempo these formulas were composed at,
// regardless of our 11025 Hz output rate. bb_step is a continuous
// multiplier — values < 1 pitch the formula down, > 1 pitch it up — the
// fractional accumulator below tracks any positive value correctly.
static inline void bbAdvance(float bb_step)
{
    s_bb_frac += (BB_BASE_HZ / SAMPLE_RATE) * bb_step;
    while (s_bb_frac >= 1.0f) {
        s_bb_frac -= 1.0f;
        s_bb_t++;
    }
}

// --- Bytebeat granular pitch shift -------------------------------------------
// Same overlap-add grain technique as the Pitch Up/Down effects in
// audio_effects.cpp (fx 7/8), generalized to a continuous ratio. This lets Y
// shift the *pitch* of the output while bb_step (and therefore the formula's
// pattern/rhythm timing) stays fixed at its composed tempo. ratio>1 = up,
// ratio<1 = down, ratio==1 = passthrough (after a fixed grain delay).
#define PB_BUF      1024
#define PB_GRAIN    256
#define PB_FADE_LEN 48
#define PB_GAP      48
static int16_t s_pbRing[PB_BUF];
static int32_t s_pbWPos  = 0;
static float   s_pbRPos  = -(float)(PB_GRAIN * 2);
static float   s_pbRPos2 = 0.0f;
static int     s_pbFade  = 0;

static inline int16_t bbPitchShift(int16_t in, float ratio)
{
    s_pbRing[s_pbWPos % PB_BUF] = in;
    s_pbWPos++;

    float outF = 0.0f;
    if (s_pbFade > 0) {
        float alpha = (float)s_pbFade / PB_FADE_LEN;
        int p0 = ((int)s_pbRPos)  % PB_BUF, p1 = (p0 + 1) % PB_BUF;
        float pf = s_pbRPos  - floorf(s_pbRPos);
        float s1 = (1.0f - pf) * s_pbRing[p0] + pf * s_pbRing[p1];
        int q0 = ((int)s_pbRPos2) % PB_BUF, q1 = (q0 + 1) % PB_BUF;
        float qf = s_pbRPos2 - floorf(s_pbRPos2);
        float s2 = (1.0f - qf) * s_pbRing[q0] + qf * s_pbRing[q1];
        outF = (1.0f - alpha) * s1 + alpha * s2;
        s_pbRPos2 += ratio;
        s_pbFade--;
    } else if (s_pbRPos >= 0.0f) {
        int i0 = ((int)s_pbRPos) % PB_BUF, i1 = (i0 + 1) % PB_BUF;
        float fr = s_pbRPos - floorf(s_pbRPos);
        outF = (1.0f - fr) * s_pbRing[i0] + fr * s_pbRing[i1];
    }

    s_pbRPos += ratio;

    if (s_pbRPos >= 0.0f) {
        int gap = s_pbWPos - (int)s_pbRPos;
        if (gap < PB_GAP) {
            // read caught up to write (pitch up) — rewind into history
            s_pbRPos2 = s_pbRPos;
            s_pbRPos -= (float)PB_GRAIN;
            s_pbFade  = PB_FADE_LEN;
        } else if (gap > PB_BUF - PB_GAP * 2) {
            // read fell behind write (pitch down) — skip ahead
            s_pbRPos2 = s_pbRPos;
            s_pbRPos += (float)PB_GRAIN;
            s_pbFade  = PB_FADE_LEN;
        }
        if (s_pbWPos > PB_BUF * 8) {
            int sub = (s_pbWPos / PB_BUF - 4) * PB_BUF;
            s_pbWPos  -= sub;
            s_pbRPos  -= (float)sub;
            s_pbRPos2 -= (float)sub;
        }
    }

    if (outF > 32767.0f) outF = 32767.0f;
    else if (outF < -32768.0f) outF = -32768.0f;
    return (int16_t)outF;
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

// JS Math.random(): uniform [0, 1).
static inline double bbRandom()
{
    return (double)rand() / ((double)RAND_MAX + 1.0);
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

static const char *BB_NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// bbSemitone (-12..+12) -> note name, treating 0 as C4 (so the Y axis's
// full travel spans C3..C5).
static void bbNoteName(int semitone, char *out, size_t outSize)
{
    int idx    = ((semitone % 12) + 12) % 12;
    int octave = 4 + (int)floorf((float)semitone / 12.0f);
    snprintf(out, outSize, "%s%d", BB_NOTE_NAMES[idx], octave);
}

// Top-right badge showing the note the Y axis is currently pitched to.
static void drawBBNote(int semitone)
{
    char note[8];
    bbNoteName(semitone, note, sizeof(note));
    tft.fillRoundRect(TFT_W - 56, 6, 48, 24, 4, tft.color565(40, 50, 100));
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(TFT_W - 52, 10);
    tft.print(note);
}

// Bytebeat formulas occupy a contiguous block of the WV_* enum
// (WV_BLUEBERRY..WV_BERLIN). The picker's top level is just "Waves" vs
// "Bytebeats" — picking one opens a second list scoped to just those
// wave types, keeping the two families fully separate.
static const int WV_BB_FIRST   = WV_BLUEBERRY;
static const int WV_BB_COUNT   = WV_BERLIN - WV_BLUEBERRY + 1;
static const int WV_WAVE_COUNT = WV_COUNT - WV_BB_COUNT;

// showWaveLabPicker() return values outside [0, WV_COUNT) — mirrors
// showIconList()'s -1="back" convention, plus sentinels for the
// "Long Loop"/"Short Loop" entries at the top of the Bytebeats list.
static const int WV_PICK_BACK       = -1;
static const int WV_PICK_LOOP_LONG  = -2;
static const int WV_PICK_LOOP_SHORT = -3;

enum { WLCAT_BYTEBEATS = 0, WLCAT_WAVES, WLCAT_COUNT };
static const char    *WLCAT_NAMES[WLCAT_COUNT] = { "Bytebeats", "Waves" };
static const uint8_t *WLCAT_ICONS[WLCAT_COUNT] = { ICON_THEREMIN, ICON_THEREMIN };

// Moves to the next wave/drum within the same top-level category (Waves vs
// Bytebeats), wrapping at that category's boundary instead of crossing into
// the other family.
static int nextWaveInCategory(int wv)
{
    if (wv >= WV_BB_FIRST && wv < WV_BB_FIRST + WV_BB_COUNT) {
        int rel = (wv - WV_BB_FIRST + 1) % WV_BB_COUNT;
        return WV_BB_FIRST + rel;
    }
    int next = wv + 1;
    if (next == WV_BB_FIRST) next += WV_BB_COUNT;
    if (next >= WV_COUNT) next = 0;
    return next;
}

// Lets the user jump straight to any wave/drum type instead of cycling
// through them one tap at a time. Returns the picked index, WV_PICK_BACK
// (X = back to main menu), or WV_PICK_LOOP_LONG/WV_PICK_LOOP_SHORT
// ("Long Loop"/"Short Loop" picked from the Bytebeats list).
static int showWaveLabPicker(int wv)
{
    const char    *waveNames[WV_WAVE_COUNT];
    const uint8_t *waveIcons[WV_WAVE_COUNT];
    int            waveIdx[WV_WAVE_COUNT];
    int w = 0;
    for (int i = 0; i < WV_COUNT; i++) {
        if (i >= WV_BB_FIRST && i < WV_BB_FIRST + WV_BB_COUNT) continue;
        waveNames[w] = WV_NAMES[i];
        waveIcons[w] = WV_ICONS[i];
        waveIdx[w]   = i;
        w++;
    }

    // Bytebeats list, with "Long Loop"/"Short Loop" entries prepended.
    const char    *bbNames[WV_BB_COUNT + 2];
    const uint8_t *bbIcons[WV_BB_COUNT + 2];
    bbNames[0] = "Long Loop";
    bbIcons[0] = ICON_THEREMIN;
    bbNames[1] = "Short Loop";
    bbIcons[1] = ICON_THEREMIN;
    for (int i = 0; i < WV_BB_COUNT; i++) {
        bbNames[i + 2] = WV_NAMES[WV_BB_FIRST + i];
        bbIcons[i + 2] = WV_ICONS[WV_BB_FIRST + i];
    }

    bool isBB  = (wv >= WV_BB_FIRST && wv < WV_BB_FIRST + WV_BB_COUNT);
    int catSel = isBB ? WLCAT_BYTEBEATS : WLCAT_WAVES;

    while (true) {
        int cat = showIconList(WLCAT_NAMES, WLCAT_ICONS, WLCAT_COUNT, catSel);
        if (cat < 0) return WV_PICK_BACK;   // X = back to main menu

        if (cat == WLCAT_WAVES) {
            int sel = 0;
            for (int i = 0; i < WV_WAVE_COUNT; i++) if (waveIdx[i] == wv) { sel = i; break; }
            int picked = showIconList(waveNames, waveIcons, WV_WAVE_COUNT, sel);
            if (picked < 0) continue;  // X = back to category list
            return waveIdx[picked];
        } else {
            int sel = isBB ? (wv - WV_BB_FIRST + 2) : 0;
            int picked = showIconList(bbNames, bbIcons, WV_BB_COUNT + 2, sel);
            if (picked < 0) continue;  // X = back to category list
            if (picked == 0) return WV_PICK_LOOP_LONG;
            if (picked == 1) return WV_PICK_LOOP_SHORT;
            return WV_BB_FIRST + (picked - 2);
        }
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
    unsigned long lastTapMs = 0;
    bool      btnWasUp = true;
    float     bb_step    = 1.0f;
    int       bbSemitone = 0;   // -12..+12, for the status line
    bool      needPicker = true;

    // ── "Loop All" mode: cycles every Bytebeats formula ───────────────────────
    bool      loopAll        = false;
    int       loopAllRel     = 0;        // 0..WV_BB_COUNT-1, relative to WV_BB_FIRST
    unsigned long loopAllStartMs = 0;
    unsigned long loopAllMs      = 60000UL;   // 60s for "Long Loop", 10s for "Short Loop"
    static const unsigned long LOOP_LONG_MS  = 60000UL;
    static const unsigned long LOOP_SHORT_MS = 10000UL;

    while (true)
    {
        // ── Wave picker: shown on entry and after a hold-to-exit ─────────────────
        if (needPicker) {
            // showWaveLabPicker() blocks on input without feeding i2s_write,
            // so the DMA ring would otherwise loop the last ~186ms of audio
            // (8 x 256-sample buffers) the whole time the picker is open.
            i2s_zero_dma_buffer(I2S_TX_PORT);

            int picked = showWaveLabPicker(wv);
            if (picked == WV_PICK_BACK) break;   // X = back to main menu

            if (picked == WV_PICK_LOOP_LONG || picked == WV_PICK_LOOP_SHORT) {
                loopAll    = true;
                loopAllRel = 0;
                loopAllMs  = (picked == WV_PICK_LOOP_LONG) ? LOOP_LONG_MS : LOOP_SHORT_MS;
                wv = WV_BB_FIRST + loopAllRel;
                loopAllStartMs = millis();
            } else {
                loopAll = false;
                wv = picked;
            }

            phase = 0.0f; phaseMod = 0.0f;
            s_ks_len = 0; ksPluckTimer = 0;
            s_bb_t = 0; s_bb_frac = 0.0f; s_bbLpf = 0.0f;
            frameCount = 0;
            btnWasUp = true;
            lastTapMs = 0;

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
        bool  isBB        = false;
        float pitchRatio  = 1.0f;

        float snrDphi   = 0.0f;
        float hatAlpha  = 0.99f;

        if (wv == WV_PD) {
            // X controls distortion depth (0.5 = sine, 0.95 = near-sawtooth)
            s_pd_bend = 0.5f + xRaw * 0.45f;
            amp = 24000.0f;
        } else if (wv == WV_BLUEBERRY || wv == WV_TECHNO || wv == WV_RHYTHM || wv == WV_DOOM ||
                   wv == WV_EASYBEAT || wv == WV_CATGIRL || wv == WV_NEUROFUNK ||
                   wv == WV_STREETSURFER || wv == WV_GROOVY2 || wv == WV_BASSLINE || wv == WV_DAFT || wv == WV_BLIPPY || wv == WV_REBEL || wv == WV_GORT || wv == WV_SMOOTH || wv == WV_SADNESS || wv == WV_DRUMKIT || wv == WV_SWAG || wv == WV_BERLIN) {
            isBB = true;

            // bb_step stays fixed at each formula's composed tempo (per-wave
            // exceptions below) so the pattern/rhythm timing never changes. Y
            // instead drives a granular pitch shift (bbPitchShift) applied to
            // the output: -6..+6 semitones (1 octave total), quantized to
            // whole notes, via the equal-tempered ratio 2^(semitone/12)
            // (yRaw==0.5 -> 0 semitones -> original pitch).
            float baseStep = 1.0f;
            if (wv == WV_BLIPPY)  baseStep *= 4.0f;
            if (wv == WV_TECHNO)  baseStep *= 2.0f;
            if (wv == WV_REBEL)   baseStep *= 4.0f;
            if (wv == WV_SMOOTH)  baseStep *= 4.0f;
            if (wv == WV_BERLIN)  baseStep *= 8.0f;
            if (wv == WV_DRUMKIT) baseStep *= 2.0f;
            if (wv == WV_SADNESS) baseStep *= 8.0f;
            if (wv == WV_SWAG)    baseStep *= 4.0f;
            bb_step = baseStep;

            // Hysteresis: only re-snap to a new semitone once the stick has
            // moved past the current note's half-step boundary by HYST, so
            // small ADC jitter near a boundary doesn't flicker the pitch
            // between two adjacent notes. bbSemitone persists across chunks.
            float rawSemitone = yRaw * 12.0f - 6.0f;    // -6..+6, continuous (1 octave total)
            const float HYST  = 0.15f;
            if (fabsf(rawSemitone - (float)bbSemitone) > 0.5f + HYST)
                bbSemitone = (int)roundf(rawSemitone);
            bbSemitone = constrain(bbSemitone, -6, 6);
            pitchRatio = powf(2.0f, (float)bbSemitone / 12.0f);

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
                case WV_DAFT: {
                    // "daft"
                    // t>>4+!(-t>>13&7)+2*!(t>>17)
                    //   | t*t*(t>>((t>>12^t>>11)%3+10))/(7+(t>>10&t>>14&3))*!(t&512)
                    //     << 3+(t>>14&1)
                    // All-integer except the t*t*X/Y*Z product, which needs
                    // double + jsToInt32 (matches JS ToInt32-on-a-fraction for "<<").
                    int32_t t  = (int32_t)s_bb_t;
                    double  td = (double)s_bb_t;

                    int32_t notA  = ((((-t) >> 13) & 7) == 0) ? 1 : 0;
                    int32_t notB  = ((t >> 17) == 0) ? 1 : 0;
                    int32_t shiftL = (4 + notA + 2 * notB) & 31;
                    int32_t lhs = t >> shiftL;

                    int32_t xorShift = (t >> 12) ^ (t >> 11);
                    int32_t shiftX = ((xorShift % 3) + 10) & 31;
                    int32_t X = t >> shiftX;

                    int32_t Y = 7 + (((t >> 10) & (t >> 14)) & 3);
                    int32_t Z = ((t & 512) == 0) ? 1 : 0;

                    double  bigProduct = td * td * (double)X / (double)Y * (double)Z;
                    int32_t shiftR = (3 + ((t >> 14) & 1)) & 31;
                    uint32_t rhs = (uint32_t)jsToInt32(bigProduct) << shiftR;

                    uint8_t samp8 = (uint8_t)(((uint32_t)lhs | rhs) & 255u);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_BLIPPY: {
                    // "blippy"
                    // s=[.5,.4,.3,.5,.6]
                    // 128 + sin(t*s[i]/15 + (128 + 10*sin(.1*t)*sin(1E-4*t)*s[i]))
                    //     * (20 - int(t/200)%20),  i = int(t/4E3)%5
                    // Pure floating-point sin() — no JS bitwise/array edge cases
                    // (t >= 0 always, so int(t/4E3)%5 is always in-range [0,4]).
                    static const double S[5] = { 0.5, 0.4, 0.3, 0.5, 0.6 };
                    double td   = (double)s_bb_t;
                    double sVal = S[(s_bb_t / 4000) % 5];

                    double inner = 128.0 + 10.0 * sin(0.1 * td) * sin(1e-4 * td) * sVal;
                    double arg   = td * sVal / 15.0 + inner;
                    double ampFactor = 20.0 - (double)((s_bb_t / 200) % 20);

                    double result = 128.0 + sin(arg) * ampFactor;
                    uint8_t samp8 = (uint8_t)((int32_t)result & 255);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_REBEL: {
                    // "rebel"
                    // t>>=2,
                    //   ((4E4/(t&2047)*"11000100"[7&t>>11]&128)
                    //  + (t*sin(t>>1)*"0000100101001001"[15&t>>10]&-t>>3&127)
                    //  + random()*(1&-t>>10)*50
                    //  + (t%((t&-16|t>>11)&42)<<2&-t>>4)) / 1.5
                    static const char STR1[] = "11000100";
                    static const char STR2[] = "0000100101001001";

                    int32_t t = (int32_t)(s_bb_t >> 2);

                    // Term 1: (40000/(t&2047)) * charVal1, & 128
                    //   t&2047==0 -> div-by-zero -> Infinity (or NaN if charVal1==0)
                    //   -> jsToInt32 -> 0, same as JS.
                    int32_t Tand2047 = t & 2047;
                    double  charVal1 = (STR1[7 & (t >> 11)] == '1') ? 1.0 : 0.0;
                    int32_t term1 = jsToInt32((40000.0 / (double)Tand2047) * charVal1) & 128;

                    // Term 2: t*sin(t>>1)*charVal2, & (-t>>3) & 127
                    double  charVal2 = (STR2[15 & (t >> 10)] == '1') ? 1.0 : 0.0;
                    double  Aval = (double)t * sin((double)(t >> 1)) * charVal2;
                    int32_t term2 = jsToInt32(Aval) & ((-t) >> 3) & 127;

                    // Term 3: random() * (1 & (-t>>10)) * 50
                    double term3 = bbRandom() * (double)(1 & ((-t) >> 10)) * 50.0;

                    // Term 4: (t % D) << 2 & (-t>>4),  D = ((t&-16)|(t>>11)) & 42
                    //   D==0 -> t%0 -> NaN -> jsToInt32 -> 0, same as JS.
                    int32_t Dval   = ((t & -16) | (t >> 11)) & 42;
                    int32_t modVal = (Dval == 0) ? 0 : (t % Dval);
                    int32_t term4  = (modVal << 2) & ((-t) >> 4);

                    double sum = (double)term1 + (double)term2 + term3 + (double)term4;
                    uint8_t samp8 = (uint8_t)(jsToInt32(sum / 1.5) & 255);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_GORT: {
                    // "gort dance"
                    // ((4E4/(t&4095)&128)
                    //  + (t*sin(t>>2)*(1&t>>12)&-t>>5&127)
                    //  + (t*[1,1.2,1.35,1.5][3&t>>13]*(t&2048?2:1)>>1&127)) / 1.5
                    int32_t t = (int32_t)s_bb_t;

                    // Term 1: (40000/(t&4095)) & 128
                    //   t&4095==0 -> div-by-zero -> Infinity -> jsToInt32 -> 0, same as JS.
                    int32_t Tand4095 = t & 4095;
                    int32_t term1 = jsToInt32(40000.0 / (double)Tand4095) & 128;

                    // Term 2: t*sin(t>>2)*(1&t>>12) & (-t>>5) & 127
                    double  Aval = (double)t * sin((double)(t >> 2)) * (double)(1 & (t >> 12));
                    int32_t term2 = jsToInt32(Aval) & ((-t) >> 5) & 127;

                    // Term 3: (t * [1,1.2,1.35,1.5][3&t>>13] * (t&2048?2:1) >> 1) & 127
                    static const double ARR[4] = { 1.0, 1.2, 1.35, 1.5 };
                    double arrVal     = ARR[3 & (t >> 13)];
                    double ternaryVal = (t & 2048) ? 2.0 : 1.0;
                    double Cval = (double)t * arrVal * ternaryVal;
                    int32_t term3 = (jsToInt32(Cval) >> 1) & 127;

                    double sum = (double)term1 + (double)term2 + (double)term3;
                    uint8_t samp8 = (uint8_t)(jsToInt32(sum / 1.5) & 255);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_SMOOTH: {
                    // "smooth thing"
                    // ASDFDSA = t*ARR1[(t>>13)&7]*ARR2[(t>>17)&3]/4 & 63
                    // EDCFCDE = t*ARR3[(t>>17)&3]/4
                    // EDCFCDE = (EDCFCDE%75/2 + EDCFCDE%75.1/2 + EDCFCDE%56
                    //            + EDCFCDE%44.5 + EDCFCDE%37.5) / 1.5
                    // ASDFDSA + EDCFCDE
                    static const double ARR1[8] = { 1.7, 2.3, 1.7, 2.3, 2.9, 3.4, 2.9, 3.4 };
                    static const double ARR2[4] = { 1.0, 0.884, 1.317, 1.0 };
                    static const double ARR3[4] = { 1.0, 0.881, 1.317, 1.0 };

                    int32_t t    = (int32_t)s_bb_t;
                    int     idx1 = (t >> 13) & 7;
                    int     idx2 = (t >> 17) & 3;

                    int32_t asdfdsa = jsToInt32((double)t * ARR1[idx1] * ARR2[idx2] / 4.0) & 63;

                    double edcfcde = (double)t * ARR3[idx2] / 4.0;
                    edcfcde = (fmod(edcfcde, 75.0)  / 2.0
                             + fmod(edcfcde, 75.1)  / 2.0
                             + fmod(edcfcde, 56.0)
                             + fmod(edcfcde, 44.5)
                             + fmod(edcfcde, 37.5)) / 1.5;

                    double sum = (double)asdfdsa + edcfcde;
                    uint8_t samp8 = (uint8_t)(jsToInt32(sum) & 255);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_SADNESS: {
                    // "sadness"
                    // r=24576
                    // a=t=>abs((.875*t*2**(ARR[(t>>14)%32]/12)&255)-128)&255
                    // (a(t)-.5&224)+(a(t-r)-.5&224)/2
                    //
                    // When (t>>14)%32 is negative the array index is out of
                    // range -> undefined -> ARR[idx]/12 = NaN -> 2**NaN = NaN
                    // -> .875*t*NaN = NaN -> jsToInt32(NaN)&255 == 0, so the
                    // inner term collapses to abs(0-128)&255 == 128.
                    static const double ARR[32] = {
                        1,5,12,1,5,12,10,8,0,4,10,0,4,10,13,12,
                        5,8,15,5,8,15,13,12,3,7,13,3,13,12,10,12
                    };
                    const int32_t r = 24576;

                    auto aFn = [&](int32_t tArg) -> double {
                        int32_t idx = (tArg >> 14) % 32;
                        double  inner;
                        if (idx < 0) {
                            inner = 0.0;
                        } else {
                            double P = pow(2.0, ARR[idx] / 12.0);
                            double M = 0.875 * (double)tArg * P;
                            inner = (double)(jsToInt32(M) & 255);
                        }
                        return (double)(jsToInt32(fabs(inner - 128.0)) & 255);
                    };

                    int32_t t = (int32_t)s_bb_t;
                    double  result = (double)(jsToInt32(aFn(t) - 0.5) & 224)
                                    + (double)(jsToInt32(aFn(t - r) - 0.5) & 224) / 2.0;

                    uint8_t samp8 = (uint8_t)(jsToInt32(result) & 255);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_DRUMKIT: {
                    // "drumkit"
                    // 6*((1152%(t&2047)&99)*(1090584833>>(t>>11)&1)/3
                    //   +(t%25^t%214&28)*(1440044373>>(t>>11)&1)/8
                    //   +((t%81^t%104)&64)*(2416971792>>(t>>11)&1)
                    //  )/(1+(t>>7&15))
                    //
                    // t>=0 always. 1152%(t&2047) is NaN when (t&2047)==0
                    // (mod-by-zero in JS) -> NaN&99==0, special-cased to
                    // avoid a C++ mod-by-zero trap. Shift counts are masked
                    // to 5 bits (&31) to match JS's ToUint32(shift)&0x1F and
                    // avoid UB on >>32. 2416971792 exceeds int32 range, so
                    // its jsToInt32-wrapped value (-1877995504) is used
                    // directly for the (sign-extending) arithmetic shift.
                    int32_t t = (int32_t)s_bb_t;

                    int32_t shiftAmt = (t >> 11) & 31;

                    int32_t maskedT = t & 2047;
                    int32_t piece1  = (maskedT == 0) ? 0 : ((1152 % maskedT) & 99);
                    int32_t piece2  = (1090584833 >> shiftAmt) & 1;

                    int32_t pieceB1 = (t % 25) ^ ((t % 214) & 28);
                    int32_t pieceB2 = (1440044373 >> shiftAmt) & 1;

                    int32_t pieceC1 = ((t % 81) ^ (t % 104)) & 64;
                    const int32_t CONST3 = -1877995504;  // jsToInt32(2416971792)
                    int32_t pieceC2 = (CONST3 >> shiftAmt) & 1;

                    double A = (double)piece1  * (double)piece2  / 3.0;
                    double B = (double)pieceB1 * (double)pieceB2 / 8.0;
                    double C = (double)pieceC1 * (double)pieceC2;

                    double denom  = 1.0 + (double)((t >> 7) & 15);
                    double result = 6.0 * (A + B + C) / denom;

                    uint8_t samp8 = (uint8_t)(jsToInt32(result) & 255);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_SWAG: {
                    // "swag"
                    // T=t*1.2
                    // sin(8/(T/16384%1+.05))*32
                    // + (B[(T>>12)%16]*t*.63&127)/2
                    // + (t*tan([t>>3,t>>1][T>>15&1])&128)/2*(1-T/16384%1)*(T>>14&1)
                    // + 32
                    // + ((t*.63*E[T>>16&3]&255)+(T>>8)&256)/4
                    //
                    // t is the global counter (always >=0), so T>=0 always —
                    // no out-of-range array indices to special-case here.
                    static const double ARR_B[16] = {1,0,2,0,1.2,0,2.4,0,1.35,0,2.7,1.5,0,3,0,1.5};
                    static const double ARR_E[4]  = {1.0, 0.9, 0.8, 0.75};

                    int32_t t    = (int32_t)s_bb_t;
                    double  T    = (double)t * 1.2;
                    int32_t Tint = jsToInt32(T);
                    double  frac = fmod(T / 16384.0, 1.0);

                    double termA = sin(8.0 / (frac + 0.05)) * 32.0;

                    int32_t idxB  = (Tint >> 12) & 15;   // %16, Tint>=0
                    double  valB  = ARR_B[idxB] * (double)t * 0.63;
                    double  termB = (double)(jsToInt32(valB) & 127) / 2.0;

                    int32_t selIdx   = (Tint >> 15) & 1;
                    int32_t shiftVal = selIdx ? (t >> 1) : (t >> 3);
                    double  bitC     = (double)(jsToInt32((double)t * tan((double)shiftVal)) & 128) / 2.0;
                    double  termC    = bitC * (1.0 - frac) * (double)((Tint >> 14) & 1);

                    int32_t idxE = (Tint >> 16) & 3;
                    int32_t X    = jsToInt32((double)t * 0.63 * ARR_E[idxE]) & 255;
                    int32_t Y    = Tint >> 8;
                    double  termE = (double)((X + Y) & 256) / 4.0;

                    double result = termA + termB + termC + 32.0 + termE;

                    uint8_t samp8 = (uint8_t)(jsToInt32(result) & 255);
                    bbAdvance(bb_step);
                    s = (samp8 - 128) * (1.0f / 128.0f);
                    break;
                }
                case WV_BERLIN: {
                    // "berlin dance"
                    // x=t=>(1.22*t*1.0596**parseInt(ROWS[1&t>>17][3&t>>13],36)&128)
                    //      *((-t>>8)&31)/16
                    // (x(t)/4 + x(t-(t>>9&255))/4 + x(t-12288)/8 + x(t-24888)/8
                    //  + random()*((-t>>7)&63) + 32*sin(3*sqrt(t%16384))) / 3 + 64
                    //
                    // ROWS = ['H0CA','H1DC']; col = 3&(t>>13) is always 0..3,
                    // so (unlike the other lookup formulas) there's no
                    // out-of-range/undefined case to handle here.
                    //
                    // 1.0596**digit only takes 8 distinct (row,col) values, so
                    // precompute them once instead of calling pow() 4x/sample.
                    static double POW_RC[2][4];
                    static bool   powReady = false;
                    if (!powReady) {
                        static const int DIGIT[2][4] = {
                            {17, 0, 12, 10},  // "H0CA"
                            {17, 1, 13, 12}   // "H1DC"
                        };
                        for (int row = 0; row < 2; row++)
                            for (int col = 0; col < 4; col++)
                                POW_RC[row][col] = pow(1.0596, (double)DIGIT[row][col]);
                        powReady = true;
                    }

                    auto xFn = [&](int32_t tArg) -> double {
                        int32_t row = (tArg >> 17) & 1;
                        int32_t col = (tArg >> 13) & 3;
                        double  M    = 1.22 * (double)tArg * POW_RC[row][col];
                        int32_t bit1 = jsToInt32(M) & 128;
                        int32_t negT = -tArg;
                        int32_t bit2 = (negT >> 8) & 31;
                        return (double)bit1 * (double)bit2 / 16.0;
                    };

                    int32_t t = (int32_t)s_bb_t;

                    int32_t off1     = (t >> 9) & 255;
                    int32_t negTOut  = -t;
                    int32_t noiseAmt = (negTOut >> 7) & 63;

                    double result = (xFn(t) / 4.0
                                    + xFn(t - off1) / 4.0
                                    + xFn(t - 12288) / 8.0
                                    + xFn(t - 24888) / 8.0
                                    + bbRandom() * (double)noiseAmt
                                    + 32.0 * sin(3.0 * sqrt((double)(t % 16384)))
                                    ) / 3.0 + 64.0;

                    uint8_t samp8 = (uint8_t)(jsToInt32(result) & 255);
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
            if (isBB) {
                s_bbLpf += (s - s_bbLpf) * BB_LPF_ALPHA;
                s = s_bbLpf;
            }
            buf[i] = (int16_t)(s * amp);
            if (isBB) buf[i] = bbPitchShift(buf[i], pitchRatio);
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
            if (wv == WV_BLUEBERRY || wv == WV_TECHNO || wv == WV_RHYTHM || wv == WV_DOOM || wv == WV_EASYBEAT || wv == WV_CATGIRL || wv == WV_NEUROFUNK || wv == WV_STREETSURFER || wv == WV_GROOVY2 || wv == WV_BASSLINE || wv == WV_DAFT || wv == WV_BLIPPY || wv == WV_REBEL || wv == WV_GORT || wv == WV_SMOOTH || wv == WV_SADNESS || wv == WV_DRUMKIT || wv == WV_SWAG || wv == WV_BERLIN) {
                tft.fillRect(0, BOT_STA - 2, TFT_W, TFT_H - (BOT_STA - 2), C_BG);
                tft.setTextColor(tft.color565(120, 140, 160)); tft.setTextSize(1);
                char tmp[48];
                if (loopAll) {
                    unsigned long elapsed = millis() - loopAllStartMs;
                    if (elapsed > loopAllMs) elapsed = loopAllMs;
                    int remainSec = (int)((loopAllMs - elapsed) / 1000UL);
                    snprintf(tmp, sizeof(tmp), "Loop All %d/%d  %ds left | click:exit", loopAllRel + 1, WV_BB_COUNT, remainSec);
                } else {
                    snprintf(tmp, sizeof(tmp), "%+d semi (x%.2f) | Y:pitch X:vol tap:next hold:list", bbSemitone, pitchRatio);
                }
                tft.setCursor(4, BOT_STA);
                tft.print(tmp);
                drawBBNote(bbSemitone);
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

        // ── Loop All: advance to next bytebeat after the loop duration ────────
        if (loopAll && (millis() - loopAllStartMs) >= loopAllMs) {
            loopAllRel = (loopAllRel + 1) % WV_BB_COUNT;
            wv = WV_BB_FIRST + loopAllRel;
            s_bb_t = 0; s_bb_frac = 0.0f; s_bbLpf = 0.0f;
            loopAllStartMs = millis();
            drawWaveLabFrame(wv);
        }

        // ── Button: tap = cycle wave, hold/double-click = back to picker ──────
        bool btnNow = isJoystickButtonPressed();
        if (loopAll) {
            if (btnNow) {
                loopAll    = false;
                needPicker = true;
            }
        } else if (btnNow && btnWasUp) {
            btnDownMs = millis();
            btnWasUp  = false;
        } else if (!btnNow && !btnWasUp) {
            unsigned long now = millis();
            if (now - btnDownMs >= 500) {
                needPicker = true;
            } else if (now - lastTapMs <= DOUBLE_CLICK_MS) {
                needPicker = true;   // double click = back to picker
            } else {
                wv = nextWaveInCategory(wv);
                phase = 0.0f; phaseMod = 0.0f;
                s_ks_len = 0; ksPluckTimer = 0;
                s_bb_t = 0; s_bb_frac = 0.0f; s_bbLpf = 0.0f;
                if (wv == WV_KICK)  triggerKick(freq);
                if (wv == WV_SNARE) triggerSnare();
                if (wv == WV_HAT)   triggerHat();
                drawWaveLabFrame(wv);
                lastTapMs = now;
            }
            btnWasUp = true;
        }
    }

    stopTxAndFlush();
}
