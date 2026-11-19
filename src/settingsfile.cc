// SPDX-License-Identifier: MIT
#include "settingsfile.h"

#ifdef PROJECTEUR_NATIVE_GNOME
#include "native/files.h"
#else
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#endif

#include <optional>

namespace {
struct Entry
{
  QStringList group;
  QString key;
};

Entry entryPath(const QString& path)
{
  auto parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  const auto key = parts.takeLast();
  if (parts.isEmpty()) { parts << QStringLiteral("General"); }
  return {parts, key};
}

QByteArray unescape(const QByteArray& input)
{
  QByteArray result;
  result.reserve(input.size());
  for (decltype(input.size()) i = 0; i < input.size(); ++i) {
    const auto ch = input[i];
    if (ch != '\\' || i + 1 == input.size()) {
      result.append(ch);
      continue;
    }
    switch (input[++i]) {
      case '\\': result.append('\\'); break;
      case 's': result.append(' '); break;
      case 't': result.append('\t'); break;
      case 'r': result.append('\r'); break;
      case 'n': result.append('\n'); break;
      case 'x': {
        bool ok = false;
        const auto byte = input.mid(i + 1, 2).toUInt(&ok, 16);
        if (ok && i + 2 < input.size()) {
          result.append(static_cast<char>(byte));
          i += 2;
        } else {
          result.append("\\x");
        }
        break;
      }
      default:
        result.append('\\');
        result.append(input[i]);
        break;
    }
  }
  return result;
}

QByteArray escape(const QByteArray& input, bool groupName = false)
{
  QByteArray result;
  for (decltype(input.size()) i = 0; i < input.size(); ++i) {
    const auto ch = static_cast<unsigned char>(input[i]);
    if (ch == '\\') {
      result.append("\\\\");
    } else if (ch == ' ' && (i == 0 || i + 1 == input.size())) {
      result.append("\\s");
    } else if (ch < 32 || ch == 127 || (!groupName && ch >= 128)
            || (groupName && (ch == '[' || ch == ']'))) {
      result.append("\\x" + QByteArray::number(ch, 16).rightJustified(2, '0'));
    } else {
      result.append(static_cast<char>(ch));
    }
  }
  return result;
}

bool readGroup(const QByteArray& line, QStringList& group, bool& immutable)
{
  if (!line.startsWith('[')) { return false; }
  group.clear();
  immutable = false;
  decltype(line.size()) start = 0;
  while (start < line.size() && line[start] == '[') {
    const auto end = line.indexOf(']', start + 1);
    if (end < 0) { break; }
    const auto name = line.mid(start + 1, end - start - 1);
    if (name == "$i") {
      immutable = true;
    } else {
      group << QString::fromUtf8(unescape(name));
    }
    start = static_cast<decltype(start)>(end) + 1;
  }
  return true;
}

QByteArray groupHeader(const QStringList& group)
{
  QByteArray header;
  for (const auto& name : group) {
    header.append('[').append(escape(name.toUtf8(), true)).append(']');
  }
  return header;
}

QString keyName(const QByteArray& line, qsizetype equals, bool& immutable)
{
  if (equals < 0 || line.startsWith('#') || line.startsWith(';')) { return {}; }
  auto raw = line.left(equals).trimmed();
  immutable = raw.endsWith("[$i]");
  if (immutable) { raw.chop(4); }
  return QString::fromUtf8(unescape(raw));
}

SettingsFile::Value decodedValue(const QByteArray& encoded, const SettingsFile::Value& fallback)
{
  const auto bytes = unescape(encoded);
  const auto text = QString::fromUtf8(bytes);
#ifdef PROJECTEUR_NATIVE_GNOME
  #define IS_TYPE(value, nativeType, qtType) std::holds_alternative<nativeType>(value)
  if (std::holds_alternative<QString>(fallback)) { return bytes; }
#else
  #define IS_TYPE(value, nativeType, qtType) (value.typeId() == QMetaType::qtType)
  if (fallback.typeId() == QMetaType::QByteArray) { return bytes; }
#endif
  if (IS_TYPE(fallback, bool, Bool)) {
    const auto normalized = bytes.toLower();
    if (normalized == "true" || normalized == "yes" || normalized == "on" || normalized == "1") {
      return true;
    }
    if (normalized == "false" || normalized == "no" || normalized == "off" || normalized == "0") {
      return false;
    }
    return fallback;
  }
  bool valid = false;
  if (IS_TYPE(fallback, int, Int)) {
    const auto result = bytes.toInt(&valid);
    return valid ? SettingsFile::Value(result) : fallback;
  }
  if (IS_TYPE(fallback, double, Double)) {
    const auto result = bytes.toDouble(&valid);
    return valid ? SettingsFile::Value(result) : fallback;
  }
  if (IS_TYPE(fallback, native::Color, QColor)) {
#ifdef PROJECTEUR_NATIVE_GNOME
    using Color = native::Color;
#else
    using Color = QColor;
#endif
    const auto channels = bytes.split(',');
    if (channels.size() == 3 || channels.size() == 4) {
      int rgba[] = {0, 0, 0, 255};
      for (decltype(channels.size()) i = 0; i < channels.size(); ++i) {
        rgba[i] = channels.at(i).toInt(&valid);
        if (!valid || rgba[i] < 0 || rgba[i] > 255) { return fallback; }
      }
      return Color{rgba[0], rgba[1], rgba[2], rgba[3]};
    }
    const auto color = Color::fromString(text);
    return color.isValid() ? SettingsFile::Value(color) : fallback;
  }
  return text;
}

QByteArray encodedValue(const SettingsFile::Value& value)
{
  if (IS_TYPE(value, native::Color, QColor)) {
#ifdef PROJECTEUR_NATIVE_GNOME
    const auto color = std::get<native::Color>(value);
#else
    const auto color = value.value<QColor>();
#endif
    auto bytes = QByteArray::number(color.red()) + ',' + QByteArray::number(color.green())
      + ',' + QByteArray::number(color.blue());
    if (color.alpha() != 255) { bytes += ',' + QByteArray::number(color.alpha()); }
    return bytes;
  }
#ifdef PROJECTEUR_NATIVE_GNOME
  if (const auto* bytes = std::get_if<QString>(&value)) { return escape(*bytes); }
  if (const auto* flag = std::get_if<bool>(&value)) { return *flag ? "true" : "false"; }
  if (const auto* number = std::get_if<int>(&value)) { return QByteArray::number(*number); }
  if (const auto* number = std::get_if<double>(&value)) {
    char text[G_ASCII_DTOSTR_BUF_SIZE];
    return g_ascii_dtostr(text, sizeof(text), *number);
  }
  return {};
#else
  return escape(value.toByteArray());
#endif
#undef IS_TYPE
}

std::optional<QByteArray> lookup(const QList<QByteArray>& lines, const Entry& entry)
{
  QStringList group;
  bool immutable = false;
  std::optional<QByteArray> value;
  for (const auto& raw : lines) {
    const auto line = raw.trimmed();
    if (readGroup(line, group, immutable) || group != entry.group) { continue; }
    const auto equals = line.indexOf('=');
    if (keyName(line, equals, immutable) == entry.key) {
      value = line.mid(equals + 1).trimmed();
    }
  }
  return value;
}
}

