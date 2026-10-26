# Third-party notices

Original code and documentation in this project are licensed under the
[MIT License](LICENSE). The following incorporated material retains its
original license.

## Bosch BME280 compensation

The fixed-point temperature, pressure, and humidity compensation routines in
[`sf32/src/env_sensor.c`](sf32/src/env_sensor.c) are adapted from Bosch's
[BME280 SensorAPI](https://github.com/boschsensortec/BME280_SensorAPI).

Copyright (c) 2020 Bosch Sensortec GmbH. All rights reserved.

These portions are licensed under the BSD 3-Clause license. The full notice
and conditions are in [licenses/BSD-3-Clause-Bosch.txt](licenses/BSD-3-Clause-Bosch.txt).

## SF32 build templates

The project files `sf32/project/SConstruct`, `sf32/project/SConscript`,
`sf32/project/Kconfig.proj`, and `sf32/project/rtconfig_project.h` derive from
the [SiFli SDK](https://github.com/OpenSiFli/SiFli-SDK) and
[LilyGo T-Display-SF32 examples](https://github.com/Xinyuan-LilyGO/T-Display-SF32).
They retain the SDK's Apache License 2.0, included in
[licenses/Apache-2.0.txt](licenses/Apache-2.0.txt).

## SDK patches

[`sdk-patches/0001-posix-build-resource.patch`](sdk-patches/0001-posix-build-resource.patch)
modifies a host build helper from RT-Thread / SiFli SDK, copyright
2006–2015 RT-Thread Development Team. That patch retains the helper's
GPL-2.0-or-later license; see [licenses/GPL-2.0.txt](licenses/GPL-2.0.txt).
The helper runs during the build and is not linked into the firmware.

[`sdk-patches/0002-charger-failed-init-cleanup.patch`](sdk-patches/0002-charger-failed-init-cleanup.patch)
modifies the SDK charger driver, copyright 2024 RT-Thread Development Team.
That patch retains the driver's Apache License 2.0. Both patches identify
their source commits and changes. The setup script itself is MIT-licensed.

## External dependencies

The SiFli SDK, RT-Thread, LVGL, ESP32 Arduino core, TFT_eSPI, and other build
dependencies are obtained separately and remain under their own licenses.
The project's MIT license does not replace any dependency's license or
file-specific terms. SDK downloads, local research copies, and firmware
backups are excluded from this repository.
