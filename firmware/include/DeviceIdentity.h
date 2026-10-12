#pragma once
#include "ProductIdentity.h"

// This device's HomeDeck Mini identity (ProductIdentity.h), computed from the
// station MAC by initDeviceIdentity() at the start of setup(), before any
// task starts; read-only afterwards, so any task may read it.
void initDeviceIdentity();
const DeviceIdentity& deviceIdentity();
