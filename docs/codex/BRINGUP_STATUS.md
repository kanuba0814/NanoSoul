# NanoSoul Bring-up Status

Source baseline: `/home/gxxl/testP4` commit `ec763431a7efbbc9b8143a049ed3e34a9a476d6a`.

| Module | Hardware | Bus / GPIO | Status | Test Method | Notes |
|---|---|---|---|---|---|
| Display | ST7701S WLK2802MIPI-15P V2 | MIPI DSI | MIGRATED | `idf.py build`; boot UI pending board flash | migrated from testP4 |
| Touch | FT6x36 / FT5x06 compatible | I2C0 GPIO7/8 | MIGRATED | LVGL pointer path; board flash pending | transform preserved from testP4 |
| Light | BH1750 | I2C1 GPIO20/21 | MIGRATED | `sense_core_read_bh1750()`; board flash pending | missing device is non-fatal |
| Audio | ES8311 + NS4150B speaker | I2S GPIO9-13, PA GPIO53, I2C0 | MIGRATED | UI audio tone button; board flash pending | testP4 player imported |
| SD | TF card | GPIO39-44 via SDSPI fallback path | MIGRATED | mount/list WAV path; board flash pending | missing card is non-fatal |
| Camera | OV5647 MIPI-CSI | CSI + SCCB on I2C0 | MIGRATED | `vision_core_init()` / preview wrappers; board flash pending | esp_video path imported |
| Wi-Fi | ESP32-C6 hosted | board hosted link | MIGRATED | `net_core_init()`; board flash pending | local UI does not depend on connect success |
| VL6180X-L | VL6180X | I2C1 planned, XSHUT GPIO22 | ABSENT | status placeholder | future |
| VL6180X-C | VL6180X | I2C1 planned, XSHUT GPIO23 | ABSENT | status placeholder | future |
| VL6180X-R | VL6180X | I2C1 planned, XSHUT GPIO26 | ABSENT | status placeholder | future |
| Motion | 3 omni wheels | frozen pins only | DISABLED | not initialized by default | out of this migration |

## Current Verification

```sh
. ~/.espressif/v5.5.2/esp-idf/export.sh
idf.py build
./tools/run_host_tests.sh
./tools/run_target_tests.sh
```

`idf.py build` passes. Host and target scripts are still integration placeholders.
