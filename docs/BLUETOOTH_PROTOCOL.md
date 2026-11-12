# LampSmart Pro Bluetooth Protocol Research

This document summarizes our reverse-engineering review of the reference repositories under `references/` and defines the protocol architecture used by this ESP32 controller.

## 1. Reference Repositories Analyzed

| Repository | Protocol / Scope | Key Findings |
|---|---|---|
| `qinjunhaocn/xLamp` | LampSmart Pro 1.4.0 (V3) | Complete reverse-engineered `libencodeV3.so` implementation: 31-byte V3 PDU, AES-128-ECB signing, `XBOXES` whitening, CRC-16/CCITT, CCT dimming (`0x21`), RGB (`0x22`), scenes (`0x10`/`0x46`/`0x47`/`0x48`), night light (`0x23`), pairing (`0x28`/`0x45`), Android 13×16-bit Service UUID advertising, and Unicorn-verified test vectors (`tool/verify.dart`). |
| `aronsky/esphome-components` | ESP32 BLE Adv (`FanLampEncoderV1`, `FanLampEncoderV2`, `Zhijia`) | Confirms ESP32 `esp_ble_gap_config_adv_data_raw` / `esp_ble_gap_start_advertising` at 20 ms interval (`ADV_TYPE_NONCONN_IND`). Confirms `lampsmart_pro` `v3` uses prefix `[0x30, 0x80, 0x00]`, device type `0x0100`, header `[0xF0, 0x08]`, AD flags `0x19` (or `0x02`), AD type `0x03` (16-bit Service UUIDs), and AES-128 signing. Does not implement RGB. |
| `NicoIIT/ha-ble-adv` | Python codecs (`FanLampEncoderV1`, `V1aa`, `V2`) | Confirms `LSV2` (`0x0100`, `[0x30, 0x80, 0x00]`) uses AD type `0x03` for phone apps and `0x16` for some remotes. Documents `0x22` RGB args (`r, g, b`), `0x1C` RGB power, `0x1E`/`0x1F` RGB auto mode, and V1 variants (`0x77 0xF8` / `0xF9 0x08`). |
| `flicker581/esphome-lampsmart` | ESPHome (`VARIANT_3`, `VARIANT_2`, `VARIANT_1A`, `VARIANT_1B`) | Implements V3 with AD type `0x16` and prefix byte `0x10` at offset 7, plus V1a/V1b legacy packets. CCT-only (`0x21`). |
| `zt8989/esphome-lampsmart-pro` | ESPHome V1 legacy + group ID | Uses V1 legacy bit-reversed + LFSR-whitened 31-byte packet (`0x71 0x0F 0x55 ...`). CCT-only, no RGB. |
| `StevenReitsma/esphome-lampsmart-pro` | ESPHome V1 legacy + FreeRTOS queue | Uses V1 legacy packet with FreeRTOS command queue and non-blocking `loop()` advertising state machine. CCT-only. |
| `MasterDevX/lampify` | Linux BlueZ C V1 legacy | Earliest C reference for V1 legacy (`0x28`, `0x10`, `0x11`, `0x21`) over raw HCI advertising. |

## 2. Selected Primary Protocol: LampSmart Pro V3 (`xLamp` / `LSV2`)

We use **LampSmart Pro V3** as the default protocol because:
1. It matches current LampSmart Pro 1.4.0 apps and supports the full requested feature set: **Power, Brightness + CCT Dimming, Custom RGB/HSV Color, Scenes 1–4 + Scene On, Night Light, Pairing, and Unpairing**.
2. Its frame construction is verified byte-for-byte against `libencodeV3.so` via Unicorn test vectors in `xLamp/tool/verify.dart`.
3. Because older fixtures in the field use V2 (unsigned) or V1 (`v1a`/`v1b`, CCT-only), the firmware also implements clean selectable protocol variants (`v3`, `v3_srvdata`, `v2`, `v1a`, `v1b`) behind a unified `LampProtocol` interface.

> **Push-Only Limitation:** BLE advertising is strictly unidirectional (controller → lamp). There is no GATT connection and no acknowledgment packet from the lamp. All reported states in the UI/API reflect the *most recently requested/transmitted command*, never confirmed physical lamp state.

## 3. V3 31-Byte Frame Layout

All multi-byte integer fields are **little-endian**.

