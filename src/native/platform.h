// SPDX-License-Identifier: MIT
#pragma once

#include <glib-unix.h>
#include <gio/gio.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace native {

// UTF-8 strings used by the shared Linux device code.
template<typename T>
struct List : std::vector<T>
{
  using std::vector<T>::vector;
  bool isEmpty() const { return this->empty(); }
  const T& first() const { return this->front(); }
  const T& constFirst() const { return this->front(); }
  const T& constLast() const { return this->back(); }
  void removeLast() { this->pop_back(); }
  T takeLast() { auto last = this->back(); this->pop_back(); return last; }
  bool contains(const T& value) const
  { return std::find(this->begin(), this->end(), value) != this->end(); }
  List& operator<<(const T& value) { this->push_back(value); return *this; }
  void insert(size_t at, const T& value)
  { std::vector<T>::insert(this->begin() + at, value); }

  T join(const T& separator) const
  {
    T result;
    for (size_t i = 0; i < this->size(); ++i) {
      if (i) { result += separator; }
      result += (*this)[i];
    }
    return result;
  }
};

class Text : public std::string
{
public:
  using std::string::string;
  Text() = default;
  Text(const std::string& text) : std::string(text) {}
  Text(std::string&& text) : std::string(std::move(text)) {}
  const char* constData() const { return c_str(); }
  const std::string& toStdString() const { return *this; }
  const Text& toLocal8Bit() const { return *this; }
  const Text& toUtf8() const { return *this; }
  bool isEmpty() const { return empty(); }
  bool startsWith(char ch) const { return !empty() && front() == ch; }
  bool startsWith(const Text& prefix) const { return compare(0, prefix.size(), prefix) == 0; }
  bool endsWith(const Text& suffix) const
  { return size() >= suffix.size() && compare(size() - suffix.size(), suffix.size(), suffix) == 0; }
  bool contains(char ch) const { return find(ch) != npos; }
  bool contains(const Text& value) const { return find(value) != npos; }
  Text mid(size_t at, size_t count = npos) const { return at < size() ? substr(at, count) : Text{}; }
  Text left(size_t count) const { return substr(0, count); }
  void chop(size_t count) { resize(size() - std::min(size(), count)); }
  ptrdiff_t indexOf(char ch, size_t at = 0) const
  {
    const auto index = find(ch, at);
    return index == npos ? -1 : static_cast<ptrdiff_t>(index);
  }
  Text trimmed() const
  {
    const auto first = find_first_not_of(" \t\r\n\f\v");
    const auto last = find_last_not_of(" \t\r\n\f\v");
    return first == npos ? Text{} : mid(first, last - first + 1);
  }
  Text toLower() const
  {
    Text result = *this;
    for (auto& ch : result) { ch = g_ascii_tolower(ch); }
    return result;
  }
  Text toUpper() const
  {
    Text result = *this;
    for (auto& ch : result) { ch = g_ascii_toupper(ch); }
    return result;
  }
  List<Text> split(char separator, bool skipEmpty = false) const
  {
    List<Text> result;
    size_t start = 0;
    do {
      const auto end = find(separator, start);
      auto part = mid(start, end - start);
      if (!skipEmpty || !part.empty()) { result.push_back(std::move(part)); }
      if (end == npos) { break; }
      start = end + 1;
    } while (start <= size());
    return result;
  }
  template<typename Number>
  static Text number(Number value, int base = 10)
  {
    std::ostringstream output;
    if (base == 16) { output << std::hex; }
    if constexpr (std::is_integral_v<Number>) { output << +value; }
    else { output << value; }
    return output.str();
  }
  Text rightJustified(size_t width, char fill) const
  {
    Text result = *this;
    if (const auto length = result.size(); length < width) {
      result.insert(0, width - length, fill);
    }
    return result;
  }
  Text arg(const Text& value) const;
  Text arg(const char* value) const { return arg(Text(value)); }
  template<typename Number, std::enable_if_t<std::is_arithmetic_v<Number>, int> = 0>
  Text arg(Number value, int width = 0, int base = 10, char fill = ' ') const
  { return arg(number(value, base).rightJustified(std::max(0, width), fill)); }
  template<typename... Rest>
  Text arg(const Text& first, const Text& second, const Rest&... rest) const
  { return arg(first).arg(second, rest...); }

