#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace lampsmart {

class BleAdvertiser {
public:
  bool begin();
  bool isReady() const { return ready_; }
  const String& lastError() const { return lastError_; }

  /// Advertise a single packet synchronously for `windowMs` (or until preempted).
  bool advertiseFrame(const uint8_t* data, size_t len, String& errOut);
  bool stopAdvertising();

  /**
   * Executes a complete LampSmart advertising burst:
   * - `repeats` windows of `windowMs` each (matching xLamp's 8 x 500ms default)
   * - Optional `[0xbbaa, 0x3412]` end-marker window
   * - Calls `onFirstStarted` as soon as the BLE GAP stack confirms the first frame started
   * - Checks `shouldPreempt()` periodically after a minimum 80ms floor so newer slider updates
   *   can interrupt long bursts without stalling.
   */
  bool transmitSequence(
      const uint8_t* frame,
      size_t len,
      uint8_t repeats,
      uint16_t windowMs,
      bool sendEndMarker,
      uint8_t adFlag,
      const std::function<bool()>& shouldPreempt,
      const std::function<void(bool ok, const String& err)>& onFirstStarted,
      String& errOut,
      bool& wasPreempted);

private:
  bool ready_ = false;
  bool advertising_ = false;
  String lastError_;
};

} // namespace lampsmart

