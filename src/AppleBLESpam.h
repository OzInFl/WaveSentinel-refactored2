// src/AppleBLESpam.h
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

/**
 * AppleBLESpam
 *
 * Crafts and sends BLE Advertising packets that mimic Apple Continuity
 * “Proximity Pairing” frames.  Call begin() in setup(), then start() to
 * kick off the spam task.
 */
class AppleBLESpam {
public:
  /**
   * @param fakeMac 6-byte “fake” BLE address to advertise as the Apple device.
   * @param intervalMs Advertisement interval, in milliseconds.
   */
  AppleBLESpam(const uint8_t fakeMac[6], uint16_t intervalMs = 100);

  /// Initialize BLE stack.  Call in setup().
  void begin();

  /// Start the background spam task.
  void start();

  /// Stop spamming.
  void stop();

private:
  NimBLEAdvertising* _pAdv;
  uint8_t            _fakeMac[6];
  uint16_t           _intervalMs;
  TaskHandle_t       _taskHandle;

  /// Build one ADV payload and hand it to the controller.
  void buildAndSend();

  /// FreeRTOS task entry point
  static void spamTask(void* param);
};
