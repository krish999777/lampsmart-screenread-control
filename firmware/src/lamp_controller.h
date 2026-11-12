#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <deque>
#include <memory>

#include "ble_advertiser.h"
#include "device_config.h"
#include "protocols/lamp_protocol.h"

namespace lampsmart {

enum class EnqueueOutcome : uint8_t {
  OK = 0,
  UNSUPPORTED_BY_PROTOCOL,
  BLE_UNAVAILABLE,
  QUEUE_TIMEOUT,
  BLE_TX_FAILED,
};

struct CommandResult {
  EnqueueOutcome outcome = EnqueueOutcome::OK;
  String status = "transmitted";
  String message = "";
  String frameHex = "";
  uint8_t txCount = 0;
};

class LampController {
public:
  bool begin();

  bool isBleReady() const { return advertiser_.isReady(); }
  String bleError() const { return advertiser_.lastError(); }

  DeviceSettings getSettings();
  LampState getState();
  TransmissionStatus getTransmissionStatus();
  const LampProtocol* getActiveProtocol();

  bool updateSettings(const DeviceSettings& updated, String& errOut);
  bool setPairedFlag(bool paired);

  /// Enqueues a command, updates local requested state, coalesces intermediate
  /// slider updates, and waits for the BLE stack to confirm advertising start.
  CommandResult executeCommand(
      const LampCommand& cmd,
      const String& label,
      bool autoPowerOnIfOff = false);

private:
  struct CommandSync {
    SemaphoreHandle_t doneSem = nullptr;
    bool ok = false;
    String err;
    String frameHex;
    uint8_t txCount = 0;

    CommandSync() : doneSem(xSemaphoreCreateBinary()) {}
    ~CommandSync() {
      if (doneSem != nullptr) vSemaphoreDelete(doneSem);
    }
  };

  struct PendingItem {
    LampCommand cmd;
    String label;
    bool autoPowerOnIfOff = false;
    bool coalescible = false;
    std::shared_ptr<CommandSync> sync;
  };

  static void workerTaskTrampoline(void* arg);
  void workerLoop();
  bool encodeAndTransmit(
      const LampCommand& cmd,
      const String& label,
      const std::function<void(bool ok, const String& err)>& onStarted,
      EncodedPacket& pktOut,
      String& errOut,
      bool& preempted);

  ConfigStore store_;
  BleAdvertiser advertiser_;
  DeviceSettings settings_;
  LampState state_;
  TransmissionStatus txStatus_;

  SemaphoreHandle_t mutex_ = nullptr;
  SemaphoreHandle_t queueWakeSem_ = nullptr;
  TaskHandle_t workerTask_ = nullptr;

  std::deque<PendingItem> queue_;
  volatile bool preemptRequested_ = false;
  uint32_t lastNvsTxSaveMs_ = 0;
};

} // namespace lampsmart
