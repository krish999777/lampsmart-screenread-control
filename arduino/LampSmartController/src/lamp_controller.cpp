#include "lamp_controller.h"

#include <cstdio>

namespace lampsmart {

namespace {
constexpr const char* kTag = "LampController";

String formatFrameHex(const uint8_t* data, size_t len) {
  String hex;
  hex.reserve(len * 2);
  char buf[3];
  for (size_t i = 0; i < len; ++i) {
    std::snprintf(buf, sizeof(buf), "%02X", data[i]);
    hex += buf;
  }
  return hex;
}

bool isCoalescible(LampCommandType t) {
  return t == LampCommandType::DIMMING ||
         t == LampCommandType::RGB ||
         t == LampCommandType::SCENE_SWITCH;
}
} // namespace

bool LampController::begin() {
  mutex_ = xSemaphoreCreateMutex();
  queueWakeSem_ = xSemaphoreCreateBinary();
  if (!mutex_ || !queueWakeSem_) {
    ESP_LOGE(kTag, "Failed to create synchronization primitives");
    return false;
  }

  store_.load(settings_, state_);
  txStatus_.protocol = settings_.protocol;

  if (!advertiser_.begin()) {
    txStatus_.success = false;
    txStatus_.status = "failed";
    txStatus_.message = advertiser_.lastError();
    ESP_LOGE(kTag, "BLE init failed: %s", advertiser_.lastError().c_str());
  }

  xTaskCreate(workerTaskTrampoline, "lamp_ble_tx", 6144, this, 3, &workerTask_);
  return true;
}

void LampController::workerTaskTrampoline(void* arg) {
  static_cast<LampController*>(arg)->workerLoop();
}

DeviceSettings LampController::getSettings() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  DeviceSettings copy = settings_;
  xSemaphoreGive(mutex_);
  return copy;
}

LampState LampController::getState() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  LampState copy = state_;
  xSemaphoreGive(mutex_);
  return copy;
}

TransmissionStatus LampController::getTransmissionStatus() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  TransmissionStatus copy = txStatus_;
  xSemaphoreGive(mutex_);
  return copy;
}

const LampProtocol* LampController::getActiveProtocol() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const LampProtocol* proto = getProtocolById(settings_.protocol.c_str());
  xSemaphoreGive(mutex_);
  return proto;
}

bool LampController::updateSettings(const DeviceSettings& updated, String& errOut) {
  if (!isValidProtocolId(updated.protocol.c_str())) {
    errOut = "Unsupported protocol version";
    return false;
  }
  if (updated.advRepeats < 1 || updated.advRepeats > 32) {
    errOut = "advRepeats must be between 1 and 32";
    return false;
  }
  if (updated.advWindowMs < 50 || updated.advWindowMs > 2000) {
    errOut = "advWindowMs must be between 50 and 2000";
    return false;
  }
  if (updated.channel > 15) {
    errOut = "channel must be between 0 and 15";
    return false;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  settings_.lightName = updated.lightName.length() ? updated.lightName : "LampSmart Light";
  settings_.protocol = updated.protocol;
  settings_.deviceType = updated.deviceType;
  settings_.deviceId = updated.deviceId ? (updated.deviceId & 0x7FFFFFFFu) : settings_.deviceId;
  settings_.channel = updated.channel;
  settings_.deviceAddress = updated.deviceAddress;
  settings_.paired = updated.paired;
  settings_.reversedCct = updated.reversedCct;
  settings_.adFlag = updated.adFlag;
  settings_.advRepeats = updated.advRepeats;
  settings_.advWindowMs = updated.advWindowMs;
  settings_.advEndMarker = updated.advEndMarker;
  if (updated.mdnsHost.length()) {
    settings_.mdnsHost = updated.mdnsHost;
  }
  const bool saved = store_.saveSettings(settings_);
  xSemaphoreGive(mutex_);

  if (!saved) {
    errOut = "Failed to save configuration to NVS";
    return false;
  }
  return true;
}

bool LampController::setPairedFlag(bool paired) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  settings_.paired = paired;
  const bool ok = store_.saveSettings(settings_);
  xSemaphoreGive(mutex_);
  return ok;
}

