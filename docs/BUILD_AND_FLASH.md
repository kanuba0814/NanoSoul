# Build And Flash

## Toolchain

- ESP-IDF: `~/.espressif/v5.5.2/esp-idf`
- Expected target: `esp32p4`

## Environment Bootstrap

```sh
. ~/.espressif/v5.5.2/esp-idf/export.sh
```

If export fails with a missing Python environment, run the ESP-IDF installer for
this version first.

## Configure

```sh
idf.py set-target esp32p4
```

## Planned Follow-Up

- build
- flash
- monitor
- target-specific board notes
- common failure modes
