#include "ble_advertiser.h"
#include "protocols/lampsmart_v3.h"

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace lampsmart {

namespace {

static constexpr const char* kTag = "BleAdvertiser";

static esp_ble_adv_params_t kAdvParams = {
  .adv_int_min = 0x20, // 20 ms
  .adv_int_max = 0x20, // 20 ms
  .adv_type = ADV_TYPE_NONCONN_IND,
  .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
  .peer_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
  .channel_map = ADV_CHNL_ALL,
  .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

enum GapWaitBit : uint32_t {
  BIT_DATA_SET = 1u << 0,
  BIT_STARTED  = 1u << 1,
  BIT_STOPPED  = 1u << 2,
};

SemaphoreHandle_t sGapDoneSem = nullptr;
volatile uint32_t sWaitTarget = 0;
volatile esp_bt_status_t sLastStatus = ESP_BT_STATUS_SUCCESS;

void gapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
  if (sGapDoneSem == nullptr) return;
  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
      if (sWaitTarget == BIT_DATA_SET) {
        sLastStatus = param->adv_data_raw_cmpl.status;
        xSemaphoreGive(sGapDoneSem);
      }
      break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      if (sWaitTarget == BIT_STARTED) {
        sLastStatus = param->adv_start_cmpl.status;
        xSemaphoreGive(sGapDoneSem);
      }
      break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
      if (sWaitTarget == BIT_STOPPED) {
        sLastStatus = param->adv_stop_cmpl.status;
        xSemaphoreGive(sGapDoneSem);
      }
      break;
    default:
      break;
  }
}

bool waitForGapEvent(uint32_t bit, uint32_t timeoutMs, esp_bt_status_t& statusOut) {
  sWaitTarget = bit;
  if (xSemaphoreTake(sGapDoneSem, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
    sWaitTarget = 0;
    return false;
  }
  sWaitTarget = 0;
  statusOut = sLastStatus;
  return true;
}

} // namespace

bool BleAdvertiser::begin() {
  if (ready_) return true;

  if (sGapDoneSem == nullptr) {
    sGapDoneSem = xSemaphoreCreateBinary();
    if (sGapDoneSem == nullptr) {
      lastError_ = "Failed to allocate BLE semaphore";
      return false;
    }
  }

#if CONFIG_IDF_TARGET_ESP32
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  }
#endif

  if (!btStart()) {
    lastError_ = "btStart() failed to initialize BLE controller";
    ESP_LOGE(kTag, "%s", lastError_.c_str());
    return false;
  }

  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
    esp_err_t err = esp_bluedroid_init();
    if (err != ESP_OK) {
      lastError_ = String("esp_bluedroid_init failed: ") + esp_err_to_name(err);
      ESP_LOGE(kTag, "%s", lastError_.c_str());
      return false;
    }
  }

  if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
    esp_err_t err = esp_bluedroid_enable();
    if (err != ESP_OK) {
      lastError_ = String("esp_bluedroid_enable failed: ") + esp_err_to_name(err);
      ESP_LOGE(kTag, "%s", lastError_.c_str());
      return false;
    }
  }

  esp_err_t err = esp_ble_gap_register_callback(gapCallback);
  if (err != ESP_OK) {
    lastError_ = String("esp_ble_gap_register_callback failed: ") + esp_err_to_name(err);
    ESP_LOGE(kTag, "%s", lastError_.c_str());
    return false;
  }

  ready_ = true;
  lastError_ = "";
  ESP_LOGI(kTag, "BLE GAP advertiser initialized");
  return true;
}

bool BleAdvertiser::stopAdvertising() {
  if (!ready_ || !advertising_) {
    advertising_ = false;
    return true;
  }
  xSemaphoreTake(sGapDoneSem, 0);
  esp_err_t err = esp_ble_gap_stop_advertising();
  if (err != ESP_OK) {
    advertising_ = false;
    lastError_ = String("stop_advertising failed: ") + esp_err_to_name(err);
    return false;
  }
  esp_bt_status_t st = ESP_BT_STATUS_SUCCESS;
  waitForGapEvent(BIT_STOPPED, 300, st);
  advertising_ = false;
  return true;
}