CommandResult LampController::executeCommand(
    const LampCommand& cmd,
    const String& label,
    bool autoPowerOnIfOff) {
  CommandResult res;

  if (!advertiser_.isReady()) {
    res.outcome = EnqueueOutcome::BLE_UNAVAILABLE;
    res.status = "failed";
    res.message = advertiser_.lastError().length() ? advertiser_.lastError() : "BLE advertiser is not ready";
    return res;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  const LampProtocol* proto = getProtocolById(settings_.protocol.c_str());

  // Validate protocol capability without advancing txCount
  LampTargetConfig dryCfg;
  dryCfg.deviceType = settings_.deviceType;
  dryCfg.deviceId = settings_.deviceId;
  dryCfg.channel = settings_.channel;
  dryCfg.deviceAddress = settings_.deviceAddress;
  dryCfg.txCount = settings_.txCount;
  dryCfg.reversedCct = settings_.reversedCct;
  dryCfg.adFlag = settings_.adFlag;
  EncodedPacket dryPkt;
  if (!proto->encode(cmd, dryCfg, dryPkt)) {
    xSemaphoreGive(mutex_);
    res.outcome = EnqueueOutcome::UNSUPPORTED_BY_PROTOCOL;
    res.status = "unsupported";
    res.message = String("Command is not supported by protocol '") + proto->id() + "'";
    return res;
  }

  // Update requested state optimistically (one-way BLE advertising protocol)
  const bool wasOff = !state_.power;
  const uint64_t nowMs = millis();
  state_.lastCommandMs = nowMs;
  switch (cmd.type) {
    case LampCommandType::POWER:
      state_.power = cmd.on;
      break;
    case LampCommandType::DIMMING:
      if (autoPowerOnIfOff) state_.power = true;
      state_.brightness = cmd.brightness;
      state_.temperature = cmd.temperature;
      state_.colorMode = "cct";
      break;
    case LampCommandType::RGB:
      if (autoPowerOnIfOff) state_.power = true;
      state_.r = cmd.r;
      state_.g = cmd.g;
      state_.b = cmd.b;
      state_.brightness = cmd.brightness;
      state_.colorMode = "rgb";
      break;
    case LampCommandType::SCENE_ON:
      state_.power = true;
      state_.colorMode = "scene";
      break;
    case LampCommandType::SCENE_SWITCH:
      state_.power = true;
      state_.scene = static_cast<uint8_t>(cmd.sceneIndex + 1);
      state_.colorMode = "scene";
      break;
    case LampCommandType::NIGHT_LIGHT:
      state_.power = true;
      state_.colorMode = "night";
      break;
    case LampCommandType::UNPAIR:
      settings_.paired = false;
      state_.power = false;
      store_.saveSettings(settings_);
      break;
    default:
      break;
  }

  // Coalesce pending identical-type updates in the queue (latest-value-wins)
  const bool coalescible = isCoalescible(cmd.type);
  if (coalescible) {
    for (auto it = queue_.begin(); it != queue_.end();) {
      if (it->coalescible && it->cmd.type == cmd.type) {
        if (it->sync) {
          it->sync->ok = true;
          it->sync->err = "Superseded by newer value";
          if (it->sync->doneSem != nullptr) xSemaphoreGive(it->sync->doneSem);
        }
        it = queue_.erase(it);
      } else {
        ++it;
      }
    }
  }

  auto sync = std::make_shared<CommandSync>();

  PendingItem item;
  item.cmd = cmd;
  item.label = label;
  item.autoPowerOnIfOff = autoPowerOnIfOff && wasOff;
  item.coalescible = coalescible;
  item.sync = sync;

  queue_.push_back(item);
  preemptRequested_ = true;
  xSemaphoreGive(mutex_);
  xSemaphoreGive(queueWakeSem_);

  // Wait for BLE worker to accept & start GAP advertising (does not wait for all 8 repeats)
  if (sync->doneSem != nullptr) {
    const BaseType_t got = xSemaphoreTake(sync->doneSem, pdMS_TO_TICKS(1500));
    if (got != pdTRUE) {
      res.outcome = EnqueueOutcome::QUEUE_TIMEOUT;
      res.status = "failed";
      res.message = "Timed out waiting for BLE transmitter";
      return res;
    }
  }

  if (!sync->ok) {
    res.outcome = EnqueueOutcome::BLE_TX_FAILED;
    res.status = "failed";
    res.message = sync->err.length() ? sync->err : "BLE advertising start failed";
    return res;
  }

  res.outcome = EnqueueOutcome::OK;
  res.status = "transmitted";
  res.message = "BLE advertising started";
  res.frameHex = sync->frameHex;
  res.txCount = sync->txCount;
  return res;
}

bool LampController::encodeAndTransmit(
    const LampCommand& cmd,
    const String& label,
    const std::function<void(bool ok, const String& err)>& onStarted,
    EncodedPacket& pktOut,
    String& errOut,
    bool& preempted) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  DeviceSettings cfgCopy = settings_;
  const LampProtocol* proto = getProtocolById(cfgCopy.protocol.c_str());

  LampTargetConfig target;
  target.deviceType = cfgCopy.deviceType;
  target.deviceId = cfgCopy.deviceId;
  target.channel = cfgCopy.channel;
  target.deviceAddress = cfgCopy.deviceAddress;
  target.txCount = cfgCopy.txCount;
  target.reversedCct = cfgCopy.reversedCct;
  target.adFlag = cfgCopy.adFlag;

  if (!proto->encode(cmd, target, pktOut)) {
    xSemaphoreGive(mutex_);
    errOut = "Protocol failed to encode command";
    if (onStarted) onStarted(false, errOut);
    return false;
  }

  settings_.txCount = target.txCount;
  const String hex = formatFrameHex(pktOut.data, pktOut.length);
  txStatus_.status = "transmitting";
  txStatus_.command = label;
  txStatus_.protocol = proto->id();
  txStatus_.txCount = pktOut.txCount;
  txStatus_.frameHex = hex;
  txStatus_.timestampMs = millis();

  // Persist txCount periodically (at most every 10s) to protect NVS flash longevity
  const uint32_t now = millis();
  if (now - lastNvsTxSaveMs_ > 10000) {
    store_.saveTxCount(settings_.txCount);
    lastNvsTxSaveMs_ = now;
  }
  xSemaphoreGive(mutex_);

  const bool ok = advertiser_.transmitSequence(
      pktOut.data,
      pktOut.length,
      cfgCopy.advRepeats,
      cfgCopy.advWindowMs,
      cfgCopy.advEndMarker,
      cfgCopy.adFlag,
      [this]() { return preemptRequested_; },
      onStarted,
      errOut,
      preempted);

  xSemaphoreTake(mutex_, portMAX_DELAY);
  txStatus_.timestampMs = millis();
  if (ok) {
    txStatus_.success = true;
    txStatus_.status = preempted ? "transmitted_preempted" : "transmitted";
    txStatus_.message = preempted
                            ? "BLE packet transmitted (superseded by newer command)"
                            : "BLE advertising sequence completed";
  } else {
    txStatus_.success = false;
    txStatus_.status = "failed";
    txStatus_.message = errOut;
  }
  xSemaphoreGive(mutex_);

  return ok;
}

