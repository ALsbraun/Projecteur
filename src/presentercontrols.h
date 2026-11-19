// This file is part of Projecteur - see LICENSE.md.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>

namespace PresenterControls {

// Logitech control IDs, also documented in Solaar's special_keys.py.
constexpr uint16_t PointerHold = 0x00d8;
constexpr uint16_t SwitchHighlight = 0x00df;
constexpr uint8_t Divertable = 0x20;

// Direct Bluetooth notifications may use Logitech's local-device address even
// when commands are accepted at a wireless slot. Receiver slots stay isolated.
inline bool notificationDeviceMatches(uint8_t reported, uint8_t expected, bool bluetooth)
{
  return reported == expected || (bluetooth && reported == 0xff);
}

// Set-reporting replies may echo valid bits, or contain only current state
// bits. getCidReporting always reports state; valid bits are write masks.
inline bool reportingStateMatches(uint8_t requested, uint8_t reported)
{
  const uint8_t stateMask = ((requested & 0x02) ? 0x01 : 0)
    | ((requested & 0x08) ? 0x04 : 0)
    | ((requested & 0x20) ? 0x10 : 0)
    | ((requested & 0x80) ? 0x40 : 0);
  return (requested & stateMask) == (reported & stateMask);
}

inline uint16_t controlId(uint8_t high, uint8_t low)
{
  return (uint16_t(high) << 8) | low;
}

// REPROG_CONTROLS_V4 event 0 contains up to four complete 16-bit CIDs.
inline bool contains(const uint8_t* payload, size_t size, uint16_t control)
{
  for (size_t offset = 0; offset + 1 < std::min<size_t>(size, 8); offset += 2) {
    if (controlId(payload[offset], payload[offset + 1]) == control) { return true; }
  }
  return false;
}

enum class PointerTransition { None, Activate, Deactivate };

// Keep state per connection: two presenters may be held at the same time, and
// losing one hidraw connection must not hide the other presenter's overlay.
template<typename Device, typename Connection>
class PointerActivity {
public:
  PointerTransition observe(const Device& device, const Connection& connection, bool pressed)
  {
    m_reported[connection] = device;
    if (pressed) {
      m_held.insert(connection);
      return PointerTransition::Activate;
    }
    m_held.erase(connection);
    return anyHeld() ? PointerTransition::None : PointerTransition::Deactivate;
  }

  bool hasReports(const Device& device) const
  {
    return std::any_of(m_reported.begin(), m_reported.end(), [&device](const auto& entry) {
      return entry.second == device;
    });
  }

  bool anyHeld() const { return !m_held.empty(); }
  bool shouldExpire() const { return !anyHeld(); }

  bool removeConnection(const Connection& connection)
  {
    m_reported.erase(connection);
    return m_held.erase(connection) && !anyHeld();
  }

  bool removeDevice(const Device& device)
  {
    const bool wasHeld = anyHeld();
    for (auto it = m_reported.begin(); it != m_reported.end();) {
      if (it->second == device) {
        m_held.erase(it->first);
        it = m_reported.erase(it);
      } else {
        ++it;
      }
    }
    return wasHeld && !anyHeld();
  }

private:
  std::map<Connection, Device> m_reported;
  std::set<Connection> m_held;
};

} // namespace PresenterControls
