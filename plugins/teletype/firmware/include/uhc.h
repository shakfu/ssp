// Replaces Atmel ASF's USB host controller header, whose licence restricts redistribution to Atmel
// microcontroller products. libavr32's hid.h declares hid_change() with a device pointer; under the
// mock nothing calls it, so the device type is only declared.
#pragma once

#include "conf_usb_host.h"  // monome-rack's; teletype's sources rely on its compiler.h

typedef struct uhc_device uhc_device_t;
