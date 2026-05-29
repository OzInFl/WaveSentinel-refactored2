#ifndef SubGhzTypes_h
#define SubGhzTypes_h

#include <stdint.h>

enum CC1101Preset : uint8_t
{
  AM650 = 0,
  AM270 = 1,
  FM238 = 2,
  FM476 = 3,
  CUSTOM = 4,

  // Flipper-aligned aliases for the four canonical presets. These map onto
  // the existing AM/FM names above so legacy callers (setPreset(AM650), etc.)
  // keep working while new code (Read RAW capture) can use the Flipper-style
  // names that match the dropdown labels in the WaveKaiju frontend.
  CC1101_PRESET_OOK_270 = AM270,  // AM, narrow (~270 kHz), most fixed-code remotes
  CC1101_PRESET_OOK_650 = AM650,  // AM, wide   (~650 kHz), default for most remotes
  CC1101_PRESET_FSK_238 = FM238,  // FM, narrow (~238 kHz), Tesla EU 433.92, some industrial
  CC1101_PRESET_FSK_476 = FM476,  // FM, wide   (~476 kHz), high-bandwidth FSK
  CC1101_PRESET_COUNT   = 4
};

#endif