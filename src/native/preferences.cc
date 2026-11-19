// SPDX-License-Identifier: MIT
#include "preferences.h"

#include "device-hidpp.h"
#include "device-key-lookup.h"
#include "files.h"
#include "projecteur-GitVersion.h"
#include "settings.h"
#include "spotlight.h"

#include <cmath>
#include <linux/input.h>

namespace native {
namespace {
using Json = nlohmann::json;
constexpr int maxRecordedEvents = 8;

QString serialize(const Json& value)
{
  return value.dump(-1, ' ', false, Json::error_handler_t::replace);
}

QString deviceKey(const DeviceId& id)
{
  return QStringLiteral("%1:%2:%3:%4").arg(formatHexId(id.vendorId), formatHexId(id.productId))
    .arg(static_cast<int>(id.busType)).arg(id.phys);
}

bool supportsVibration(SubDeviceConnection& subDevice)
{
  if (subDevice.hasFlags(DeviceFlag::Vibrate)) { return true; }
  auto* hidpp = dynamic_cast<SubHidppConnection*>(&subDevice);
  return hidpp
    && (hidpp->featureSet().featureCodeSupported(HIDPP::FeatureCode::PresenterControl)
        || hidpp->featureSet().featureCodeSupported(HIDPP::FeatureCode::Haptic));
}

Json encodeSequence(const KeyEventSequence& sequence)
{
  auto result = Json::array();
  for (const auto& frame : sequence) {
    auto events = Json::array();
    for (const auto& event : frame) {
      events.push_back(Json::array({event.type, event.code, event.value}));
    }
    result.push_back(std::move(events));
  }
  return result;
}

int integer(const Json& value, int fallback = 0)
{
  if (!value.is_number()) { return fallback; }
  const auto number = value.get<double>();
  return number >= INT32_MIN && number <= INT32_MAX && number == std::trunc(number)
    ? static_cast<int>(number) : fallback;
}

const Json& field(const Json& object, const char* key)
{
  static const Json empty;
  if (!object.is_object()) { return empty; }
  const auto it = object.find(key);
  return it == object.end() ? empty : *it;
}

bool decodeSequence(const Json& value, KeyEventSequence& result)
{
  if (!value.is_array() || value.size() > 64) { return false; }
  for (const auto& frame : value) {
    if (!frame.is_array() || frame.empty() || frame.size() > 64) { return false; }
    KeyEvent events;
    for (const auto& event : frame) {
      if (!event.is_array() || event.size() != 3) { return false; }
      const int type = integer(event[0], -1);
      const int code = integer(event[1], -1);
      const int value = integer(event[2]);
      if (type < 0 || type > UINT16_MAX || code < 0 || code > UINT16_MAX || event[2] != value) {
        return false;
      }
      events.emplace_back(type, code, value);
    }
    result.push_back(std::move(events));
  }
  return true;
}

QString sequenceLabel(const DeviceId& id, const KeyEventSequence& sequence)
{
  for (const auto& [key, info] : SpecialKeys::keyEventSequenceMap()) {
    Q_UNUSED(key);
    if (sequence == info.keyEventSeq) { return info.name; }
  }
  QStringList parts;
  for (const auto& frame : sequence) {
    for (const auto& event : frame) {
      if (event.type == EV_SYN || event.type == EV_MSC) { continue; }
      const auto& name = KeyName::lookup(id, event);
      parts.push_back((name.isEmpty() ? QString::number(event.code, 16) : name)
        + (event.value ? QStringLiteral(" ↓") : QStringLiteral(" ↑")));
    }
  }
  return parts.join(QStringLiteral("  "));
}

using Type = Action::Type;
bool isMoveAction(Type type)
{
  return type == Type::ScrollHorizontal || type == Type::ScrollVertical || type == Type::VolumeControl;
}

struct ModifierKey {
  int code;
  uint16_t native;
  Qt::KeyboardModifier qt;
};

constexpr ModifierKey modifierKeys[] = {
  {KEY_LEFTCTRL, NativeKeySequence::LeftCtrl, Qt::ControlModifier},
  {KEY_RIGHTCTRL, NativeKeySequence::RightCtrl, Qt::ControlModifier},
  {KEY_LEFTALT, NativeKeySequence::LeftAlt, Qt::AltModifier},
  {KEY_RIGHTALT, NativeKeySequence::RightAlt, Qt::AltModifier},
  {KEY_LEFTSHIFT, NativeKeySequence::LeftShift, Qt::ShiftModifier},
  {KEY_RIGHTSHIFT, NativeKeySequence::RightShift, Qt::ShiftModifier},
  {KEY_LEFTMETA, NativeKeySequence::LeftMeta, Qt::MetaModifier},
  {KEY_RIGHTMETA, NativeKeySequence::RightMeta, Qt::MetaModifier},
};

std::shared_ptr<Action> shortcutAction(const Json& value)
{
  if (value == QStringLiteral("alt-tab")) {
    return std::make_shared<KeySequenceAction>(NativeKeySequence::predefined::altTab());
  }
  if (value == QStringLiteral("alt-f4")) {
    return std::make_shared<KeySequenceAction>(NativeKeySequence::predefined::altF4());
  }
  if (value == QStringLiteral("super")) {
    return std::make_shared<KeySequenceAction>(NativeKeySequence::predefined::meta());
  }
  if (!value.is_array() || value.empty() || value.size() > 4) { return {}; }
  std::vector<int> qtKeys;
  std::vector<uint16_t> nativeModifiers;
  KeyEventSequence events;
  for (const auto& chord : value) {
    const auto& symbolField = field(chord, "symbol");
    if (!symbolField.is_string()) { return {}; }
    const QString symbol = symbolField.get<std::string>();
    const int key = integer(field(chord, "code"));
    if (key <= 0 || key > KEY_MAX || symbol.isEmpty() || symbol.size() > 64) { return {}; }
    int qt = QKeySequence::fromString(symbol)[0].key();
    uint16_t native = 0;
    KeyEvent press;
    KeyEvent release;
    const auto& modifiers = field(chord, "modifiers");
    if (!modifiers.is_array() || modifiers.size() > std::size(modifierKeys)) { return {}; }
    for (const auto& item : modifiers) {
      const int code = integer(item);
      const auto modifier = std::find_if(std::begin(modifierKeys), std::end(modifierKeys),
        [code](const auto& value) { return value.code == code; });
      if (modifier == std::end(modifierKeys) || (native & modifier->native)) { return {}; }
      native |= modifier->native;
      qt |= modifier->qt;
      press.emplace_back(EV_KEY, code, 1);
      release.emplace_back(EV_KEY, code, 0);
    }
    press.emplace_back(EV_KEY, key, 1);
    release.insert(release.begin(), DeviceInputEvent(EV_KEY, key, 0));
    press.emplace_back(EV_SYN, SYN_REPORT, 0);
    release.emplace_back(EV_SYN, SYN_REPORT, 0);
    qtKeys.push_back(qt);
    nativeModifiers.push_back(native);
    events.push_back(std::move(press));
    events.push_back(std::move(release));
  }
  return std::make_shared<KeySequenceAction>(
    NativeKeySequence(qtKeys, std::move(nativeModifiers), std::move(events)));
}
}

const char* const preferencesXml = R"xml(
<node><interface name="org.projecteur.Projecteur.Preferences">
  <method name="GetState"><arg type="s" direction="out"/></method>
  <method name="SetOverlay"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="b" direction="out"/></method>
  <method name="ResetOverlay"/>
  <method name="Preview"><arg type="b" direction="out"/></method>
  <method name="SavePreset"><arg type="s" direction="in"/><arg type="b" direction="out"/></method>
  <method name="DeletePreset"><arg type="s" direction="in"/><arg type="b" direction="out"/></method>
  <method name="RetryDevices"/>
  <method name="SetDeviceOption"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="i" direction="in"/><arg type="b" direction="out"/></method>
  <method name="GetMappings"><arg type="s" direction="in"/><arg type="s" direction="out"/></method>
  <method name="UpdateMapping"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="b" direction="out"/></method>
  <method name="RecordInput"><arg type="s" direction="in"/><arg type="s" direction="out"/></method>
  <method name="CancelRecording"/>
  <signal name="StateChanged"/>
</interface></node>
)xml";