  Text& append(char ch) { push_back(ch); return *this; }
  Text& append(const Text& other) { std::string::append(other); return *this; }
  Text toHex() const;
  uint64_t toULongLong(bool* ok = nullptr, int base = 10) const;
  uint16_t toUShort(bool* ok = nullptr, int base = 10) const;
  unsigned toUInt(bool* ok = nullptr, int base = 10) const;
  int toInt(bool* ok = nullptr, int base = 10) const;
  double toDouble(bool* ok = nullptr) const;

  static Text fromRawData(const char* data, size_t size) { return Text(data, size); }
  static Text fromUtf8(const Text& text) { return text; }
  static Text fromLatin1(const char* text) { return text; }
  static Text fromLatin1(const Text& text);
  static Text fromLocal8Bit(const char* text) { return text; }
};

class Object;

template<typename... Args>
class Signal
{
public:
  using Callback = std::function<void(Args...)>;

  void add(Callback callback) { m_callbacks->push_back(std::move(callback)); }
  void operator()(Args... args) const
  {
    // Slots may change the signal or destroy the sender during delivery.
    const auto callbacks = *m_callbacks;
    for (const auto& callback : callbacks) { callback(args...); }
  }

private:
  std::unique_ptr<std::vector<Callback>> m_callbacks = std::make_unique<std::vector<Callback>>();
};

class Object
{
public:
  explicit Object(Object* parent = nullptr);
  virtual ~Object();
  Object(const Object&) = delete;
  Object& operator=(const Object&) = delete;

  Signal<> destroyed;
  std::weak_ptr<int> lifetime() const { return m_lifetime; }

private:
  Object* m_parent;
  std::vector<Object*> m_children;
  std::shared_ptr<int> m_lifetime = std::make_shared<int>(0);
};

template<typename T>
class WeakObject
{
public:
  explicit WeakObject(T* pointer) : m_pointer(pointer)
  { if (pointer) { m_lifetime = pointer->lifetime(); } }
  bool isNull() const { return m_lifetime.expired(); }
  explicit operator bool() const { return !isNull(); }
  T* data() const { return isNull() ? nullptr : m_pointer; }
  T* operator->() const { return data(); }

private:
  T* m_pointer;
  std::weak_ptr<int> m_lifetime;
};

template<typename Sender, typename Owner, typename... Args, typename Callback>
void connect(Sender* sender, Signal<Args...> Owner::*signal, Callback callback)
{
  (sender->*signal).add(std::move(callback));
}

template<typename Sender, typename Owner, typename... Args, typename Receiver, typename Callback>
void connect(Sender* sender, Signal<Args...> Owner::*signal, Receiver* receiver, Callback callback)
{
  (sender->*signal).add([alive = receiver->lifetime(), receiver,
                         callback = std::move(callback)](Args... args) mutable {
    if (alive.expired()) { return; }
    if constexpr (std::is_member_function_pointer_v<Callback>) {
      if constexpr (std::is_invocable_v<Callback, Receiver*, Args...>) {
        std::invoke(callback, receiver, args...);
      } else {
        std::invoke(callback, receiver);
      }
    } else if constexpr (std::is_member_object_pointer_v<Callback>) {
      (receiver->*callback)(args...);
    } else if constexpr (std::is_invocable_v<Callback, Args...>) {
      callback(args...);
    } else {
      callback();
    }
  });
}

void schedule(unsigned milliseconds, std::function<void()> callback);

class Timer : public Object
{
public:
  using Object::Object;
  ~Timer() override { stop(); }

