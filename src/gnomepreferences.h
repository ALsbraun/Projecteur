// SPDX-License-Identifier: MIT
#pragma once

#include "deviceinput.h"

#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QPointer>
#include <QTimer>

class DeviceConnection;
class GnomeOverlay;
class Settings;
class Spotlight;
struct DeviceId;

class GnomePreferences final : public QObject, protected QDBusContext
{
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.projecteur.Projecteur.Preferences")

public:
  GnomePreferences(Settings* settings, Spotlight* spotlight, GnomeOverlay* overlay,
                   QTimer* previewTimer, bool minimizeOnly, QObject* parent);
  bool registerObject();
  void present(const QString& command);

public slots:
  Q_SCRIPTABLE QString GetState();
  Q_SCRIPTABLE bool SetOverlay(const QString& key, const QString& json);
  Q_SCRIPTABLE void ResetOverlay();
  Q_SCRIPTABLE bool Preview();
  Q_SCRIPTABLE bool SavePreset(const QString& name);
  Q_SCRIPTABLE bool DeletePreset(const QString& name);
  Q_SCRIPTABLE void RetryDevices();
  Q_SCRIPTABLE bool SetDeviceOption(const QString& device, const QString& option, int value);
  Q_SCRIPTABLE QString GetMappings(const QString& device);
  Q_SCRIPTABLE bool UpdateMapping(const QString& device, const QString& json);
  Q_SCRIPTABLE QString RecordInput(const QString& device);
  Q_SCRIPTABLE void CancelRecording();

signals:
  void StateChanged();

private slots:
  void scheduleUpdate();

private:
  std::shared_ptr<DeviceConnection> findDevice(const QString& key);
  void watchDevice(const DeviceId& id);
  void watchSubDevice(const DeviceId& id, const QString& name, const QString& path);
  void finishRecording(bool accepted);

  Settings* const m_settings;
  Spotlight* const m_spotlight;
  GnomeOverlay* const m_overlay;
  QTimer* const m_previewTimer;
  const bool m_minimizeOnly;
  QTimer m_updateTimer;
  QTimer m_recordTimer;
  QDBusServiceWatcher m_recordOwner;
  QDBusMessage m_recordRequest;
  std::unique_ptr<QObject> m_recordConnections;
  QPointer<InputMapper> m_recorder;
  KeyEventSequence m_recorded;
  bool m_registered = false;
};
