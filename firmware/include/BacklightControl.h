#pragma once
#include <stdint.h>

// Pure logic shared by the main task and host tests. Times are UTC epoch seconds.
struct SolarSchedule {
  uint32_t sunrise = 0;
  uint32_t sunset = 0;
  uint32_t dayStart = 0;
  uint32_t dayEnd = 0;
  uint32_t validUntil = 0;
  uint32_t timestamp = 0;

  bool valid() const {
    return dayStart > 0 && dayStart < sunrise && sunrise < sunset &&
           sunset < dayEnd && validUntil >= dayEnd &&
           uint64_t(validUntil) <= uint64_t(dayEnd) + 86400 &&
           timestamp >= dayStart && timestamp < validUntil;
  }
};

class BacklightControl {
 public:
  static constexpr uint8_t DAY_DUTY = 255;
  static constexpr uint8_t NIGHT_DUTY = 38;
  static constexpr uint8_t BOOST_DUTY = 153;
  static constexpr uint32_t BOOST_MS = 30000;

  void setSchedule(const SolarSchedule& value, uint32_t now) {
    schedule = value;
    receivedAt = now;
  }

  bool isNight(uint32_t now) const {
    if (!schedule.valid()) return false;
    // A monotonic clock avoids local timezone/DST arithmetic on the ESP32.
    uint64_t epoch = uint64_t(schedule.timestamp) + uint32_t(now - receivedAt) / 1000;
    if (epoch >= schedule.validUntil) return false;
    return epoch < schedule.sunrise || epoch >= schedule.sunset;
  }

  void acceptedTouch(uint32_t now) {
    if (isNight(now)) {
      boosting = true;
      lastBoostTouch = now;
    }
  }

  uint8_t duty(uint32_t now) {
    if (!isNight(now)) {
      boosting = false;
      return DAY_DUTY;
    }
    if (boosting && uint32_t(now - lastBoostTouch) >= BOOST_MS) boosting = false;
    return boosting ? BOOST_DUTY : NIGHT_DUTY;
  }

 private:
  SolarSchedule schedule;
  uint32_t receivedAt = 0;
  uint32_t lastBoostTouch = 0;
  bool boosting = false;
};