Preferences::Preferences(GDBusConnection* bus, Settings& settings, Spotlight& spotlight,
                         const bool& overlayAvailable, Timer& preview, bool minimizeOnly)
  : m_bus(bus), m_settings(&settings), m_spotlight(&spotlight), m_overlayAvailable(overlayAvailable)
  , m_previewTimer(&preview), m_minimizeOnly(minimizeOnly)
{
  m_updateTimer.setSingleShot(true);
  m_updateTimer.timeout.add([this] {
    g_dbus_connection_emit_signal(m_bus, nullptr, preferencesPath, preferencesInterface,
      "StateChanged", nullptr, nullptr);
  });
  m_recordTimer.setSingleShot(true);
  m_recordTimer.setInterval(10000);
  m_recordTimer.timeout.add([this] { finishRecording(false); });
  connect(&settings, &Settings::overlayChanged, this, &Preferences::scheduleUpdate);
  connect(&settings, &Settings::overlayDisabledChanged, this, &Preferences::scheduleUpdate);
  connect(&spotlight, &Spotlight::spotActiveChanged, this, &Preferences::scheduleUpdate);
  connect(&spotlight, &Spotlight::inputDevicesChanged, this, &Preferences::scheduleUpdate);
  connect(&spotlight, &Spotlight::deviceConnected, this, [this](const DeviceId& id, const QString&) {
    watchDevice(id);
    scheduleUpdate();
  });
  connect(&spotlight, &Spotlight::subDeviceConnected, this,
    [this](const DeviceId& id, const QString&, const QString& path) {
      watchSubDevice(id, path);
      scheduleUpdate();
    });
  connect(&spotlight, &Spotlight::deviceDisconnected, this, &Preferences::scheduleUpdate);
  connect(&spotlight, &Spotlight::subDeviceDisconnected, this, &Preferences::scheduleUpdate);
  for (const auto& info : spotlight.connectedDevices()) { watchDevice(info.id); }
}