void LampController::workerLoop() {
  while (true) {
    xSemaphoreTake(queueWakeSem_, pdMS_TO_TICKS(500));

    while (true) {
      xSemaphoreTake(mutex_, portMAX_DELAY);
      if (queue_.empty()) {
        preemptRequested_ = false;
        xSemaphoreGive(mutex_);
        break;
      }
      PendingItem item = queue_.front();
      queue_.pop_front();
      if (queue_.empty()) {
        preemptRequested_ = false;
      }
      xSemaphoreGive(mutex_);

      bool notifiedCaller = false;
      auto notifyCaller = [&](bool ok, const String& err, const EncodedPacket& pkt) {
        if (notifiedCaller || !item.sync) return;
        notifiedCaller = true;
        item.sync->ok = ok;
        item.sync->err = err;
        item.sync->frameHex = formatFrameHex(pkt.data, pkt.length);
        item.sync->txCount = pkt.txCount;
        if (item.sync->doneSem != nullptr) xSemaphoreGive(item.sync->doneSem);
      };

      // If light was off and user adjusted brightness/color, send Power ON briefly first
      if (item.autoPowerOnIfOff) {
        LampCommand onCmd;
        onCmd.type = LampCommandType::POWER;
        onCmd.on = true;
        EncodedPacket onPkt;
        String onErr;
        bool onPreempted = false;
        // Brief single-window turn-on burst before dimming/color
        xSemaphoreTake(mutex_, portMAX_DELAY);
        DeviceSettings cfgCopy = settings_;
        const LampProtocol* proto = getProtocolById(cfgCopy.protocol.c_str());
        LampTargetConfig target;
        target.deviceType = cfgCopy.deviceType;
        target.deviceId = cfgCopy.deviceId;
        target.channel = cfgCopy.channel;
        target.deviceAddress = cfgCopy.deviceAddress;
        target.txCount = cfgCopy.txCount;
        target.reversedCct = cfgCopy.reversedCct;
        target.adFlag = cfgCopy.adFlag;
        if (proto->encode(onCmd, target, onPkt)) {
          settings_.txCount = target.txCount;
        }
        xSemaphoreGive(mutex_);

        if (onPkt.length > 0) {
          advertiser_.transmitSequence(
              onPkt.data, onPkt.length, 2, 120, false, cfgCopy.adFlag,
              [this]() { return false; }, nullptr, onErr, onPreempted);
        }
      }

      EncodedPacket pkt;
      String err;
      bool preempted = false;
      bool ok = encodeAndTransmit(
          item.cmd,
          item.label,
          [&](bool startedOk, const String& startedErr) {
            notifyCaller(startedOk, startedErr, pkt);
          },
          pkt,
          err,
          preempted);

      if (!notifiedCaller) {
        notifyCaller(ok, err, pkt);
      }
    }
  }
}

} // namespace lampsmart
