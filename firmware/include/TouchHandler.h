#pragma once

// XPT2046 touch input. Polled from loop() on the main task; nonblocking apart
// from what the touched page's own handler does.
void setupTouch();
void handleTouch();
