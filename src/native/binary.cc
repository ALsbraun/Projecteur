// SPDX-License-Identifier: MIT
#include "binary.h"

namespace {
struct KeyName {
  const char* name;
  int value;
};
constexpr KeyName keys[] {
  {"Escape", 0x01000000}, {"Esc", 0x01000000}, {"Tab", 0x01000001},
  {"Backspace", 0x01000003}, {"Return", 0x01000004}, {"Enter", 0x01000005},
  {"Insert", 0x01000006}, {"Delete", 0x01000007}, {"Pause", 0x01000008},
  {"Print", 0x01000009}, {"SysReq", 0x0100000a}, {"Home", 0x01000010}, {"End", 0x01000011},
  {"Left", 0x01000012}, {"Up", 0x01000013}, {"Right", 0x01000014}, {"Down", 0x01000015},
  {"PgUp", 0x01000016}, {"PgDown", 0x01000017}, {"CapsLock", 0x01000024},
  {"NumLock", 0x01000025}, {"ScrollLock", 0x01000026}, {"Menu", 0x01000055},
  {"Help", 0x01000058}, {"AudioLowerVolume", 0x01000070}, {"AudioMute", 0x01000071},
  {"AudioRaiseVolume", 0x01000072}, {"space", 0x20},
};
}

namespace native {
StoredKeys StoredKeys::fromString(const QString& symbol)
{
  if (symbol == "Alt+Tab") { return StoredKeys(Qt::AltModifier | 0x01000001); }
  if (symbol == "Alt+F4") { return StoredKeys(Qt::AltModifier | 0x01000033); }
  if (g_utf8_validate(symbol.c_str(), symbol.size(), nullptr)
      && g_utf8_strlen(symbol.c_str(), symbol.size()) == 1) {
    return StoredKeys(g_unichar_toupper(g_utf8_get_char(symbol.c_str())));
  }
  for (const auto& key : keys) {
    if (symbol == key.name) { return StoredKeys(key.value); }
  }
  if (symbol.startsWith('F')) {
    const int number = symbol.mid(1).toInt();
    if (number >= 1 && number <= 35) { return StoredKeys(0x0100002f + number); }
  }
  return StoredKeys(Qt::Key_unknown);
}

QString StoredKeys::toString() const
{
  const int key = m_keys.front();
  if (key >= 0x01000030 && key <= 0x01000052) {
    return "F" + QString::number(key - 0x0100002f);
  }
  for (const auto& entry : keys) {
    if (entry.value == key) { return entry.name; }
  }
  if (key > 0 && key <= 0x10ffff && g_unichar_validate(key)) {
    char text[6];
    const auto size = g_unichar_to_utf8(key, text);
    return QString(text, size);
  }
  return {};
}
}