SettingsFile::SettingsFile(const QString& fileName)
  : m_fileName(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
      .absoluteFilePath(fileName.isEmpty() ? QStringLiteral("projecteurrc") : fileName))
{
  readFile(m_lines);
}

QStringList SettingsFile::groupList() const
{
  QStringList result;
  QStringList group;
  bool immutable = false;
  for (const auto& line : m_lines) {
    if (readGroup(line.trimmed(), group, immutable) && !group.isEmpty()
        && !result.contains(group.constFirst())) {
      result << group.constFirst();
    }
  }
  return result;
}

SettingsFile::Value SettingsFile::read(const QString& path, const SettingsFile::Value& fallback) const
{
  const auto value = lookup(m_lines, entryPath(path));
  return value ? decodedValue(*value, fallback) : fallback;
}

bool SettingsFile::contains(const QString& path) const
{
  return lookup(m_lines, entryPath(path)).has_value();
}

bool SettingsFile::write(const QString& path, const SettingsFile::Value& value)
{
  return update(path, &value);
}

bool SettingsFile::remove(const QString& path)
{
  return update(path, nullptr);
}

bool SettingsFile::readFile(QList<QByteArray>& lines) const
{
  QFile file(m_fileName);
  if (!file.exists()) { return true; }
  if (!file.open(QIODevice::ReadOnly)) { return false; }
  const auto contents = file.readAll();
  if (file.error() != QFileDevice::NoError) { return false; }
  lines = contents.split('\n');
  if (!lines.isEmpty() && lines.constLast().isEmpty()) { lines.removeLast(); }
  return true;
}

