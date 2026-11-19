// SPDX-License-Identifier: MIT
#include "platform.h"

#include <cerrno>

namespace native {

Text Text::arg(const Text& value) const
{
  char first = '9' + 1;
  for (size_t i = 0; i + 1 < size(); ++i) {
    if ((*this)[i] == '%' && (*this)[i + 1] >= '1' && (*this)[i + 1] <= '9') {
      first = std::min(first, (*this)[i + 1]);
    }
  }

  if (first > '9') { return *this; }
  Text result;
  for (size_t i = 0; i < size(); ++i) {
    if ((*this)[i] == '%' && i + 1 < size() && (*this)[i + 1] == first) {
      result += value;
      ++i;
    } else {
      result += (*this)[i];
    }
  }
  return result;
}

Text Text::toHex() const
{
  static constexpr char digits[] = "0123456789abcdef";
  Text result;
  result.reserve(size() * 2);
  for (const unsigned char ch : *this) {
    result += digits[ch >> 4];
    result += digits[ch & 15];
  }
  return result;
}

uint64_t Text::toULongLong(bool* ok, int base) const
{
  const auto text = trimmed();
  char* end = nullptr;
  errno = 0;
  const auto number = g_ascii_strtoull(text.c_str(), &end, base);
  const bool valid = !text.empty() && !text.startsWith('-') && !*end && !errno;
  if (ok) { *ok = valid; }
  return valid ? number : 0;
}

uint16_t Text::toUShort(bool* ok, int base) const
{
  bool valid;
  const auto number = toULongLong(&valid, base);
  valid = valid && number <= UINT16_MAX;
  if (ok) { *ok = valid; }
  return valid ? number : 0;
}

unsigned Text::toUInt(bool* ok, int base) const
{
  bool valid;
  const auto number = toULongLong(&valid, base);
  valid = valid && number <= std::numeric_limits<unsigned>::max();
  if (ok) { *ok = valid; }
  return valid ? number : 0;
}

int Text::toInt(bool* ok, int base) const
{
  const auto text = trimmed();
  char* end = nullptr;
  errno = 0;
  const auto number = g_ascii_strtoll(text.c_str(), &end, base);
  const bool valid = !text.empty() && !*end && !errno
    && number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max();
  if (ok) { *ok = valid; }
  return valid ? number : 0;
}

double Text::toDouble(bool* ok) const
{
  const auto text = trimmed();
  char* end = nullptr;
  errno = 0;
  const double number = g_ascii_strtod(text.c_str(), &end);
  const bool valid = !text.empty() && !*end && !errno;
  if (ok) { *ok = valid; }
  return valid ? number : 0;
}

Text Text::fromLatin1(const Text& text)
{
  gsize length = 0;
  const auto utf8 = g_convert(text.data(), text.size(), "UTF-8", "ISO-8859-1", nullptr, &length, nullptr);
  const auto result = utf8 ? Text(utf8, length) : Text{};
  g_free(utf8);
  return result;
}

Object::Object(Object* parent) : m_parent(parent)
{
  if (m_parent) { m_parent->m_children.push_back(this); }
}

Object::~Object()
{
  m_lifetime.reset();
  destroyed();
  if (m_parent) {
    auto& children = m_parent->m_children;
    children.erase(std::remove(children.begin(), children.end(), this), children.end());
  }
  while (!m_children.empty()) { delete m_children.back(); }
}

void schedule(unsigned milliseconds, std::function<void()> callback)
{
  auto* task = new std::function<void()>(std::move(callback));
  g_timeout_add_full(G_PRIORITY_DEFAULT, milliseconds, [](gpointer data) -> gboolean {
    (*static_cast<std::function<void()>*>(data))();
    return G_SOURCE_REMOVE;
  }, task, [](gpointer data) { delete static_cast<std::function<void()>*>(data); });
}

void Timer::start()
{
  stop();
  m_source = g_timeout_add(std::max(0, m_interval), [](gpointer data) -> gboolean {
    auto* timer = static_cast<Timer*>(data);
    if (timer->m_singleShot) {
      timer->m_source = 0;
      timer->timeout();
      return G_SOURCE_REMOVE;
    }
    timer->timeout();
    return G_SOURCE_CONTINUE;
  }, this);
}

void Timer::stop()
{
  if (m_source) { g_source_remove(std::exchange(m_source, 0)); }
}

FdWatch::FdWatch(int fd, Type type, Object* parent) : Object(parent), m_fd(fd), m_type(type)
{
  setEnabled(true);
}

void FdWatch::setEnabled(bool enable)
{
  if (enable == isEnabled()) { return; }
  if (!enable) {
    g_source_remove(std::exchange(m_source, 0));
    return;
  }
  const auto poll = m_type == Read ? G_IO_IN : G_IO_OUT;
  m_source = g_unix_fd_add(m_fd, static_cast<GIOCondition>(poll | G_IO_ERR | G_IO_HUP | G_IO_NVAL),
    [](gint fd, GIOCondition, gpointer data) -> gboolean {
      static_cast<FdWatch*>(data)->activated(fd);
      return G_SOURCE_CONTINUE;
    }, this);
}

Log::Log(GLogLevelFlags level, const char* domain) : m_state(std::make_shared<State>(level, domain)) {}

Log::State::~State()
{
  g_log(domain, level, "%s", message.str().c_str());
}
}
