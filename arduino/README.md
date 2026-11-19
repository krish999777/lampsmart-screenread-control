# Flashing via Arduino IDE

This directory contains a ready-to-open sketch for the Arduino IDE:
`arduino/LampSmartController/LampSmartController.ino`

## Prerequisites in Arduino IDE

1. **ESP32 Board Support**:
   - In Arduino IDE, go to **Settings / Preferences** -> **Additional boards manager URLs**:
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Go to **Tools** -> **Board** -> **Boards Manager...**, search for `esp32` by Espressif Systems, and install it.

2. **ArduinoJson Library**:
   - Go to **Tools** -> **Manage Libraries...** (or `Ctrl+Shift+I` / `Cmd+Shift+I`).
   - Search for `ArduinoJson` (by Benoit Blanchon) and install version **7.x** (e.g. 7.2.0 or 7.4.x).

3. **Required Board Settings (Tools menu)**:
   - **Board**: `ESP32 Dev Module` (or your specific ESP32 variant like `ESP32-S3 Dev Module` / `ESP32-C3 Dev Module`).
   - **Upload Speed**: `921600` (or `115200`).
   - **Partition Scheme**: Select **"No OTA (2MB APP / 2MB SPIFFS)"** or **"Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)"**.
     *(Important: The default 1.2MB partition is too small because the combined BLE + Wi-Fi + HTTP stack is ~1.6MB).*
   - **Flash Frequency**: `80MHz`
   - **Flash Mode**: `QIO` (or `DIO`)
   - **Core Debug Level**: `Info` or `None`
   - **Port**: Select your ESP32 serial port (e.g., `/dev/cu.usbserial-*` or `/dev/cu.wchusbserial*`).

4. **Flashing the Web UI (LittleFS)**:
   - The compiled code runs the BLE broadcaster, serial console, and REST API out of the box.
   - To serve the web application directly from the ESP32, upload the `data/` folder using the **ESP32 LittleFS Filesystem Uploader** plugin in Arduino IDE (or flash it using PlatformIO: `~/.pio-venv/bin/pio run -e esp32dev -t uploadfs`).
