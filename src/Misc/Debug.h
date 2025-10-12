#ifndef DEBUG_H
#define DEBUG_H

#include "Arduino.h"

#define ENABLE_SERIAL true
#define ENABLE_DEBUG true

#define Print_Debug(args...)                             \
  {                                                      \
    if (ENABLE_SERIAL && ENABLE_DEBUG)                   \
    {                                                    \
      printf("[DEBUG] - ");                              \
      printf(args);                                      \
      printf(" | Core: ");                               \
      printf(String(xPortGetCoreID()).c_str());          \
      printf(" | Current RAM: ");                        \
      printf(String(ESP.getFreeHeap() / 1024).c_str());  \
      printf("KB\n");                                    \
    }                                                    \
  }

#define Printn_Debug(args...)                             \
  {                                                      \
    if (ENABLE_SERIAL && ENABLE_DEBUG)                   \
    {                                                    \
      printf(args);                                      \
    }                                                    \
  }

#define Print_Serial(args...) \
  {                           \
    if (ENABLE_SERIAL)        \
    {                         \
      printf(args);           \
      printf("\n");           \
    }                         \
  }

#endif