bool BleAdvertiser::advertiseFrame(const uint8_t* data, size_t len, String& errOut) {
  if (!ready_) {
    errOut = lastError_.length() ? lastError_ : "BLE stack is not initialized";
    return false;
  }
  if (data == nullptr || len == 0 || len > 31) {
    errOut = "Invalid BLE advertising payload length";
    return false;
  }

  if (advertising_) {
    stopAdvertising();
  }

  // Drain any stale semaphore signal
  xSemaphoreTake(sGapDoneSem, 0);
  esp_err_t err = esp_ble_gap_config_adv_data_raw(const_cast<uint8_t*>(data), static_cast<uint32_t>(len));
  if (err != ESP_OK) {
    errOut = String("esp_ble_gap_config_adv_data_raw failed: ") + esp_err_to_name(err);
    lastError_ = errOut;
    return false;
  }

  esp_bt_status_t st = ESP_BT_STATUS_SUCCESS;
  if (!waitForGapEvent(BIT_DATA_SET, 500, st) || st != ESP_BT_STATUS_SUCCESS) {
    errOut = "BLE raw adv data set failed or timed out";
    lastError_ = errOut;
    return false;
  }

  xSemaphoreTake(sGapDoneSem, 0);
  err = esp_ble_gap_start_advertising(&kAdvParams);
  if (err != ESP_OK) {
    errOut = String("esp_ble_gap_start_advertising failed: ") + esp_err_to_name(err);
    lastError_ = errOut;
    return false;
  }

  if (!waitForGapEvent(BIT_STARTED, 500, st) || st != ESP_BT_STATUS_SUCCESS) {
    errOut = "BLE start advertising failed or timed out";
    lastError_ = errOut;
    return false;
  }

  advertising_ = true;
  lastError_ = "";
  return true;
}

bool BleAdvertiser::transmitSequence(
    const uint8_t* frame,
    size_t len,
    uint8_t repeats,
    uint16_t windowMs,
    bool sendEndMarker,
    uint8_t adFlag,
    const std::function<bool()>& shouldPreempt,
    const std::function<void(bool ok, const String& err)>& onFirstStarted,
    String& errOut,
    bool& wasPreempted) {
  wasPreempted = false;
  const uint8_t repCount = (repeats < 1) ? 1 : (repeats > 32 ? 32 : repeats);
  const uint16_t winMs = (windowMs < 50) ? 50 : (windowMs > 2000 ? 2000 : windowMs);

  auto sleepInterruptible = [&](uint16_t totalMs, uint16_t minFloorMs) -> bool {
    uint32_t elapsed = 0;
    while (elapsed < totalMs) {
      const uint32_t step = (totalMs - elapsed > 20) ? 20 : (totalMs - elapsed);
      vTaskDelay(pdMS_TO_TICKS(step));
      elapsed += step;
      if (elapsed >= minFloorMs && shouldPreempt && shouldPreempt()) {
        return true; // preempted
      }
    }
    return false;
  };

  for (uint8_t i = 0; i < repCount; ++i) {
    String err;
    if (!advertiseFrame(frame, len, err)) {
      if (i == 0 && onFirstStarted) {
        onFirstStarted(false, err);
      }
      errOut = err;
      return false;
    }
    if (i == 0 && onFirstStarted) {
      onFirstStarted(true, "");
    }

    // Keep each window active for at least 80ms before honoring preemption.
    const uint16_t minFloor = (i == 0) ? 80 : 40;
    if (sleepInterruptible(winMs, minFloor)) {
      wasPreempted = true;
      stopAdvertising();
      return true;
    }
  }

  if (sendEndMarker && (!shouldPreempt || !shouldPreempt())) {
    uint8_t marker[31]{};
    const size_t markerLen = LampSmartV3Protocol::buildEndMarker(marker, adFlag);
    String markerErr;
    if (advertiseFrame(marker, markerLen, markerErr)) {
      if (sleepInterruptible(winMs, 40)) {
        wasPreempted = true;
      }
    }
  }

  stopAdvertising();
  return true;
}

} // namespace lampsmart