  Signal<> timeout;
  void start();
  void start(int interval) { m_interval = interval; start(); }
  void stop();
  bool isActive() const { return m_source != 0; }
  int interval() const { return m_interval; }
  void setInterval(int interval)
  { m_interval = interval; if (isActive()) { start(); } }
  void setSingleShot(bool singleShot) { m_singleShot = singleShot; }
  void setTimerType(int) {}

  template<typename Receiver, typename Callback>
  static void singleShot(int milliseconds, Receiver* receiver, Callback callback)
  {
    schedule(milliseconds, [alive = receiver->lifetime(), receiver,
                             callback = std::move(callback)]() mutable {
      if (alive.expired()) { return; }
      if constexpr (std::is_member_function_pointer_v<Callback>) {
        std::invoke(callback, receiver);
      } else {
        callback();
      }
    });
  }

private:
  guint m_source = 0;
  int m_interval = 0;
  bool m_singleShot = false;
};

class FdWatch : public Object
{
public:
  enum Type { Read, Write };

  FdWatch(int fd, Type type, Object* parent = nullptr);
  ~FdWatch() override { setEnabled(false); }

  Signal<int> activated;
  bool isEnabled() const { return m_source != 0; }
  void setEnabled(bool enable);
  int socket() const { return m_fd; }

private:
  int m_fd;
  Type m_type;
  guint m_source = 0;
};

class ElapsedTimer
{
public:
  void start() { m_started = g_get_monotonic_time(); }
  void restart() { start(); }
  bool isValid() const { return m_started != 0; }
  void invalidate() { m_started = 0; }
  int64_t elapsed() const { return (g_get_monotonic_time() - m_started) / 1000; }

private:
  gint64 m_started = 0;
};

class Log
{
public:
  Log(GLogLevelFlags level, const char* domain);
  Log& noquote() { return *this; }
  Log& nospace() { m_state->space = false; return *this; }

  template<typename T>
  Log& operator<<(const T& value)
  {
    if (m_state->space && m_state->message.tellp() > 0) { m_state->message << ' '; }
    m_state->message << value;
    return *this;
  }

private:
  struct State {
    State(GLogLevelFlags level, const char* domain) : level(level), domain(domain) {}
    ~State();
    GLogLevelFlags level;
    const char* domain;
    bool space = true;
    std::ostringstream message;
  };
  std::shared_ptr<State> m_state;
};
}

// Only the shared hardware code uses these source aliases. The GNOME service
// uses the GLib main loop, UTF-8, and standard C++ throughout.
using QObject = native::Object;
using QTimer = native::Timer;
using QSocketNotifier = native::FdWatch;
using QElapsedTimer = native::ElapsedTimer;
template<typename T> using QPointer = native::WeakObject<T>;
template<typename T, int> using QVarLengthArray = std::vector<T>;
using QString = native::Text;
using QByteArray = native::Text;
using QStringList = native::List<QString>;
template<typename T> using QList = native::List<T>;
using qsizetype = ptrdiff_t;
using quint16 = uint16_t;
using quint32 = uint32_t;
using quint64 = uint64_t;
using qint32 = int32_t;
using qint64 = int64_t;
using QDebug = native::Log;
struct QDebugStateSaver { explicit QDebugStateSaver(const QDebug&) {} };
#define Q_OBJECT
#define Q_ENUM(...)
#define Q_DECLARE_METATYPE(...)
#define Q_UNUSED(value) (void)value;
#define QStringLiteral(value) QString(value)
#define QLatin1String(value) QString(value)
#define QLatin1Char(value) (value)
#define qPrintable(value) ((value).c_str())
#define emit
using native::connect;
template<typename T> constexpr int qRegisterMetaType() { return 1; }
using std::max;
#define qMax std::max
namespace Qt {
enum { CoarseTimer, PreciseTimer };
inline constexpr bool SkipEmptyParts = true;
}
