// SPDX-License-Identifier: MIT
#pragma once

#include "files.h"
#include <array>

namespace native {

// On-disk format shared by the GNOME and Plasma editions. The original
// implementation used QDataStream 6.0 with its default big-endian byte order.
class BinaryStream
{
public:
  enum Status { Ok, ReadPastEnd, ReadCorruptData };
  static constexpr int Qt_6_0 = 20;
  void setVersion(int) {}

  explicit BinaryStream(File* file) : m_input(file->readAll()) {}
  explicit BinaryStream(const QByteArray& input) : m_input(input) {}
  BinaryStream(QByteArray* output, int) : m_output(output) { m_output->clear(); }

  Status status() const { return m_status; }
  void setStatus(Status status) { if (m_status == Ok) { m_status = status; } }
  size_t remaining() const { return m_input.size() - m_position; }

  template<typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
  BinaryStream& operator<<(T value)
  {
    using U = std::make_unsigned_t<std::conditional_t<std::is_same_v<T, bool>, uint8_t, T>>;
    if (!m_output) { setStatus(ReadCorruptData); return *this; }
    for (size_t i = sizeof(T); i > 0; --i) {
      m_output->push_back(static_cast<char>((static_cast<U>(value) >> ((i - 1) * 8)) & 255));
    }
    return *this;
  }

  template<typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
  BinaryStream& operator>>(T& value)
  {
    using U = std::make_unsigned_t<std::conditional_t<std::is_same_v<T, bool>, uint8_t, T>>;
    value = {};
    if (m_status != Ok) { return *this; }
    if (remaining() < sizeof(T)) { setStatus(ReadPastEnd); return *this; }
    U bits = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
      bits = static_cast<U>((bits << 8) | static_cast<unsigned char>(m_input[m_position++]));
    }
    if constexpr (std::is_same_v<T, bool>) { value = bits != 0; }
    else { std::memcpy(&value, &bits, sizeof(T)); }
    return *this;
  }

  BinaryStream& operator<<(const QByteArray& bytes)
  {
    *this << static_cast<uint32_t>(bytes.size());
    if (m_output) { m_output->append(bytes); }
    return *this;
  }

  BinaryStream& operator>>(QByteArray& bytes)
  {
    uint32_t length = 0;
    *this >> length;
    bytes.clear();
    if (length == UINT32_MAX || m_status != Ok) { return *this; }
    if (length > remaining()) { setStatus(ReadPastEnd); return *this; }
    bytes.assign(m_input.data() + m_position, length);
    m_position += length;
    return *this;
  }

private:
  QByteArray m_input;
  QByteArray* m_output = nullptr;
  size_t m_position = 0;
  Status m_status = Ok;
};

class StoredKeys
{
public:
  StoredKeys() = default;
  explicit StoredKeys(int first, int second = 0, int third = 0, int fourth = 0)
    : m_keys{first, second, third, fourth} {}

  int count() const
  { return std::count_if(m_keys.begin(), m_keys.end(), [](int key) { return key != 0; }); }
  struct Key { int value; int key() const { return value; } };
  Key operator[](size_t i) const { return {i < m_keys.size() ? m_keys[i] : 0}; }
  bool operator==(const StoredKeys& other) const { return m_keys == other.m_keys; }
  bool operator!=(const StoredKeys& other) const { return !(*this == other); }
  void swap(StoredKeys& other) { m_keys.swap(other.m_keys); }
  static StoredKeys fromString(const QString& symbol);
  QString toString() const;

  friend BinaryStream& operator<<(BinaryStream& stream, const StoredKeys& keys)
  {
    if (keys.count() <= 1) { return stream << uint32_t{1} << keys.m_keys.front(); }
    stream << uint32_t{4};
    for (const auto key : keys.m_keys) { stream << key; }
    return stream;
  }

  friend BinaryStream& operator>>(BinaryStream& stream, StoredKeys& keys)
  {
    uint32_t count = 0;
    stream >> count;
    keys.m_keys.fill(0);
    if (count != 1 && count != 4) {
      stream.setStatus(BinaryStream::ReadCorruptData);
      return stream;
    }
    for (size_t i = 0; i < count; ++i) { stream >> keys.m_keys[i]; }
    return stream;
  }

private:
  std::array<int32_t, 4> m_keys{};
};
}

using QDataStream = native::BinaryStream;
using QKeySequence = native::StoredKeys;

namespace Qt {
// Key and modifier values from the legacy settings format.
enum KeyboardModifier {
  ShiftModifier = 0x02000000, ControlModifier = 0x04000000, AltModifier = 0x08000000,
  MetaModifier = 0x10000000, KeypadModifier = 0x20000000, GroupSwitchModifier = 0x40000000
};
inline constexpr int Key_unknown = 0x01ffffff;
}