Preferences::~Preferences()
{
  finishRecording(false);
}

void Preferences::present(const QString& command)
{
  const QString argument = m_minimizeOnly && command == "--hide" ? "--minimize-existing" : command;
  const auto directory = executableDirectory();
  QString script = directory + "/gnome-preferences/main.js";
  if (!File::exists(script)) { script = directory + "/" PROJECTEUR_PREFERENCES_RELPATH "/main.js"; }
  const char* argv[] = {"gjs", "-m", script.c_str(), argument.c_str(), nullptr};
  GError* error = nullptr;
  auto* process = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_NONE, &error);
  if (process) { g_object_unref(process); }
  if (error) {
    g_warning("Could not start Projecteur preferences: %s", error->message);
    g_error_free(error);
  }
}

void Preferences::scheduleUpdate()
{
  if (!m_updateTimer.isActive()) { m_updateTimer.start(0); }
}

void Preferences::watchDevice(const DeviceId& id)
{
  const auto device = m_spotlight->deviceConnection(id);
  if (!device) { return; }
  connect(device.get(), &DeviceConnection::subDeviceFlagsChanged, this, &Preferences::scheduleUpdate);
  connect(device->inputMapper().get(), &InputMapper::configurationChanged, this, &Preferences::scheduleUpdate);
  for (const auto& [path, subDevice] : device->subDevices()) {
    watchSubDevice(id, path);
  }
}

