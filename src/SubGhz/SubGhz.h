#pragma once

#ifndef SubGhz_h
#define SubGhz_h

#include <Arduino.h>

#include "./Misc/Config.h"

#include <lvgl.h>
#include <ui.h>

#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <vector>
#include <ctime>
#include <sstream>
#include <array>      // For std::array
#include <SD.h>
//#include "SD/SDCard.h"
#include "SubGhzTypes.h"
#include "FlipperSubFile.h"

class SubGhz
{

private:
  static const char *bin2tristate(const char *bin);
  static char *dec2binWzerofill(unsigned long Dec, unsigned int bitLength);
  void send_byte(uint8_t dataByte);
  
  void generateRandomString(char* buf, size_t bufSize, int length);
  void generateFilename(char* buf, size_t bufSize, float frequency, int modulation, float bandwidth);

public:
  bool init();

  void setPreset(CC1101Preset preset);
  void setPacketFormat(int packetFormat);
  void setModulation(int modulation);

  void setFrequency(float freq);
  float getFrequency();

  void setRxBandwidth(float bw);
  void setDeviation(float dev);
  void setDataRate(float drate);
  void setPower(int pa);
  void setSyncMode(int mode);

  void enableRCSwitch();
  void disableRCSwitch();

  void enableReceiver();
  void disableReceiver();

  void enableTransmit();
  void disableTransmit();

  void enableScanner(float start, float stop);
  void disableScanner();

  // Type A (10-pole DIP)
  void switchOn(const char *sGroup, const char *sDevice);
  void switchOff(const char *sGroup, const char *sDevice);
  // Type B (Rotary): address 1-4, channel 1-4
  void switchOnB(int nAddress, int nChannel);
  void switchOffB(int nAddress, int nChannel);
  // Type C (Intertechno): family a-f, group 1-4, device 1-4
  void switchOnC(char sFamily, int nGroup, int nDevice);
  void switchOffC(char sFamily, int nGroup, int nDevice);
  // Type D (REV): group A-D, device 1-3
  void switchOnD(char sGroup, int nDevice);
  void switchOffD(char sGroup, int nDevice);
  // Raw code send
  void sendRaw(unsigned long code, unsigned int bitLength, int protocol, int pulseLength, int repeatCount);

  void sendLastSignal();
  bool send_tesla(float freqMhz);
  void sendSamples(int samples[], int samplesLength);
  bool sendCapture();

  void resetProtAnalyzer();
  void showResultProtAnalyzer();
  void showResultRecPlay();

  bool CaptureLoop();
  bool CaptureLoopSD();
  bool saveCaptureToSD();
  bool saveCaptureToSD(const char* customFilename);
  void getDefaultFilename(char* buf, size_t bufSize);
  bool ProtAnalyzerLoop();
  void ScannerLoop();
  void GeneratorLoop();
};

#endif
