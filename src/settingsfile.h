// SPDX-License-Identifier: MIT
#pragma once

#ifdef PROJECTEUR_NATIVE_GNOME
#include "native/settings-value.h"
#else
#include <QByteArray>
#include <QList>
#include <QStringList>
#include <QVariant>
#endif

// Projecteur's existing configuration uses KDE-style INI groups, escaped bytes
// and RGB(A) values. Keep those on disk so either desktop edition can use it.
class SettingsFile
{
public:
#ifdef PROJECTEUR_NATIVE_GNOME
  using Value = native::Setting;
#else
  using Value = QVariant;
#endif
  explicit SettingsFile(const QString& fileName);

  const QString& fileName() const { return m_fileName; }
  QStringList groupList() const;
  Value read(const QString& path, const Value& fallback = {}) const;
  bool contains(const QString& path) const;

  bool write(const QString& path, const Value& value);
#ifdef PROJECTEUR_NATIVE_GNOME
  bool write(const std::vector<std::pair<QString, Value>>& values);
#endif
  bool remove(const QString& path);

private:
  bool readFile(QList<QByteArray>& lines) const;
  bool update(const QString& path, const Value* value);
  bool commit(QList<QByteArray>&& edited, const QList<QByteArray>& latest);

  QString m_fileName;
  QList<QByteArray> m_lines;
};