void Preferences::watchSubDevice(const DeviceId& id, const QString& path)
{
  const auto device = m_spotlight->deviceConnection(id);
  const auto subDevice = device ? device->subDevice(path) : nullptr;
  if (auto* hidpp = dynamic_cast<SubHidppConnection*>(subDevice.get())) {
    if (!m_watchedHidpp.emplace(hidpp).second) { return; }
    hidpp->destroyed.add([alive = lifetime(), this, hidpp] {
      if (!alive.expired()) { m_watchedHidpp.erase(hidpp); }
    });
    connect(hidpp, &SubHidppConnection::receiverStateChanged, this, &Preferences::scheduleUpdate);
    connect(hidpp, &SubHidppConnection::presenterStateChanged, this, &Preferences::scheduleUpdate);
    connect(hidpp, &SubHidppConnection::featureSetInitialized, this, &Preferences::scheduleUpdate);
  }
}

std::shared_ptr<DeviceConnection> Preferences::findDevice(const QString& key) const
{
  for (const auto& device : m_spotlight->connectedDevices()) {
    if (deviceKey(device.id) == key) { return m_spotlight->deviceConnection(device.id); }
  }
  return {};
}

QString Preferences::getState() const
{
  auto devices = Json::array();
  for (const auto& info : m_spotlight->connectedDevices()) {
    const auto device = m_spotlight->deviceConnection(info.id);
    if (!device) { continue; }
    bool vibration = false;
    auto subDevices = Json::array();
    auto hidppDevices = Json::array();
    for (const auto& [path, subDevice] : device->subDevices()) {
      vibration = vibration || supportsVibration(*subDevice);
      if (const auto* hidpp = dynamic_cast<const SubHidppConnection*>(subDevice.get())) {
        const auto protocol = hidpp->protocolVersion();
        hidppDevices.push_back({
          {"presenter", toString(hidpp->presenterState(), false)},
          {"receiver", hidpp->busType() == BusType::Usb ? toString(hidpp->receiverState(), false) : ""},
          {"protocol", QString("%1.%2").arg(protocol.major).arg(protocol.minor).toStdString()},
        });
      }
      auto flags = Json::array();
      for (const auto& flag : toStringList(subDevice->flags(), false)) {
        flags.push_back(flag.toStdString());
      }
      subDevices.push_back({
        {"path", path.toStdString()}, {"mode", toString(subDevice->mode(), false)}, {"flags", flags},
      });
    }
    devices.push_back({
      {"id", deviceKey(info.id).toStdString()}, {"name", info.name.toStdString()},
      {"vendor", formatHexId(info.id.vendorId).toStdString()},
      {"product", formatHexId(info.id.productId).toStdString()},
      {"bus", toString(info.id.busType, false)}, {"physicalPath", info.id.phys.toStdString()},
      {"subDevices", subDevices}, {"hidpp", hidppDevices},
      {"inputAvailable", device->inputMapper()->hasVirtualDevice()},
      {"inputStatus", m_spotlight->inputMappingStatus(info.id).toStdString()},
      {"interval", device->inputMapper()->keyEventInterval()}, {"vibration", vibration},
      {"strength", m_settings->devicePresentationTimerHapticStrength(info.id)},
    });
  }
  return serialize({
    {"overlay", m_settings->overlayState(true)}, {"available", m_overlayAvailable},
    {"version", projecteur::version_string()}, {"preview", m_previewTimer->isActive()},
    {"devices", devices},
  });
}

bool Preferences::setOverlay(const QString& key, const QString& value)
{
  if (value.size() > 4096) { return false; }
  const auto items = Json::parse(value, nullptr, false);
  return items.is_array() && items.size() == 1 && m_settings->setOverlay(key, items[0]);
}

bool Preferences::preview()
{
  if (m_previewTimer->isActive()) {
    m_previewTimer->stop();
    m_spotlight->setSpotActive(false);
    return true;
  }
  if (!m_overlayAvailable || m_settings->overlayDisabled() || m_spotlight->spotActive()) {
    return false;
  }
  m_previewTimer->start();
  m_spotlight->setSpotActive(true);
  return true;
}

bool Preferences::setDeviceOption(const QString& key, const QString& option, int value)
{
  const auto device = findDevice(key);
  if (!device) { return false; }
  if (option == "interval" && device->inputMapper()->hasVirtualDevice()) {
    device->inputMapper()->setKeyEventInterval(value);
    m_settings->setDeviceInputSeqInterval(device->deviceId(), value);
  } else if (option == "strength") {
    m_settings->setDevicePresentationTimerHapticStrength(device->deviceId(), value);
  } else {
    return false;
  }
  scheduleUpdate();
  return true;
}

