#pragma once

struct RadioStation {
  const char *name;
  const char *url;
};

// Plain-HTTP MP3 streams only (ESP32-audioI2S v3.0.0 + HTTPS is more
// failure-prone). Lower-bitrate stations first - on a moderate WiFi
// signal (~-68dBm), 96-128kbps streams played with no "slow stream"
// warnings, while a 192kbps stream produced constant warnings and
// audible choppiness. Higher-bitrate stations are kept at the end for
// users with a stronger signal.
static const RadioStation STATIONS[] = {
  {"NPR News",          "http://npr-ice.streamguys1.com/live.mp3"},            // 96kbps
  {"Los 40 Catalunya",  "http://25543.live.streamtheworld.com:80/LOS40_CAT_SC"},// 96kbps, Spanish Top 40
  {"Rock FM",           "http://flucast31-h-cloud.flumotion.com/cope/rockfm-low.mp3"}, // 96kbps, Spanish rock
  {"Hit Radio",         "http://stm1.emiteonline.com:8004/hitelgrado"},        // 96kbps, Spanish pop/various
  {"Classical Relax",   "http://relax.stream.publicradio.org/relax.mp3"},       // 128kbps
  {"KEXP Seattle",      "http://live-mp3-128.kexp.org/kexp128.mp3"},            // 128kbps
  {"Ambient",           "http://phoebe.streamerr.co:4140/ambient.mp3"},         // ambient/chillout
  {"UK 1940s Radio",    "http://solid2.streamupsolutions.com:24929/stream"},          // 128kbps, 1940s era
  {"1940s Radio",       "http://uk3.internet-radio.com:8082/live"},                    // 128kbps, 1940s era
  {"Jazz Radio Blues",  "http://jazzblues.ice.infomaniak.ch/jazzblues-high.mp3"}, // 128kbps
  {"181 True Blues",   "http://listen.181fm.com/181-blues_128k.mp3"},             // 128kbps
  {"WWOZ New Orleans", "http://wwoz-sc.streamguys.com/wwoz-hi.mp3"},              // 128kbps, legendary
  {"Allzic Blues",     "http://allzic09.ice.infomaniak.ch/allzic09.mp3"},         // 128kbps
  {"Old School Hip-Hop","http://listen.181fm.com/181-oldschool_128k.mp3"},      // 128kbps
  {"NPO Radio 1",       "http://icecast.omroep.nl/radio1-bb-mp3"},              // higher bitrate
  {"Chocolate FM",      "http://streaming5.elitecomunicacion.es:8082/live.mp3"},// 192kbps, may be choppy on weak WiFi
};

#define STATION_COUNT (sizeof(STATIONS) / sizeof(STATIONS[0]))

// Parallel array of station names, for use with showIconList().
static const char *STATION_NAMES[] = {
  "NPR News", "Los 40 Catalunya", "Rock FM", "Hit Radio", "Classical Relax",
  "KEXP Seattle", "Ambient", "UK 1940s Radio", "1940s Radio",
  "Jazz Radio Blues", "181 True Blues", "WWOZ New Orleans", "Allzic Blues",
  "Old School Hip-Hop", "NPO Radio 1", "Chocolate FM",
};
