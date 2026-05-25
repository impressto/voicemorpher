#pragma once

// Default playback volume multiplier for MAX98357A output.
// Increase this value to make playback louder, but avoid values that clip badly.
#ifndef DEFAULT_PLAYBACK_GAIN
#define DEFAULT_PLAYBACK_GAIN 4.0f
#endif

// Enable a light post-recording smoothing filter to reduce microphone crackle.
#ifndef ENABLE_AUDIO_CLEANING
#define ENABLE_AUDIO_CLEANING 1
#endif

// Filter strength: 1 = light smoothing, 2 = stronger smoothing.
#ifndef AUDIO_CLEANING_STRENGTH
#define AUDIO_CLEANING_STRENGTH 1
#endif
