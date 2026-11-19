// SPDX-License-Identifier: MIT
#pragma once

#include "deviceinput.h"
#include "device-defs.h"

#include <nlohmann/json.hpp>
#include <set>

class DeviceConnection;
class Settings;
class Spotlight;

namespace native {
inline constexpr auto preferencesPath = "/org/projecteur/Projecteur/Preferences";
inline constexpr auto preferencesInterface = "org.projecteur.Projecteur.Preferences";
extern const char* const preferencesXml;

class Preferences : public Object
{
public:
  Preferences(GDBusConnection* bus, Settings& settings, Spotlight& spotlight,
              const bool& overlayAvailable, Timer& preview, bool minimizeOnly);
  ~Preferences() override;
  void present(const QString& command);
  void dispatch(const QString& method, GVariant* parameters, GDBusMethodInvocation* invocation);
  void scheduleUpdate();

private:
  QString getState() const;
  bool setOverlay(const QString& key, const QString& value);
  bool preview();
  bool setDeviceOption(const QString& key, const QString& option, int value);
  QString getMappings(const QString& key);
  bool updateMapping(const QString& key, const QString& payload);
  void recordInput(const QString& key, GDBusMethodInvocation* invocation);
  void finishRecording(bool accepted);
  void watchDevice(const DeviceId& id);
  void watchSubDevice(const DeviceId& id, const QString& path);
  std::shared_ptr<DeviceConnection> findDevice(const QString& key) const;

  GDBusConnection* m_bus;
  Settings* m_settings;
  Spotlight* m_spotlight;
  const bool& m_overlayAvailable;
  Timer* m_previewTimer;
  bool m_minimizeOnly;
  Timer m_updateTimer;
  Timer m_recordTimer;
  std::unique_ptr<Object> m_recordConnections;
  WeakObject<InputMapper> m_recorder{nullptr};
  KeyEventSequence m_recorded;
  GDBusMethodInvocation* m_recordRequest = nullptr;
  guint m_recordOwner = 0;
  std::set<const void*> m_watchedHidpp;
};
}
