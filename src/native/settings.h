// SPDX-License-Identifier: MIT
#pragma once

#include "deviceinput.h"
#include "device-defs.h"
#include "settingsfile.h"

#include <nlohmann/json.hpp>

class Settings : public native::Object
{
public:
  explicit Settings(const QString& fileName);

  struct Range { int min; int max; };
  static constexpr Range inputSequenceIntervalRange() { return {100, 950}; }
  const std::vector<QString>& presets() const { return m_presets; }
  bool hasPreset(const QString& name) const;
  bool loadPreset(const QString& name);
  bool savePreset(const QString& name);
  bool removePreset(const QString& name);
  const QString& configFileName() const { return m_file.fileName(); }

  nlohmann::json overlayState(bool preferences) const;
  bool setOverlay(const QString& key, const nlohmann::json& value);
  void resetOverlay();
  void cyclePointerMode();
  void applyCommand(const QString& command);

  bool overlayDisabled() const { return m_overlayDisabled; }
  void setOverlayDisabled(bool disabled);
  bool presentationTimerEnabled() const;
  void setPresentationTimerEnabled(bool enabled);
  int presentationTimerDurationSeconds() const;
  void setPresentationTimerDurationSeconds(int seconds);

  int deviceInputSeqInterval(const DeviceId& id) const;
  void setDeviceInputSeqInterval(const DeviceId& id, int interval);
  InputMapConfig getDeviceInputMapConfig(const DeviceId& id) const;
  void setDeviceInputMapConfig(const DeviceId& id, const InputMapConfig& config);
  int devicePresentationTimerHapticStrength(const DeviceId& id) const;
  void setDevicePresentationTimerHapticStrength(const DeviceId& id, int strength);

  native::Signal<const QString&> presetLoaded;
  native::Signal<> presetsChanged;
  native::Signal<> overlayChanged;
  native::Signal<bool> overlayDisabledChanged;

private:
  bool setValue(const QString& key, const native::Setting& value);
  std::map<QString, native::Setting> readOverlay(const QString& section) const;
  SettingsFile m_file;
  std::map<QString, native::Setting> m_overlay;
  std::vector<QString> m_presets;
  bool m_overlayDisabled = false;
};
