#pragma once

// XPT2046 touch input. Polled from loop() on the main task; nonblocking apart
// from what the touched page's own handler does.
void setupTouch();
void handleTouch();
// Raw "finger down" state, for the sleep sequence.
bool touchPressed();
// XPT2046 PENIRQ (active low) is wired to this RTC-capable input on the CYD.
constexpr int TOUCH_IRQ_PIN = 36;
