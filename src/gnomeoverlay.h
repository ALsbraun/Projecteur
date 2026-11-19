// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QTimer>

class Settings;

// Only state crosses D-Bus. GNOME Shell owns cursor tracking and rendering so
// Wayland clients never need screen capture permission or global coordinates.
class GnomeOverlay final : public QObject
{
  Q_OBJECT
public:
  explicit GnomeOverlay(Settings* settings, QObject* parent = nullptr);
  bool available() const { return m_available; }
  void setActive(bool active);
  void stop();

signals:
  void availableChanged(bool available);
  void visibleChanged(bool visible);

private slots:
  void scheduleSync();
  void sync();

private:
  QString settingsJson() const;
  void setAvailable(bool available);
  Settings* m_settings;
  QTimer m_heartbeat;
  QTimer m_update;
  bool m_active = false;
  bool m_available = false;
  bool m_warned = false;
  bool m_stopping = false;
  quint64 m_sequence = 0;
};
