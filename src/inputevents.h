// This file is part of Projecteur - see LICENSE.md.
#pragma once

#include <linux/input.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace InputEvents {

inline bool hasPointerMotion(const input_event* events, size_t count)
{
  if (!count) { return false; }
  return std::any_of(events, events + count, [](const input_event& event) {
    return event.type == EV_REL && (event.code == REL_X || event.code == REL_Y);
  });
}

struct RoutedFrame {
  std::vector<input_event> mouse;
  std::vector<input_event> keyboard;
};

// A HID report can contain both motion/buttons and keyboard events. Routing the
// entire report according to its first event loses keys on mouse-only devices.
inline RoutedFrame routeFrame(const input_event* events, size_t count)
{
  RoutedFrame frame;
  for (size_t i = 0; i < count; ++i) {
    const auto& event = events[i];
    if (event.type == EV_REL
        || (event.type == EV_KEY && event.code >= BTN_MISC && event.code < KEY_OK)) {
      frame.mouse.push_back(event);
    } else if (event.type == EV_KEY && event.code < KEY_CNT) {
      frame.keyboard.push_back(event);
    }
  }
  const input_event sync{{}, EV_SYN, SYN_REPORT, 0};
  if (!frame.mouse.empty()) { frame.mouse.push_back(sync); }
  if (!frame.keyboard.empty()) { frame.keyboard.push_back(sync); }
  return frame;
}

inline int signedMotion(uint8_t high, uint8_t low)
{
  const int value = (int(high) << 8) | low;
  return value >= 0x8000 ? value - 0x10000 : value;
}

inline uint8_t pointerSpeed(uint8_t speed)
{
  return 0x10 | std::min<uint8_t>(speed, 9);
}

class PressedKeys {
public:
  void record(const input_event* events, size_t count)
  {
    for (size_t i = 0; i < count; ++i) {
      const auto& event = events[i];
      if (event.type != EV_KEY || event.code >= KEY_CNT) { continue; }
      if (event.value == 1) { m_keys.insert(event.code); }
      else if (event.value == 0) { m_keys.erase(event.code); }
    }
  }

  std::vector<input_event> releases() const
  {
    std::vector<input_event> events;
    for (const auto key : m_keys) { events.push_back(input_event{{}, EV_KEY, key, 0}); }
    if (!events.empty()) { events.push_back(input_event{{}, EV_SYN, SYN_REPORT, 0}); }
    return events;
  }

private:
  std::set<uint16_t> m_keys;
};

} // namespace InputEvents