namespace {
bool edit(QList<QByteArray>& lines, const QString& path, const SettingsFile::Value* value)
{
  const bool removeGroup = !value && !path.contains(QLatin1Char('/'));
  const auto entry = removeGroup ? Entry{{path}, {}} : entryPath(path);
  const auto encoded = value ? entry.key.toUtf8() + '=' + encodedValue(*value) : QByteArray{};
  QStringList group;
  bool immutableGroup = false;
  qsizetype insertAt = -1;
  qsizetype replaceAt = -1;
  QList<QByteArray> edited;

  for (const auto& raw : lines) {
    const auto line = raw.trimmed();
    const bool header = readGroup(line, group, immutableGroup);
    if (header && group.isEmpty() && immutableGroup) { return false; }
    if (removeGroup && !group.isEmpty() && group.constFirst() == path) {
      if (immutableGroup) { return false; }
      continue;
    }
    if (group == entry.group) {
      if (immutableGroup) { return false; }
      bool immutableKey = false;
      if (!header && keyName(line, line.indexOf('='), immutableKey) == entry.key) {
        if (immutableKey) { return false; }
        if (!value) { continue; }
        replaceAt = edited.size();
      }
      insertAt = edited.size() + 1;
    }
    edited << raw;
  }

  if (value) {
    if (replaceAt >= 0) {
      edited[replaceAt] = encoded;
    } else if (insertAt >= 0) {
      edited.insert(insertAt, encoded);
    } else {
      if (!edited.isEmpty() && !edited.constLast().isEmpty()) { edited << QByteArray{}; }
      edited << groupHeader(entry.group) << encoded;
    }
  }
  lines = std::move(edited);
  return true;
}
}

bool SettingsFile::update(const QString& path, const Value* value)
{
  const auto directory = QFileInfo(m_fileName).absolutePath();
  if (!QDir().mkpath(directory)) { return false; }
  // Share the lock path with KConfig.
  QLockFile lock(m_fileName + QStringLiteral(".lock"));
  QList<QByteArray> latest;
  if (!lock.tryLock(100) || !readFile(latest)) { return false; }
  auto edited = latest;
  return edit(edited, path, value) && commit(std::move(edited), latest);
}

#ifdef PROJECTEUR_NATIVE_GNOME
bool SettingsFile::write(const std::vector<std::pair<QString, Value>>& values)
{
  const auto directory = QFileInfo(m_fileName).absolutePath();
  if (!QDir().mkpath(directory)) { return false; }
  QLockFile lock(m_fileName + QStringLiteral(".lock"));
  QList<QByteArray> latest;
  if (!lock.tryLock(100) || !readFile(latest)) { return false; }
  auto edited = latest;
  for (const auto& [path, value] : values) {
    if (!edit(edited, path, &value)) { return false; }
  }
  return commit(std::move(edited), latest);
}
#endif

bool SettingsFile::commit(QList<QByteArray>&& edited, const QList<QByteArray>& latest)
{
  if (edited != latest) {
    QByteArray contents;
    for (const auto& line : edited) { contents.append(line).append('\n'); }
#ifdef PROJECTEUR_NATIVE_GNOME
    if (!native::replaceFile(m_fileName, contents)) { return false; }
#else
    const bool newFile = !QFile::exists(m_fileName);
    QSaveFile file(m_fileName);
    if (!file.open(QIODevice::WriteOnly)
        || (newFile && !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        || file.write(contents) != contents.size() || !file.commit()) {
      return false;
    }
#endif
  }
  m_lines = std::move(edited);
  return true;
}