| Offset | Len | Field | Notes |
|---:|---:|---|---|
| `0..2` | 3 | AD Flags | `02 01 02` (default) or `02 01 19` |
| `3` | 1 | AD Length | `0x1B` (27 bytes follow) |
| `4` | 1 | AD Type | `0x03` (Complete List of 16-bit Service UUIDs — matches xLamp Android on-air packets) or `0x16` (Service Data, `v3_srvdata` variant) |
| `5..6` | 2 | Service UUID / Header | `F0 08` (`0x08F0`) |
| `7` | 1 | Wrapper byte | `0x30` |
| `8` | 1 | V3 flag | `0x80` |
| `9` | 1 | Reserved | `0x00` (pre-whitening) |
| `10` | 1 | `tx_count` | Increments `0..255` per command |
| `11..12` | 2 | `type` | `0x0100` (256=Lamp), `0x0200` (512=Fan), `0x0400` (1024=Exhaust), `0x0800` (2048=Bath) |
| `13..16` | 4 | `var1` (`identifier`) | `device_id + channel` (masked to 31-bit in xLamp) |
| `17` | 1 | `var2` (`address`) | Device address / group index (`0..255`) |
| `18..19` | 2 | `command` | 16-bit opcode |
| `20` | 1 | Reserved | `0x00` |
| `21..23` | 3 | `p21, p22, p23` | Command parameters |
| `24..25` | 2 | `signature` | First 2 bytes (LE) of `AES-128-ECB(key, PDU[8..23])`; `0xFFFF` if `0x0000` |
| `26` | 1 | Spare | `0x00` |
| `27..28` | 2 | `rand` | 16-bit seed (`0x2B53` default or random) |
| `29..30` | 2 | `crc16` | `CRC-16/CCITT(PDU[7..28], init = ~rand & 0xFFFF)` poly `0x1021` |

### Signing & Obfuscation
1. **AES-128-ECB Signature** (before whitening):
   - `key[0] = rand & 0xFF`
   - `key[1] = (rand >> 8) & 0xFF`
   - `key[2] = tx_count`
   - `key[3..15] = {0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16}`
   - Encrypt `PDU[8..23]` (16 bytes); take `out[0] | (out[1] << 8)`; if `0`, use `0xFFFF`. Store at `PDU[24..25]`.
2. **XBOXES Whitening**:
   - Using the 32-byte row 0 of `XBOXES`, for `i = 9..26`:
     `PDU[i] ^= (rand & 0xFF) ^ XBOXES[(i + (rand & 0xFF)) & 0x1F]`
3. **CRC-16/CCITT**:
   - Computed over `PDU[7..28]` (22 bytes) with polynomial `0x1021` and initial value `(~rand) & 0xFFFF`.
4. **13 × 16-bit Service UUIDs**:
   - `PDU[5..30]` (26 bytes) maps to 13 little-endian 16-bit Service UUIDs: `uuid[i] = PDU[5+2i] | (PDU[6+2i] << 8)`.

## 4. V3 Opcodes & Parameter Mapping

| Action | Opcode | `p21` | `p22` | `p23` | Notes |
|---|---:|---:|---:|---:|---|
| Pair | `0x28` | 0 | 0 | 0 | Broadcast within ~5s of lamp power-on |
| Unpair | `0x45` | 0 | 0 | 0 | |
| Power On | `0x10` | 0 | 0 | 0 | |
| Power Off | `0x11` | 0 | 0 | 0 | |
| Dimming (CCT) | `0x21` | 0 | `cool` | `warm` | See channel math below |
| Night Light | `0x23` | 0 | 0 | 0 | |
| RGB Color | `0x22` | `r` | `g` | `b` | Scaled by brightness factor |
| RGB Power | `0x1C` | `1/0` | 0 | 0 | Auxiliary RGB toggle |
| Scene On | `0x10` | `1` | 0 | 0 | Activates scene mode |
| Scene Set | `0x46` | `idx` | 0 | 0 | |
| Scene Switch | `0x47` | `idx` | 0 | 0 | `idx` `0..3` for Scenes 1–4 |
| Scene Reset | `0x48` | 0 | 0 | 0 | |

### Brightness & Color Temperature (`0x21`)
- Scale: `brightness` ∈ `[1, 100]` (`b = brightness / 100.0`), `temperature` ∈ `[0, 100]` (`0` = warmest, `100` = coldest).
- `factor = 0.1 + 0.9 * b` (retains a 10% floor so minimum brightness is never `0` / power-off).
- If `temperature > 50`:
  - `cool = floor(255 * factor)`
  - `warm = floor(255 * factor * (1 - (temperature - 50) / 50))`
- Else (`temperature <= 50`):
  - `cool = floor(255 * factor * (temperature / 50))`
  - `warm = floor(255 * factor)`

### Color Control (`0x22`)
- Input HSV `(h: 0..360, s: 0..100, v: 0..100)` is converted to full-value RGB `(r0, g0, b0)` at `V=100%`, then scaled by `factor = 0.1 + 0.9 * (v / 100.0)` matching `LampCommands.scaleChannel`. Direct RGB `(r, g, b)` inputs are scaled with the same brightness factor formula.

## 5. Advertising Timing

Matching `xLamp`'s `BleAdvertisePlugin.kt`:
- Default sequence: **8 windows × 500 ms** (`4000 ms` total) broadcasting the 31-byte frame at 20 ms BLE advertising interval (`ADV_TYPE_NONCONN_IND`), followed by a **500 ms end marker** (`02 01 02 05 03 AA BB 12 34`, UUIDs `0xBBAA`, `0x3412`), then advertising stops.
- On ESP32, the sequence runs in a dedicated FreeRTOS task and is **preemptible** when a newer command arrives (coalescing intermediate slider updates so the final value always completes its full burst).

