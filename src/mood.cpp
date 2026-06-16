#include "globals.h"

const char *MOOD_NAMES[] = { "None", "Exciting", "Happy", "Romantica", "Sad", "Powerful" };
const char *MOOD_PATHS[] = { nullptr, "/exciting.wav", "/happy.wav", "/romantica.wav", "/sad.wav", "/powerful.wav" };

// Validates the mood WAV and caches its PCM data offset/size for streaming.
bool loadMoodTrack(int mood)
{
  g_mood_data_start = 0;
  g_mood_data_size  = 0;
  if (mood == 0) return true;
  if (mood < 1 || mood >= MOOD_COUNT) { g_mood = 0; return false; }

  const char *path = MOOD_PATHS[mood];
  File f = LittleFS.open(path, "r");
  if (!f) { Serial.printf("Mood: cannot open %s\n", path); return false; }

  uint8_t riff[12];
  if (f.read(riff, 12) != 12 || riff[0] != 'R' || riff[1] != 'I') { f.close(); return false; }

  bool foundData = false;
  uint8_t chunkHdr[8];
  while (f.read(chunkHdr, 8) == 8)
  {
    uint32_t sz = (uint32_t)chunkHdr[4] | ((uint32_t)chunkHdr[5] << 8) |
                  ((uint32_t)chunkHdr[6] << 16) | ((uint32_t)chunkHdr[7] << 24);
    if (chunkHdr[0] == 'f' && chunkHdr[1] == 'm' && chunkHdr[2] == 't') {
      uint8_t fmt[16]; f.read(fmt, 16);
      if (sz > 16) f.seek(sz - 16, SeekCur);
    } else if (chunkHdr[0] == 'd' && chunkHdr[1] == 'a' && chunkHdr[2] == 't' && chunkHdr[3] == 'a') {
      g_mood_data_start = (uint32_t)f.position();
      uint32_t avail    = (uint32_t)f.size() - g_mood_data_start;
      g_mood_data_size  = (sz < avail) ? sz : avail;
      foundData = true;
      break;
    } else {
      f.seek(sz, SeekCur);
    }
  }
  f.close();

  if (!foundData || g_mood_data_size == 0) { Serial.printf("Mood: no data chunk in %s\n", path); return false; }
  Serial.printf("Mood: validated %s — %.1fs PCM\n", path, (float)(g_mood_data_size / 2) / SAMPLE_RATE);
  return true;
}

// Opens the mood WAV and seeks to the PCM data for streaming.
void openMoodPlayback()
{
  if (g_mood == 0 || g_mood_data_size == 0) return;
  if (g_mood_file) g_mood_file.close();
  g_mood_byte_pos = 0;
  g_mood_file = LittleFS.open(MOOD_PATHS[g_mood], "r");
  if (!g_mood_file) { Serial.println("Mood: open failed"); return; }
  g_mood_file.seek(g_mood_data_start);
}

// Closes the mood file after playback ends.
void closeMoodPlayback()
{
  if (g_mood_file) g_mood_file.close();
}

// Streams n samples from the mood file into buf[], looping at track end.
void mixMoodInto(int16_t *buf, int n)
{
  if (!g_mood_file || g_mood_data_size == 0) return;

  int16_t mbuf[256];
  int done = 0;

  while (done < n) {
    uint32_t left = g_mood_data_size - g_mood_byte_pos;
    if (left == 0) {
      g_mood_file.seek(g_mood_data_start);
      g_mood_byte_pos = 0;
      left = g_mood_data_size;
    }
    uint32_t want = (uint32_t)((n - done) * (int)sizeof(int16_t));
    if (want > left) want = left & ~1u;
    if (want == 0) want = 2;
    size_t got = g_mood_file.read((uint8_t *)(mbuf + done), (size_t)want);
    if (got == 0) break;
    g_mood_byte_pos += (uint32_t)got;
    done += (int)(got / sizeof(int16_t));
  }

  for (int i = 0; i < done; ++i) {
    int32_t m = (int32_t)((float)mbuf[i] * g_mood_gain);
    int32_t s = (int32_t)buf[i] + m;
    if (s > INT16_MAX) s = INT16_MAX;
    if (s < INT16_MIN) s = INT16_MIN;
    buf[i] = (int16_t)s;
  }
}
