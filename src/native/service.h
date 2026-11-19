// SPDX-License-Identifier: MIT
#pragma once

#include "device-command-helper.h"
#include "device-hidpp.h"
#include "preferences.h"
#include "presentationtimer.h"
#include "settings.h"
#include "spotlight.h"

namespace native {
inline constexpr auto serviceName = "org.projecteur.Projecteur";
inline constexpr auto controlPath = "/org/projecteur/Projecteur/Control";

class Service : public Object
{
public:
  Service(GDBusConnection* bus, GMainLoop* loop, const QString& config,
          Spotlight::Options options, bool disableOverlay, bool minimizeOnly);
  ~Service() override;

  bool registerObjects();
  void quit();
  void present(const char* command) { m_preferences.present(command); }

private:
  friend class ModeCycleTest;

  void controlMethod(const QString& method, GVariant* parameters, GDBusMethodInvocation* invocation);
  GVariant* property(const QString& name);
  void publish(const char* property, const char* signal);
  void deviceChanged();
  void watchBattery(const DeviceId& id, const QString& path);
  void batteryChanged(const QString& name, const HIDPP::BatteryInfo& info);
  void updateBatteries();
  void notify(const QString& summary, const QString& body, const char* icon);
  void applyCommand(const QString& command);
  void scheduleOverlaySync();
  void syncOverlay();
  void setOverlayAvailable(bool available);

  GDBusConnection* m_bus;
  GMainLoop* m_loop;
  Settings m_settings;
  Spotlight m_spotlight;
  DeviceCommandHelper m_commands;
  PresentationTimer m_timer;

  bool m_overlayAvailable = false;
  bool m_overlayWarned = false;
  bool m_stopping = false;
  Timer m_preview;
  Timer m_overlayUpdate;
  Timer m_overlayHeartbeat;
  Timer m_batteryPoll;
  Preferences m_preferences;
  uint64_t m_overlaySequence = 0;
  guint m_overlayWatcher = 0;
  guint m_controlId = 0;
  guint m_preferencesId = 0;
  QString m_currentPreset;
  std::set<SubHidppConnection*> m_batteryConnections;
  std::set<QString> m_accessErrors;
  std::map<QString, QString> m_batteryWarnings;
};
}
