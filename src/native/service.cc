// SPDX-License-Identifier: MIT
#include "service.h"

#include "native-control-xml.h"
#include "projecteur-i18n.h"

namespace native {
namespace {
constexpr auto shellService = "org.projecteur.Gnome";
constexpr auto shellPath = "/org/projecteur/Gnome";

const char* batteryStatus(HIDPP::BatteryStatus status)
{
  using HIDPP::BatteryStatus;
  switch (status) {
    case BatteryStatus::Discharging: return "discharging";
    case BatteryStatus::Charging: return "charging";
    case BatteryStatus::AlmostFull: return "almost-full";
    case BatteryStatus::Full: return "full";
    case BatteryStatus::SlowCharging: return "slow-charging";
    case BatteryStatus::InvalidBattery: return "invalid-battery";
    case BatteryStatus::ThermalError: return "thermal-error";
    case BatteryStatus::ChargingError: return "charging-error";
    case BatteryStatus::Uninitialized: return "";
  }
  return "";
}

QString utf8(const QString& text)
{
  auto* valid = g_utf8_make_valid(text.data(), text.size());
  QString result = valid;
  g_free(valid);
  return result;
}
}

Service::Service(GDBusConnection* bus, GMainLoop* loop, const QString& config,
                 Spotlight::Options options, bool disableOverlay, bool minimizeOnly)
  : m_bus(bus), m_loop(loop), m_settings(config)
  , m_spotlight(nullptr, std::move(options), &m_settings)
  , m_commands(nullptr, &m_spotlight), m_timer(&m_settings, &m_spotlight, &m_commands)
  , m_preferences(bus, m_settings, m_spotlight, m_overlayAvailable, m_preview, minimizeOnly)
{
  m_settings.setOverlayDisabled(disableOverlay);
  m_preview.setSingleShot(true);
  m_preview.setInterval(10000);
  m_preview.timeout.add([this] { m_spotlight.setSpotActive(false); });
  m_overlayUpdate.setSingleShot(true);
  m_overlayUpdate.timeout.add([this] { syncOverlay(); });
  m_overlayHeartbeat.setInterval(2000);
  m_overlayHeartbeat.timeout.add([this] { syncOverlay(); });
  m_batteryPoll.setInterval(5 * 60 * 1000);
  m_batteryPoll.timeout.add([this] { updateBatteries(); });
  m_batteryPoll.start();

  m_settings.overlayChanged.add([this] {
    if (!m_currentPreset.empty()) {
      m_currentPreset.clear();
      publish("CurrentPreset", "currentPresetChanged");
    }
    scheduleOverlaySync();
  });
  m_settings.overlayDisabledChanged.add([this](bool disabled) {
    if (disabled) { m_spotlight.setSpotActive(false); }
    publish("OverlayEnabled", "overlayEnabledChanged");
    scheduleOverlaySync();
  });
  m_settings.presetLoaded.add([this](const QString& preset) {
    if (m_currentPreset == preset) { return; }
    m_currentPreset = preset;
    publish("CurrentPreset", "currentPresetChanged");
  });
  m_settings.presetsChanged.add([this] {
    if (!m_currentPreset.empty() && !m_settings.hasPreset(m_currentPreset)) {
      m_currentPreset.clear();
      publish("CurrentPreset", "currentPresetChanged");
    }
    publish("Presets", "presetsChanged");
  });

  m_spotlight.spotActiveChanged.add([this](bool active) {
    if (active) { m_overlayHeartbeat.start(); }
    else {
      m_overlayHeartbeat.stop();
      m_preview.stop();
    }
    publish("SpotlightActive", "spotlightActiveChanged");
    scheduleOverlaySync();
  });
  m_spotlight.pointerModeCycleRequested.add([this] { m_settings.cyclePointerMode(); });
  m_spotlight.deviceConnected.add([this](const DeviceId&, const QString& name) {
    deviceChanged();
    notify(i18n("Presenter connected"), i18n("%1 is ready.", name), "input-mouse");
  });
  m_spotlight.deviceDisconnected.add([this](const DeviceId&, const QString& name) {
    deviceChanged();
    m_batteryWarnings.erase(name);
    notify(i18n("Presenter disconnected"), i18n("%1 is no longer available.", name), "input-mouse");
  });
  m_spotlight.deviceAccessError.add([this](const QString& name, const QString& path) {
    if (!m_accessErrors.emplace(path).second) { return; }
    notify(i18n("Presenter access failed"),
      i18n("%1 cannot access %2. Check the installed udev rules and device permissions.", name, path),
      "dialog-warning");
  });
  m_spotlight.subDeviceConnected.add([this](const DeviceId& id, const QString&, const QString& path) {
    m_accessErrors.erase(path);
    watchBattery(id, path);
  });
  m_spotlight.subDeviceDisconnected.add([this](const DeviceId&, const QString&, const QString&) {
    deviceChanged();
  });

  m_timer.enabledChanged.add([this](bool) { publish("TimerEnabled", "timerEnabledChanged"); });
  m_timer.durationSecondsChanged.add([this](int) {
    publish("TimerDurationSeconds", "timerDurationSecondsChanged");
  });
  m_timer.remainingSecondsChanged.add([this](int) {
    publish("TimerRemainingSeconds", "timerRemainingSecondsChanged");
  });
  m_timer.stateChanged.add([this](PresentationTimer::State state) {
    publish("TimerState", "timerStateChanged");
    if (state == PresentationTimer::State::Completed) {
      notify(i18n("Presentation timer finished"),
        i18n("The configured presentation time has elapsed."), "chronometer");
    }
  });
  for (const auto& info : m_spotlight.connectedDevices()) {
    const auto device = m_spotlight.deviceConnection(info.id);
    for (const auto& [path, connection] : device->subDevices()) { watchBattery(info.id, path); }
  }
}

Service::~Service()
{
  m_stopping = true;
  if (m_overlayWatcher) { g_bus_unwatch_name(m_overlayWatcher); }
  if (m_overlayAvailable) {
    g_dbus_connection_call(m_bus, shellService, shellPath, shellService, "SetState",
      g_variant_new("(bs)", false, "{}"), nullptr, G_DBUS_CALL_FLAGS_NO_AUTO_START,
      1000, nullptr, nullptr, nullptr);
  }
  if (m_controlId) { g_dbus_connection_unregister_object(m_bus, m_controlId); }
  if (m_preferencesId) { g_dbus_connection_unregister_object(m_bus, m_preferencesId); }
  // Device connections may emit signals while their HID++ state is restored.
  m_spotlight.shutdown();
  g_dbus_connection_flush_sync(m_bus, nullptr, nullptr);
}

bool Service::registerObjects()
{
  static const GDBusInterfaceVTable vtable = {
    [](GDBusConnection*, const char*, const char*, const char* interface, const char* method,
       GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data) {
      auto& self = *static_cast<Service*>(data);
      if (g_str_equal(interface, preferencesInterface)) {
        self.m_preferences.dispatch(method, parameters, invocation);
      } else {
        self.controlMethod(method, parameters, invocation);
      }
    },
    [](GDBusConnection*, const char*, const char*, const char*, const char* name, GError**,
       gpointer data) -> GVariant* {
      return static_cast<Service*>(data)->property(name);
    },
    nullptr, {}
  };
  const auto registerObject = [this](const char* path, const char* xml) {
    GError* error = nullptr;
    const auto info = g_dbus_node_info_new_for_xml(xml, &error);
    guint id = 0;
    if (info) {
      id = g_dbus_connection_register_object(m_bus, path, info->interfaces[0], &vtable,
        this, nullptr, &error);
      g_dbus_node_info_unref(info);
    }
    if (error) {
      g_warning("Could not register %s: %s", path, error->message);
      g_error_free(error);
    }
    return id;
  };
  m_controlId = registerObject(controlPath, controlXml);
  m_preferencesId = registerObject(preferencesPath, preferencesXml);
  if (!m_controlId || !m_preferencesId) { return false; }

  m_overlayWatcher = g_bus_watch_name_on_connection(m_bus, shellService, G_BUS_NAME_WATCHER_FLAGS_NONE,
    [](GDBusConnection*, const char*, const char*, gpointer data) {
      static_cast<Service*>(data)->scheduleOverlaySync();
    }, [](GDBusConnection*, const char*, gpointer data) {
      auto& self = *static_cast<Service*>(data);
      ++self.m_overlaySequence;
      self.setOverlayAvailable(false);
    }, this, nullptr);
  scheduleOverlaySync();
  return true;
}

void Service::quit()
{
  g_main_loop_quit(m_loop);
}

void Service::controlMethod(const QString& method, GVariant* parameters,
                            GDBusMethodInvocation* invocation)
{
  GVariant* reply = nullptr;
  gboolean flag = false;
  int seconds = 0;
  const char* name = nullptr;
  if (method == "SetOverlayEnabled") {
    g_variant_get(parameters, "(b)", &flag);
    m_settings.setOverlayDisabled(!flag);
  } else if (method == "SetSpotlightActive") {
    g_variant_get(parameters, "(b)", &flag);
    m_spotlight.setSpotActive(flag);
  } else if (method == "LoadPreset") {
    g_variant_get(parameters, "(&s)", &name);
    reply = g_variant_new("(b)", m_settings.loadPreset(name));
  } else if (method == "SetTimerEnabled") {
    g_variant_get(parameters, "(b)", &flag);
    m_timer.setEnabled(flag);
  } else if (method == "SetTimerDurationSeconds") {
    g_variant_get(parameters, "(i)", &seconds);
    m_timer.setDurationSeconds(seconds);
  } else if (method == "StartTimer") { m_timer.start(); }
  else if (method == "RestartTimer") { m_timer.restart(); }
  else if (method == "ResetTimer") { m_timer.reset(); }
  else if (method == "ShowPreferences") { present("--show"); }
  else if (method == "ShowAbout") { present("--about"); }
  else if (method == "Quit") { quit(); }
  else if (method == "ApplyCommands") {
    auto* commands = g_variant_get_child_value(parameters, 0);
    GVariantIter iter;
    g_variant_iter_init(&iter, commands);
    while (g_variant_iter_next(&iter, "&s", &name)) { applyCommand(QString(name).trimmed()); }
    g_variant_unref(commands);
  }
  g_dbus_method_invocation_return_value(invocation, reply);
}

GVariant* Service::property(const QString& name)
{
  if (name == "TrayVisible") { return g_variant_new_boolean(false); }
  if (name == "OverlayEnabled") { return g_variant_new_boolean(!m_settings.overlayDisabled()); }
  if (name == "SpotlightActive") { return g_variant_new_boolean(m_spotlight.spotActive()); }
  if (name == "TimerEnabled") { return g_variant_new_boolean(m_timer.enabled()); }
  if (name == "TimerState") { return g_variant_new_string(m_timer.stateName().c_str()); }
  if (name == "TimerDurationSeconds") { return g_variant_new_int32(m_timer.durationSeconds()); }
  if (name == "TimerRemainingSeconds") { return g_variant_new_int32(m_timer.remainingSeconds()); }
  if (name == "CurrentPreset") { return g_variant_new_string(utf8(m_currentPreset).c_str()); }

  const bool levels = name == "ConnectedDeviceBatteryLevels";
  GVariantBuilder values;
  g_variant_builder_init(&values, levels ? G_VARIANT_TYPE("ai") : G_VARIANT_TYPE_STRING_ARRAY);
  if (name == "Presets") {
    for (const auto& preset : m_settings.presets()) {
      g_variant_builder_add(&values, "s", utf8(preset).c_str());
    }
  } else {
    for (const auto& info : m_spotlight.connectedDevices()) {
      if (name == "ConnectedDevices") {
        g_variant_builder_add(&values, "s", utf8(info.name).c_str());
        continue;
      }
      HIDPP::BatteryInfo battery;
      const auto device = m_spotlight.deviceConnection(info.id);
      for (const auto& [path, sub] : device->subDevices()) {
        const auto* hidpp = dynamic_cast<const SubHidppConnection*>(sub.get());
        if (hidpp && hidpp->hasFlags(DeviceFlag::ReportBattery)
            && hidpp->batteryInfo().status != HIDPP::BatteryStatus::Uninitialized) {
          battery = hidpp->batteryInfo();
          break;
        }
      }
      if (levels) {
        g_variant_builder_add(&values, "i",
          battery.status == HIDPP::BatteryStatus::Uninitialized ? -1 : battery.currentLevel);
      } else {
        g_variant_builder_add(&values, "s", batteryStatus(battery.status));
      }
    }
  }
  return g_variant_builder_end(&values);
}

void Service::publish(const char* propertyName, const char* signal)
{
  if (!m_controlId || m_stopping) { return; }
  auto* value = g_variant_ref_sink(property(propertyName));
  g_dbus_connection_emit_signal(m_bus, nullptr, controlPath, serviceName, signal,
    g_variant_new_tuple(&value, 1), nullptr);
  GVariantBuilder changed;
  g_variant_builder_init(&changed, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&changed, "{sv}", propertyName, value);
  g_dbus_connection_emit_signal(m_bus, nullptr, controlPath, "org.freedesktop.DBus.Properties",
    "PropertiesChanged", g_variant_new("(sa{sv}as)", serviceName, &changed, nullptr), nullptr);
  g_variant_unref(value);
}

void Service::deviceChanged()
{
  publish("ConnectedDevices", "connectedDevicesChanged");
  publish("ConnectedDeviceBatteryLevels", "connectedDeviceBatteryLevelsChanged");
  publish("ConnectedDeviceBatteryStatuses", "connectedDeviceBatteryStatusesChanged");
}

void Service::watchBattery(const DeviceId& id, const QString& path)
{
  const auto device = m_spotlight.deviceConnection(id);
  if (!device) { return; }
  const auto sub = device->subDevice(path);
  auto* hidpp = dynamic_cast<SubHidppConnection*>(sub.get());
  if (!hidpp || !m_batteryConnections.emplace(hidpp).second) { return; }
  connect(hidpp, &Object::destroyed, this, [this, hidpp] { m_batteryConnections.erase(hidpp); });
  connect(hidpp, &SubHidppConnection::batteryInfoChanged, this,
    [this, name = device->deviceName()](const HIDPP::BatteryInfo& info) { batteryChanged(name, info); });
  connect(hidpp, &SubHidppConnection::featureSetInitialized, this,
    [this, hidpp, name = device->deviceName()] {
      if (hidpp->hasFlags(DeviceFlag::ReportBattery)) {
        batteryChanged(name, hidpp->batteryInfo());
        hidpp->triggerBattyerInfoUpdate();
      }
    });
  if (hidpp->hasFlags(DeviceFlag::ReportBattery)) { hidpp->triggerBattyerInfoUpdate(); }
}

void Service::batteryChanged(const QString& name, const HIDPP::BatteryInfo& info)
{
  if (m_stopping) { return; }
  publish("ConnectedDeviceBatteryLevels", "connectedDeviceBatteryLevelsChanged");
  publish("ConnectedDeviceBatteryStatuses", "connectedDeviceBatteryStatusesChanged");
  using HIDPP::BatteryStatus;
  if (info.status == BatteryStatus::Discharging && info.currentLevel <= 20) {
    if (m_batteryWarnings[name] != "low") {
      m_batteryWarnings[name] = "low";
      notify(i18n("Presenter battery low"), i18n("%1 has %2% battery remaining.", name, info.currentLevel),
        "battery-low");
    }
  } else if (info.status == BatteryStatus::InvalidBattery || info.status == BatteryStatus::ThermalError
             || info.status == BatteryStatus::ChargingError) {
    const auto status = batteryStatus(info.status);
    if (m_batteryWarnings[name] != status) {
      m_batteryWarnings[name] = status;
      notify(i18n("Presenter battery problem"), i18n("%1 reported a battery error: %2.", name, status),
        "dialog-warning");
    }
  } else {
    m_batteryWarnings.erase(name);
  }
}

void Service::updateBatteries()
{
  // A request can fail immediately and invoke a callback, so keep iteration
  // independent from changes to the connection registry.
  for (const auto& info : m_spotlight.connectedDevices()) {
    const auto device = m_spotlight.deviceConnection(info.id);
    for (const auto& [path, sub] : device->subDevices()) {
      auto* hidpp = dynamic_cast<SubHidppConnection*>(sub.get());
      if (hidpp && hidpp->hasFlags(DeviceFlag::ReportBattery)) { hidpp->triggerBattyerInfoUpdate(); }
    }
  }
}

void Service::notify(const QString& summary, const QString& body, const char* icon)
{
  if (m_stopping) { return; }
  g_dbus_connection_call(m_bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
    "org.freedesktop.Notifications", "Notify",
    g_variant_new("(susssasa{sv}i)", "Projecteur", 0u, icon, utf8(summary).c_str(), utf8(body).c_str(),
      nullptr, nullptr, -1), nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr, nullptr);
}

void Service::applyCommand(const QString& command)
{
  const auto equals = command.find('=');
  const auto key = command.left(equals).trimmed();
  const auto value = equals == QString::npos ? QString{} : command.mid(equals + 1).trimmed();
  if (key == "quit") { quit(); }
  else if (key == "spot" && !value.empty()) {
    const auto mode = value.toLower();
    m_spotlight.setSpotActive(mode == "toggle" ? !m_spotlight.spotActive()
      : mode == "on" || mode == "true" || mode == "1");
  } else if (key == "settings" || key == "preferences") {
    present(value.toLower() == "hide" || value == "0" ? "--hide" : "--show");
  } else if (key == "vibrate") {
    const auto args = value.split(',', true);
    const auto read = [&args](size_t index, int fallback, int max) {
      if (index >= args.size()) { return fallback; }
      bool valid = false;
      const auto number = args[index].toInt(&valid);
      return valid ? std::clamp(number, 0, max) : fallback;
    };
    m_commands.sendVibrateCommand(read(0, 128, 255), read(1, 0, 10));
  } else if (key == "spot.size.adjust") {
    bool valid = false;
    const auto delta = value.toInt(&valid);
    if (valid) {
      const auto size = m_settings.overlayState(true).at("spotSize").get<int>();
      m_settings.setOverlay("spotSize", std::clamp<int64_t>(int64_t{size} + delta, 5, 100));
    }
  } else { m_settings.applyCommand(command); }
}

void Service::scheduleOverlaySync()
{
  if (!m_stopping && !m_overlayUpdate.isActive()) { m_overlayUpdate.start(0); }
}

void Service::syncOverlay()
{
  const auto settings = m_settings.overlayState(false).dump(
    -1, ' ', false, nlohmann::json::error_handler_t::replace);
  struct Pending {
    WeakObject<Service> service;
    uint64_t sequence;
  };
  g_dbus_connection_call(m_bus, shellService, shellPath, shellService, "SetState",
    g_variant_new("(bs)", m_spotlight.spotActive() && !m_settings.overlayDisabled(), settings.c_str()),
    G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NO_AUTO_START, 1500, nullptr,
    [](GObject* source, GAsyncResult* result, gpointer data) {
      std::unique_ptr<Pending> pending(static_cast<Pending*>(data));
      GError* error = nullptr;
      auto* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
      if (reply) { g_variant_unref(reply); }
      auto* self = pending->service.data();
      if (self && !self->m_stopping && pending->sequence == self->m_overlaySequence) {
        self->setOverlayAvailable(error == nullptr);
        if (error && !self->m_overlayWarned) {
          self->m_overlayWarned = true;
          g_warning("GNOME overlay unavailable. Enable projecteur@projecteur.github.io. %s", error->message);
        }
      }
      g_clear_error(&error);
    }, new Pending{WeakObject<Service>(this), ++m_overlaySequence});
}

void Service::setOverlayAvailable(bool available)
{
  if (available) { m_overlayWarned = false; }
  if (available != m_overlayAvailable) {
    m_overlayAvailable = available;
    m_preferences.scheduleUpdate();
  }
}
}