QString Preferences::getMappings(const QString& key)
{
  const auto device = findDevice(key);
  if (!device) { return {}; }
  const auto& mapper = device->inputMapper();
  auto items = Json::array();
  auto movement = Json::array();
  for (const auto& [sequence, mapped] : mapper->configuration()) {
    const auto type = mapped.action->type();
    const auto shortcut = type == Type::KeySequence
      ? static_cast<const KeySequenceAction&>(*mapped.action).keySequence.toString() : QString();
    items.push_back({
      {"sequence", encodeSequence(sequence)}, {"label", sequenceLabel(device->deviceId(), sequence).toStdString()},
      {"type", static_cast<int>(type)}, {"shortcut", shortcut.toStdString()},
      {"movement", !SpecialKeys::logitechSpotlightHoldMove(sequence).name.empty()},
    });
  }
  for (const auto& input : mapper->specialMoveInputs()) {
    movement.push_back({
      {"sequence", encodeSequence(input.keyEventSeq)}, {"label", input.name.toStdString()}, {"movement", true},
    });
  }
  return serialize({{"items", items}, {"movement", movement}});
}

bool Preferences::updateMapping(const QString& key, const QString& payload)
{
  const auto device = findDevice(key);
  if (!device || !device->inputMapper()->hasVirtualDevice() || payload.size() > 100000) { return false; }
  const auto& mapper = device->inputMapper();
  auto configuration = mapper->configuration();
  const auto update = Json::parse(payload, nullptr, false);
  KeyEventSequence previous;
  KeyEventSequence sequence;
  if (!decodeSequence(field(update, "previous"), previous)
      || !decodeSequence(field(update, "sequence"), sequence)) { return false; }
  const auto old = configuration.find(previous);
  if (!previous.empty() && old == configuration.end()) { return false; }
  if (sequence.empty()) {
    if (old == configuration.end()) { return false; }
    configuration.erase(old);
    mapper->setConfiguration(std::move(configuration));
    return true;
  }
  if ((sequence != previous && configuration.count(sequence))
      || (previous.empty() && configuration.size() >= 1024)) { return false; }

  const auto type = static_cast<Type>(integer(field(update, "type"), -1));
  const bool movement = !SpecialKeys::logitechSpotlightHoldMove(sequence).name.empty();
  if (isMoveAction(type) != movement) { return false; }
  std::shared_ptr<Action> action;
  switch (type) {
    case Type::KeySequence:
      if (update.contains("shortcut")) { action = shortcutAction(update.at("shortcut")); }
      else if (old != configuration.end() && old->second.action->type() == type) {
        action = old->second.action;
      }
      break;
    case Type::CyclePresets: action = std::make_shared<CyclePresetsAction>(); break;
    case Type::ToggleSpotlight: action = std::make_shared<ToggleSpotlightAction>(); break;
    case Type::ScrollHorizontal: action = GlobalActions::scrollHorizontal(); break;
    case Type::ScrollVertical: action = GlobalActions::scrollVertical(); break;
    case Type::VolumeControl: action = GlobalActions::volumeControl(); break;
  }
  if (!action) { return false; }
  if (old != configuration.end()) { configuration.erase(old); }
  configuration.emplace(std::move(sequence), MappedAction{std::move(action)});
  mapper->setConfiguration(std::move(configuration));
  return true;
}

