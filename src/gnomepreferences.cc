// SPDX-License-Identifier: MIT
#include "gnomepreferences.h"

#include "device-hidpp.h"
#include "device-key-lookup.h"
#include "gnomeoverlay.h"
#include "projecteur-logging.h"
#include "settings.h"
#include "spotlight.h"

#include <QColor>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaProperty>
#include <QProcess>

#include <algorithm>

#include <linux/input.h>

namespace {
constexpr auto path = "/org/projecteur/Projecteur/Preferences";
constexpr auto editableProperties = {
  "showSpotShade", "spotSize", "showCenterDot", "dotSize", "dotColor", "dotOpacity",
  "dotMode", "dotTrailEnabled", "shadeColor", "shadeOpacity", "showBorder", "borderColor",
  "borderSize", "borderOpacity", "zoomEnabled", "zoomFactor", "multiScreenOverlayEnabled"
};
constexpr int maxRecordedEvents = 8;

QString json(const QJsonObject& object)
{
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonValue displayValue(const QVariant& value)
{
  if (value.metaType().id() == QMetaType::QColor) {
    return value.value<QColor>().name();
  }
  return QJsonValue::fromVariant(value);
}

QString deviceKey(const DeviceId& id)
{
  return QStringLiteral("%1:%2:%3:%4").arg(formatHexId(id.vendorId), formatHexId(id.productId))
    .arg(static_cast<int>(id.busType)).arg(id.phys);
}

bool supportsVibration(SubDeviceConnection& subDevice)
{
  if (subDevice.hasFlags(DeviceFlag::Vibrate)) { return true; }
  auto* hidpp = qobject_cast<SubHidppConnection*>(&subDevice);
  return hidpp
    && (hidpp->featureSet().featureCodeSupported(HIDPP::FeatureCode::PresenterControl)
        || hidpp->featureSet().featureCodeSupported(HIDPP::FeatureCode::Haptic));
}

QJsonArray encodeSequence(const KeyEventSequence& sequence)
{
  QJsonArray result;
  for (const auto& frame : sequence) {
    QJsonArray events;
    for (const auto& event : frame) {
      events.append(QJsonArray{event.type, event.code, event.value});
    }
    result.append(events);
  }
  return result;
}

bool decodeSequence(const QJsonValue& value, KeyEventSequence& result)
{
  if (!value.isArray() || value.toArray().size() > 64) { return false; }
  for (const auto& frame : value.toArray()) {
    if (!frame.isArray() || frame.toArray().isEmpty() || frame.toArray().size() > 64) { return false; }
    KeyEvent events;
    for (const auto& event : frame.toArray()) {
      const auto fields = event.toArray();
      if (fields.size() != 3) { return false; }
      const int type = fields[0].toInt(-1);
      const int code = fields[1].toInt(-1);
      const int value = fields[2].toInt();
      if (type < 0 || type > UINT16_MAX || code < 0 || code > UINT16_MAX || fields[2] != value) {
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

std::shared_ptr<Action> shortcutAction(const QJsonValue& value)
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
  const auto chords = value.toArray();
  if (chords.isEmpty() || chords.size() > 4) { return {}; }
  std::vector<int> qtKeys;
  std::vector<uint16_t> nativeModifiers;
  KeyEventSequence events;
  for (const auto& chord : chords) {
    const auto fields = chord.toObject();
    const auto symbol = fields.value(QStringLiteral("symbol")).toString();
    const int key = fields.value(QStringLiteral("code")).toInt();
    if (key <= 0 || key > KEY_MAX || symbol.isEmpty() || symbol.size() > 64) { return {}; }
    const auto parsed = QKeySequence::fromString(symbol, QKeySequence::PortableText);
    const int qtKey = symbol.size() == 1 ? symbol.front().toUpper().unicode()
                                        : static_cast<int>(parsed[0].key());
    int qt = qtKey ? qtKey : Qt::Key_unknown;
    uint16_t native = 0;
    KeyEvent press;
    KeyEvent release;
    const auto modifiers = fields.value(QStringLiteral("modifiers")).toArray();
    for (const auto& item : modifiers) {
      const int code = item.toInt();
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

GnomePreferences::GnomePreferences(Settings* settings, Spotlight* spotlight, GnomeOverlay* overlay,
                                   QTimer* previewTimer, bool minimizeOnly, QObject* parent)
  : QObject(parent), m_settings(settings), m_spotlight(spotlight), m_overlay(overlay)
  , m_previewTimer(previewTimer), m_minimizeOnly(minimizeOnly)
{
  m_updateTimer.setSingleShot(true);
  connect(&m_updateTimer, &QTimer::timeout, this, &GnomePreferences::StateChanged);
  const auto* meta = settings->metaObject();
  const auto update = metaObject()->method(metaObject()->indexOfSlot("scheduleUpdate()"));
  for (const auto* name : editableProperties) {
    connect(settings, meta->property(meta->indexOfProperty(name)).notifySignal(), this, update);
  }
  connect(settings, &Settings::cursorChanged, this, &GnomePreferences::scheduleUpdate);
  connect(settings, &Settings::overlayDisabledChanged, this, &GnomePreferences::scheduleUpdate);
  connect(overlay, &GnomeOverlay::availableChanged, this, &GnomePreferences::scheduleUpdate);
  connect(spotlight, &Spotlight::spotActiveChanged, this, &GnomePreferences::scheduleUpdate);
  connect(spotlight, &Spotlight::inputDevicesChanged, this, &GnomePreferences::scheduleUpdate);
  connect(spotlight, &Spotlight::deviceConnected, this, &GnomePreferences::watchDevice);
  connect(spotlight, &Spotlight::subDeviceConnected, this, &GnomePreferences::watchSubDevice);
  connect(spotlight, &Spotlight::deviceDisconnected, this, [this](const DeviceId&) {
    scheduleUpdate();
    if (m_recordConnections && !m_recorder) { finishRecording(false); }
  });
  for (const auto& device : spotlight->connectedDevices()) { watchDevice(device.id); }

  m_recordTimer.setSingleShot(true);
  m_recordTimer.setInterval(10000);
  connect(&m_recordTimer, &QTimer::timeout, this, [this] { finishRecording(false); });
  m_recordOwner.setConnection(QDBusConnection::sessionBus());
  m_recordOwner.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
  connect(&m_recordOwner, &QDBusServiceWatcher::serviceUnregistered, this,
    [this] { finishRecording(false); });
}

bool GnomePreferences::registerObject()
{
  m_registered = QDBusConnection::sessionBus().registerObject(QString::fromLatin1(path), this,
    QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportAllSignals);
  return m_registered;
}

void GnomePreferences::present(const QString& command)
{
  if (!m_registered) { return; }
  const auto argument = m_minimizeOnly && command == QStringLiteral("--hide")
    ? QStringLiteral("--minimize-existing") : command;
  const auto appDir = QCoreApplication::applicationDirPath();
  auto script = appDir + QStringLiteral("/gnome-preferences/main.js");
  if (!QFileInfo::exists(script)) {
    script = appDir + QStringLiteral("/" PROJECTEUR_PREFERENCES_RELPATH "/main.js");
  }
  if (!QProcess::startDetached(QStringLiteral("gjs"), {QStringLiteral("-m"), script, argument})) {
    qCWarning(PROJECTEUR_MAIN_LOG) << "Could not start Projecteur's GNOME preferences.";
  }
}

void GnomePreferences::scheduleUpdate()
{
  if (!m_updateTimer.isActive()) { m_updateTimer.start(0); }
}

void GnomePreferences::watchDevice(const DeviceId& id)
{
  const auto device = m_spotlight->deviceConnection(id);
  if (!device) { return; }
  connect(device.get(), &DeviceConnection::subDeviceConnected, this,
          &GnomePreferences::scheduleUpdate, Qt::UniqueConnection);
  connect(device.get(), &DeviceConnection::subDeviceDisconnected, this,
          &GnomePreferences::scheduleUpdate, Qt::UniqueConnection);
  connect(device.get(), &DeviceConnection::subDeviceFlagsChanged, this,
          &GnomePreferences::scheduleUpdate, Qt::UniqueConnection);
  connect(device->inputMapper().get(), &InputMapper::configurationChanged, this,
          &GnomePreferences::scheduleUpdate, Qt::UniqueConnection);
  for (const auto& item : device->subDevices()) {
    watchSubDevice(id, device->deviceName(), item.first);
  }
  scheduleUpdate();
}

void GnomePreferences::watchSubDevice(const DeviceId& id, const QString&, const QString& path)
{
  const auto device = m_spotlight->deviceConnection(id);
  if (!device) { return; }
  const auto subDevice = device->subDevice(path);
  if (auto* hidpp = qobject_cast<SubHidppConnection*>(subDevice.get())) {
    connect(hidpp, &SubHidppConnection::receiverStateChanged, this,
            &GnomePreferences::scheduleUpdate, Qt::UniqueConnection);
    connect(hidpp, &SubHidppConnection::presenterStateChanged, this,
            &GnomePreferences::scheduleUpdate, Qt::UniqueConnection);
    connect(hidpp, &SubHidppConnection::featureSetInitialized, this,
            &GnomePreferences::scheduleUpdate, Qt::UniqueConnection);
  }
}

std::shared_ptr<DeviceConnection> GnomePreferences::findDevice(const QString& key)
{
  for (const auto& device : m_spotlight->connectedDevices()) {
    if (deviceKey(device.id) == key) { return m_spotlight->deviceConnection(device.id); }
  }
  return {};
}

QString GnomePreferences::GetState()
{
  QJsonObject overlay;
  for (const auto* name : editableProperties) {
    overlay.insert(QString::fromLatin1(name), displayValue(m_settings->property(name)));
  }
  overlay.insert(QStringLiteral("hideCursor"), m_settings->cursor() == Qt::BlankCursor);
  QJsonArray devices;
  for (const auto& info : m_spotlight->connectedDevices()) {
    const auto device = m_spotlight->deviceConnection(info.id);
    if (!device) { continue; }
    bool vibration = false;
    QJsonArray subDevices;
    QJsonArray hidppDevices;
    for (const auto& [path, subDevice] : device->subDevices()) {
      vibration = vibration || supportsVibration(*subDevice);
      if (const auto* hidpp = qobject_cast<SubHidppConnection*>(subDevice.get())) {
        const auto protocol = hidpp->protocolVersion();
        hidppDevices.append(QJsonObject{
          {QStringLiteral("presenter"), QString::fromLatin1(toString(hidpp->presenterState(), false))},
          {QStringLiteral("receiver"), hidpp->busType() == BusType::Usb
            ? QString::fromLatin1(toString(hidpp->receiverState(), false)) : QString()},
          {QStringLiteral("protocol"), QStringLiteral("%1.%2").arg(protocol.major).arg(protocol.minor)}
        });
      }
      subDevices.append(QJsonObject{
        {QStringLiteral("path"), path},
        {QStringLiteral("mode"), QString::fromLatin1(toString(subDevice->mode(), false))},
        {QStringLiteral("flags"), QJsonArray::fromStringList(toStringList(subDevice->flags(), false))}
      });
    }
    devices.append(QJsonObject{
      {QStringLiteral("id"), deviceKey(info.id)},
      {QStringLiteral("name"), info.name},
      {QStringLiteral("vendor"), formatHexId(info.id.vendorId)},
      {QStringLiteral("product"), formatHexId(info.id.productId)},
      {QStringLiteral("bus"), QString::fromLatin1(toString(info.id.busType, false))},
      {QStringLiteral("physicalPath"), info.id.phys},
      {QStringLiteral("subDevices"), subDevices},
      {QStringLiteral("hidpp"), hidppDevices},
      {QStringLiteral("inputAvailable"), device->inputMapper()->hasVirtualDevice()},
      {QStringLiteral("inputStatus"), m_spotlight->inputMappingStatus(info.id)},
      {QStringLiteral("interval"), device->inputMapper()->keyEventInterval()},
      {QStringLiteral("vibration"), vibration},
      {QStringLiteral("strength"), m_settings->devicePresentationTimerHapticStrength(info.id)}
    });
  }
  return json({
    {QStringLiteral("overlay"), overlay},
    {QStringLiteral("available"), m_overlay->available()},
    {QStringLiteral("version"), QCoreApplication::applicationVersion()},
    {QStringLiteral("preview"), m_previewTimer->isActive()},
    {QStringLiteral("devices"), devices}
  });
}

bool GnomePreferences::SetOverlay(const QString& key, const QString& value)
{
  // A JSON array allows scalar values to be parsed without changing their types.
  const auto values = QJsonDocument::fromJson(value.toUtf8()).array();
  if (values.size() != 1) { return false; }
  const auto item = values.at(0);
  if (key == QStringLiteral("hideCursor") && item.isBool()) {
    m_settings->setCursor(item.toBool() ? Qt::BlankCursor : Qt::ArrowCursor);
    return true;
  }
  for (const auto* name : editableProperties) {
    if (key != QLatin1String(name)) { continue; }
    const auto existing = m_settings->property(name);
    if (existing.metaType().id() == QMetaType::QColor) {
      const QColor color(item.toString());
      return color.isValid() && m_settings->setProperty(name, color);
    }
    if ((existing.typeId() == QMetaType::Bool && item.isBool())
        || (existing.typeId() == QMetaType::Int && item.isDouble() && item == item.toInt())
        || (existing.typeId() == QMetaType::Double && item.isDouble())
        || (existing.typeId() == QMetaType::QString && item.isString())) {
      return m_settings->setProperty(name, item.toVariant());
    }
  }
  return false;
}

void GnomePreferences::ResetOverlay()
{
  m_settings->setSpotlightSettings(Settings::defaultSpotlightSettings());
}

bool GnomePreferences::Preview()
{
  if (m_previewTimer->isActive()) {
    m_previewTimer->stop();
    m_spotlight->setSpotActive(false);
    return true;
  }
  if (!m_overlay->available() || m_settings->overlayDisabled() || m_spotlight->spotActive()) {
    return false;
  }
  m_previewTimer->start();
  m_spotlight->setSpotActive(true);
  return true;
}

bool GnomePreferences::SavePreset(const QString& name)
{
  const auto trimmed = name.trimmed();
  if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/'))
      || m_settings->presetModel()->hasPreset(trimmed)) {
    return false;
  }
  m_settings->savePreset(trimmed);
  return true;
}

bool GnomePreferences::DeletePreset(const QString& name)
{
  if (!m_settings->presetModel()->hasPreset(name)) { return false; }
  m_settings->removePreset(name);
  return true;
}

void GnomePreferences::RetryDevices()
{
  m_spotlight->retryInputDevices();
}

bool GnomePreferences::SetDeviceOption(const QString& key, const QString& option, int value)
{
  const auto device = findDevice(key);
  if (!device) { return false; }
  if (option == QStringLiteral("interval") && device->inputMapper()->hasVirtualDevice()) {
    device->inputMapper()->setKeyEventInterval(value);
    m_settings->setDeviceInputSeqInterval(device->deviceId(), value);
  } else if (option == QStringLiteral("strength")) {
    m_settings->setDevicePresentationTimerHapticStrength(device->deviceId(), value);
  } else {
    return false;
  }
  scheduleUpdate();
  return true;
}

QString GnomePreferences::GetMappings(const QString& key)
{
  const auto device = findDevice(key);
  if (!device) { return {}; }
  const auto& mapper = device->inputMapper();
  QJsonArray items;
  QJsonArray movement;
  for (const auto& [sequence, mapped] : mapper->configuration()) {
    const auto type = mapped.action->type();
    const auto shortcut = type == Type::KeySequence
      ? static_cast<const KeySequenceAction&>(*mapped.action).keySequence.toString() : QString();
    items.append(QJsonObject{
      {QStringLiteral("sequence"), encodeSequence(sequence)},
      {QStringLiteral("label"), sequenceLabel(device->deviceId(), sequence)},
      {QStringLiteral("type"), static_cast<int>(type)},
      {QStringLiteral("shortcut"), shortcut},
      {QStringLiteral("movement"), !SpecialKeys::logitechSpotlightHoldMove(sequence).name.isEmpty()}
    });
  }
  for (const auto& input : mapper->specialMoveInputs()) {
    movement.append(QJsonObject{
      {QStringLiteral("sequence"), encodeSequence(input.keyEventSeq)},
      {QStringLiteral("label"), input.name},
      {QStringLiteral("movement"), true}
    });
  }
  return json({{QStringLiteral("items"), items}, {QStringLiteral("movement"), movement}});
}

bool GnomePreferences::UpdateMapping(const QString& key, const QString& payload)
{
  const auto device = findDevice(key);
  if (!device || !device->inputMapper()->hasVirtualDevice() || payload.size() > 100000) { return false; }
  const auto& mapper = device->inputMapper();
  auto configuration = mapper->configuration();
  const auto update = QJsonDocument::fromJson(payload.toUtf8()).object();
  KeyEventSequence previous;
  KeyEventSequence sequence;
  if (!decodeSequence(update.value(QStringLiteral("previous")), previous)
      || !decodeSequence(update.value(QStringLiteral("sequence")), sequence)) { return false; }
  const auto old = configuration.find(previous);
  if (!previous.empty() && old == configuration.end()) { return false; }
  if (sequence.empty()) {
    if (old == configuration.end()) { return false; }
    configuration.erase(old);
    mapper->setConfiguration(std::move(configuration));
    return true;
  }
  if ((sequence != previous && configuration.count(sequence)) || (previous.empty() && configuration.size() >= 1024)) {
    return false;
  }

  const auto type = static_cast<Type>(update.value(QStringLiteral("type")).toInt());
  const bool movement = !SpecialKeys::logitechSpotlightHoldMove(sequence).name.isEmpty();
  if (isMoveAction(type) != movement) { return false; }
  std::shared_ptr<Action> action;
  switch (type) {
    case Type::KeySequence:
      if (update.contains(QStringLiteral("shortcut"))) {
        action = shortcutAction(update.value(QStringLiteral("shortcut")));
      } else if (old != configuration.end() && old->second.action->type() == type) {
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

QString GnomePreferences::RecordInput(const QString& key)
{
  const auto device = findDevice(key);
  if (!calledFromDBus() || m_recordConnections || !device || !device->inputMapper()->hasVirtualDevice()) {
    return {};
  }
  m_recorder = device->inputMapper().get();
  m_recordRequest = message();
  setDelayedReply(true);
  m_recordOwner.setWatchedServices({m_recordRequest.service()});
  m_recordConnections = std::make_unique<QObject>();
  connect(m_recorder, &QObject::destroyed, m_recordConnections.get(),
    [this] { finishRecording(false); });
  connect(m_recorder, &InputMapper::keyEventRecorded, m_recordConnections.get(),
    [this](const KeyEvent& event) {
      m_recorded.push_back(event);
      if (m_recorded.size() >= maxRecordedEvents) { finishRecording(true); }
    });
  connect(m_recorder, &InputMapper::recordingFinished, m_recordConnections.get(),
    [this](bool canceled) { finishRecording(!canceled); });
  m_recorder->setRecordingMode(true);
  m_recordTimer.start();
  return {};
}

void GnomePreferences::CancelRecording()
{
  if (calledFromDBus() && m_recordConnections && message().service() == m_recordRequest.service()) {
    finishRecording(false);
  }
}

void GnomePreferences::finishRecording(bool accepted)
{
  if (!m_recordConnections) { return; }
  m_recordConnections.reset();
  m_recordTimer.stop();
  m_recordOwner.setWatchedServices({});
  QString result;
  if (m_recorder) {
    if (accepted && !m_recorded.empty()) {
      // The recorder belongs to the selected device. Resolve its label before
      // resuming normal input processing.
      for (const auto& info : m_spotlight->connectedDevices()) {
        const auto device = m_spotlight->deviceConnection(info.id);
        if (device && device->inputMapper().get() == m_recorder.data()) {
          result = json({
            {QStringLiteral("sequence"), encodeSequence(m_recorded)},
            {QStringLiteral("label"), sequenceLabel(info.id, m_recorded)},
            {QStringLiteral("movement"), false}
          });
          break;
        }
      }
    }
    m_recorder->setRecordingMode(false);
  }
  m_recorder.clear();
  m_recorded.clear();
  QDBusConnection::sessionBus().send(m_recordRequest.createReply(QVariantList{result}));
}
