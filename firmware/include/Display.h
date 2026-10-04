#pragma once
#include <TFT_eSPI.h>

// Defined in main.cpp. Draw only from the main Arduino task, never from the
// status worker or other background tasks.
extern TFT_eSPI tft;