void Preferences::recordInput(const QString& key, GDBusMethodInvocation* invocation)
{
  const auto device = findDevice(key);
  if (m_recordRequest || !device || !device->inputMapper()->hasVirtualDevice()) {
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", ""));
    return;
  }
  auto* mapper = device->inputMapper().get();
  m_recorder = WeakObject<InputMapper>(mapper);
  m_recordRequest = invocation;
  m_recordConnections = std::make_unique<Object>();
  m_recordOwner = g_bus_watch_name_on_connection(m_bus, g_dbus_method_invocation_get_sender(invocation),
    G_BUS_NAME_WATCHER_FLAGS_NONE, nullptr, [](GDBusConnection*, const char*, gpointer data) {
      static_cast<Preferences*>(data)->finishRecording(false);
    }, this, nullptr);
  connect(mapper, &Object::destroyed, m_recordConnections.get(), [this] { finishRecording(false); });
  connect(mapper, &InputMapper::keyEventRecorded, m_recordConnections.get(), [this](const KeyEvent& event) {
    m_recorded.push_back(event);
    if (m_recorded.size() >= maxRecordedEvents) { finishRecording(true); }
  });
  connect(mapper, &InputMapper::recordingFinished, m_recordConnections.get(),
    [this](bool canceled) { finishRecording(!canceled); });
  mapper->setRecordingMode(true);
  m_recordTimer.start();
}

void Preferences::finishRecording(bool accepted)
{
  auto* request = std::exchange(m_recordRequest, nullptr);
  if (!request) { return; }
  m_recordConnections.reset();
  m_recordTimer.stop();
  g_bus_unwatch_name(std::exchange(m_recordOwner, 0));
  QString result;
  if (m_recorder) {
    if (accepted && !m_recorded.empty()) {
      for (const auto& info : m_spotlight->connectedDevices()) {
        const auto device = m_spotlight->deviceConnection(info.id);
        if (device && device->inputMapper().get() == m_recorder.data()) {
          result = serialize({
            {"sequence", encodeSequence(m_recorded)},
            {"label", sequenceLabel(info.id, m_recorded).toStdString()}, {"movement", false},
          });
          break;
        }
      }
    }
    m_recorder->setRecordingMode(false);
  }
  m_recorder = WeakObject<InputMapper>(nullptr);
  m_recorded.clear();
  g_dbus_method_invocation_return_value(request, g_variant_new("(s)", result.c_str()));
}

void Preferences::dispatch(const QString& method, GVariant* params, GDBusMethodInvocation* invocation)
{
  const char* first = nullptr;
  const char* second = nullptr;
  int number = 0;
  GVariant* reply = nullptr;
  if (method == "GetState") { reply = g_variant_new("(s)", getState().c_str()); }
  else if (method == "SetOverlay") {
    g_variant_get(params, "(&s&s)", &first, &second);
    reply = g_variant_new("(b)", setOverlay(first, second));
  } else if (method == "ResetOverlay") { m_settings->resetOverlay(); }
  else if (method == "Preview") { reply = g_variant_new("(b)", preview()); }
  else if (method == "SavePreset") {
    g_variant_get(params, "(&s)", &first);
    reply = g_variant_new("(b)", m_settings->savePreset(QString(first).trimmed()));
  } else if (method == "DeletePreset") {
    g_variant_get(params, "(&s)", &first);
    reply = g_variant_new("(b)", m_settings->removePreset(first));
  } else if (method == "RetryDevices") { m_spotlight->retryInputDevices(); }
  else if (method == "SetDeviceOption") {
    g_variant_get(params, "(&s&si)", &first, &second, &number);
    reply = g_variant_new("(b)", setDeviceOption(first, second, number));
  } else if (method == "GetMappings") {
    g_variant_get(params, "(&s)", &first);
    reply = g_variant_new("(s)", getMappings(first).c_str());
  } else if (method == "UpdateMapping") {
    g_variant_get(params, "(&s&s)", &first, &second);
    reply = g_variant_new("(b)", updateMapping(first, second));
  } else if (method == "RecordInput") {
    g_variant_get(params, "(&s)", &first);
    recordInput(first, invocation);
    return;
  } else if (method == "CancelRecording" && m_recordRequest
      && g_str_equal(g_dbus_method_invocation_get_sender(invocation),
                     g_dbus_method_invocation_get_sender(m_recordRequest))) {
    finishRecording(false);
  }
  g_dbus_method_invocation_return_value(invocation, reply);
}
}
