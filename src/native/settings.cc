// SPDX-License-Identifier: MIT
#include "settings.h"

#include "device-defs.h"
#include "projecteur-logging.h"

#include <cmath>

using nlohmann::json;
using native::Color;
using native::Setting;

namespace {
constexpr int hiddenCursor = 10;
constexpr auto presetPrefix = "Preset_";

struct OverlayOption
{
  const char* property;
  const char* configKey;
  const char* command;
  Setting defaultValue;
  double min = 0;
  double max = 0;
  bool editable = true;
};

const OverlayOption options[] = {
  {"showSpotShade", "showSpotShade", "shade", true},
  {"spotSize", "spotSize", "spot.size", 32, 5, 100},
  {"showCenterDot", "showCenterDot", "dot", false},
  {"dotSize", "dotSize", "dot.size", 5, 3, 100},
  {"dotColor", "dotColor", "dot.color", Color{255, 0, 0}},
  {"dotOpacity", "dotOpacity", "dot.opacity", 0.8, 0, 1},
  {"dotMode", "dotMode", "dot.mode", QString("solid")},
  {"dotTrailEnabled", "dotTrailEnabled", "dot.trail", false},
  {"shadeColor", "shadeColor", "shade.color", Color{34, 34, 34}},
  {"shadeOpacity", "shadeOpacity", "shade.opacity", 0.3, 0, 1},
  {"cursor", "cursor", "cursor", hiddenCursor, 0, 21, false},
  {"spotShape", "spotShape", "spot.shape", QString("spotshapes/Circle.qml"), 0, 0, false},
  {"spotRotation", "spotRotation", "spot.rotation", 0.0, 0, 360, false},
  {"showBorder", "showBorder", "border", true},
  {"borderColor", "borderColor", "border.color", Color{115, 210, 22}},
  {"borderSize", "borderSize", "border.size", 4, 0, 100},
  {"borderOpacity", "borderOpacity", "border.opacity", 0.8, 0, 1},
  {"zoomEnabled", "enableZoom", "zoom", false},
  {"zoomFactor", "zoomFactor", "zoom.factor", 2.0, 1.5, 20},
  {"zoomMode", "zoomMode", "zoom.mode", QString("smooth"), 0, 0, false},
  {"multiScreenOverlayEnabled", "multiScreenOverlay", "spot.multi-screen", false},
  {"squareRadius", "Shape.Square/radius", "spot.shape.square.radius", 20, 0, 100, false},
  {"starPoints", "Shape.Star/points", "spot.shape.star.points", 5, 3, 100, false},
  {"starInnerRadius", "Shape.Star/innerRadius", "spot.shape.star.innerradius", 50, 5, 100, false},
  {"ngonSides", "Shape.Ngon/sides", "spot.shape.ngon.sides", 3, 3, 100, false},
};

constexpr const char* shapes[] = {"Circle", "Square", "Star", "Ngon"};

QString presetSection(const QString& preset)
{
  return presetPrefix + preset + '/';
}

QString deviceOption(const DeviceId& id, const QString& key)
{
  return QString("Device_%1_%2/%3").arg(formatHexId(id.vendorId), formatHexId(id.productId), key);
}

bool commandBool(const QString& value)
{
  const auto normalized = value.toLower();
  return normalized == "on" || normalized == "true" || normalized.toInt() > 0;
}

Setting validateOption(const OverlayOption& option, Setting value)
{
  if (auto* integer = std::get_if<int>(&value)) {
    *integer = std::clamp(*integer, static_cast<int>(option.min), static_cast<int>(option.max));
  } else if (auto* number = std::get_if<double>(&value)) {
    if (!std::isfinite(*number)) { return {}; }
    *number = std::clamp(*number, option.min, option.max);
  } else if (const auto* text = std::get_if<QString>(&value)) {
    if (strcmp(option.property, "dotMode") == 0 && *text != "solid" && *text != "diffuse") {
      return {};
    }
    if (strcmp(option.property, "zoomMode") == 0
        && *text != "smooth" && *text != "pixel" && *text != "text") { return {}; }
  } else if (const auto* color = std::get_if<Color>(&value)) {
    if (!color->isValid()) { return {}; }
  }
  return value;
}
}

QString Color::name() const
{
  char output[8];
  g_snprintf(output, sizeof(output), "#%02x%02x%02x", r, g, b);
  return output;
}

Color Color::fromString(const QString& text)
{
  if (!text.startsWith('#')) {
    static const std::map<QString, Color> htmlColors{
      {"aqua", {0, 255, 255}}, {"black", {0, 0, 0}}, {"blue", {0, 0, 255}},
      {"fuchsia", {255, 0, 255}}, {"gray", {128, 128, 128}}, {"green", {0, 128, 0}},
      {"lime", {0, 255, 0}}, {"maroon", {128, 0, 0}}, {"navy", {0, 0, 128}},
      {"olive", {128, 128, 0}}, {"purple", {128, 0, 128}}, {"red", {255, 0, 0}},
      {"silver", {192, 192, 192}}, {"teal", {0, 128, 128}}, {"white", {255, 255, 255}},
      {"yellow", {255, 255, 0}}, {"transparent", {0, 0, 0, 0}},
    };
    const auto match = htmlColors.find(text.toLower());
    return match == htmlColors.end() ? Color{} : match->second;
  }
  bool valid = false;
  const auto bits = text.mid(1).toUInt(&valid, 16);
  if (!valid) { return {}; }
  if (text.size() == 4) {
    return {int((bits >> 8) & 15) * 17, int((bits >> 4) & 15) * 17, int(bits & 15) * 17};
  }
  if (text.size() == 7 || text.size() == 9) {
    return {int((bits >> 16) & 255), int((bits >> 8) & 255), int(bits & 255),
      text.size() == 9 ? int(bits >> 24) : 255};
  }
  return {};
}

Settings::Settings(const QString& fileName) : m_file(fileName)
{
  for (const auto& group : m_file.groupList()) {
    if (group.startsWith(presetPrefix)) { m_presets.push_back(group.mid(strlen(presetPrefix))); }
  }
  std::sort(m_presets.begin(), m_presets.end());
  m_overlay = readOverlay({});
}

std::map<QString, Setting> Settings::readOverlay(const QString& section) const
{
  std::map<QString, Setting> overlay;
  for (const auto& option : options) {
    auto value = validateOption(option, m_file.read(section + option.configKey, option.defaultValue));
    if (value.index() == 0) { value = option.defaultValue; }
    overlay.emplace(option.property, std::move(value));
  }
  return overlay;
}

namespace {
bool writeOverlay(SettingsFile& file, const QString& section, const std::map<QString, Setting>& state)
{
  std::vector<std::pair<QString, Setting>> values;
  for (const auto& option : options) {
    values.emplace_back(section + option.configKey, state.at(option.property));
  }
  return file.write(values);
}
}

bool Settings::hasPreset(const QString& name) const
{
  return std::binary_search(m_presets.begin(), m_presets.end(), name);
}

bool Settings::loadPreset(const QString& name)
{
  if (!hasPreset(name)) { return false; }
  auto overlay = readOverlay(presetSection(name));
  if (!writeOverlay(m_file, {}, overlay)) { return false; }
  m_overlay = std::move(overlay);
  overlayChanged();
  presetLoaded(name);
  return true;
}

bool Settings::savePreset(const QString& name)
{
  if (name.empty() || name.contains('/') || hasPreset(name)) { return false; }
  if (!writeOverlay(m_file, presetSection(name), m_overlay)) { return false; }
  m_presets.insert(std::lower_bound(m_presets.begin(), m_presets.end(), name), name);
  presetsChanged();
  presetLoaded(name);
  return true;
}

bool Settings::removePreset(const QString& name)
{
  if (!hasPreset(name) || !m_file.remove(presetPrefix + name)) { return false; }
  m_presets.erase(std::lower_bound(m_presets.begin(), m_presets.end(), name));
  presetsChanged();
  return true;
}

json Settings::overlayState(bool preferences) const
{
  json state = json::object();
  for (const auto& option : options) {
    if ((preferences && !option.editable) || strchr(option.configKey, '/')) { continue; }
    std::visit([&state, &option](const auto& value) {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, Color>) { state[option.property] = value.name().toStdString(); }
      else if constexpr (std::is_same_v<T, QString>) { state[option.property] = value.toStdString(); }
      else if constexpr (!std::is_same_v<T, std::monostate>) { state[option.property] = value; }
    }, m_overlay.at(option.property));
  }
  if (preferences) { state["hideCursor"] = std::get<int>(m_overlay.at("cursor")) == hiddenCursor; }
  return state;
}

bool Settings::setOverlay(const QString& key, const json& item)
{
  if (key == "hideCursor" && item.is_boolean()) {
    return setValue("cursor", item.get<bool>() ? hiddenCursor : 0);
  }
  for (const auto& option : options) {
    if (key != option.property || !option.editable) { continue; }
    if (std::holds_alternative<bool>(option.defaultValue) && item.is_boolean()) {
      return setValue(key, item.get<bool>());
    }
    if (std::holds_alternative<int>(option.defaultValue) && item.is_number()) {
      const auto number = item.get<double>();
      if (!std::isfinite(number) || number != std::trunc(number)
          || number < INT32_MIN || number > INT32_MAX) { return false; }
      return setValue(key, static_cast<int>(number));
    }
    if (std::holds_alternative<double>(option.defaultValue) && item.is_number()) {
      return setValue(key, item.get<double>());
    }
    if (std::holds_alternative<Color>(option.defaultValue) && item.is_string()) {
      return setValue(key, Color::fromString(item.get<std::string>()));
    }
    if (std::holds_alternative<QString>(option.defaultValue) && item.is_string()) {
      return setValue(key, QString(item.get<std::string>()));
    }
  }
  return false;
}

bool Settings::setValue(const QString& key, const Setting& input)
{
  for (const auto& option : options) {
    if (key != option.property) { continue; }
    auto value = validateOption(option, input);
    if (value.index() == 0) { return false; }
    auto& current = m_overlay.at(key);
    if (current == value) { return true; }
    if (!m_file.write(option.configKey, value)) { return false; }
    current = std::move(value);
    overlayChanged();
    return true;
  }
  return false;
}

void Settings::resetOverlay()
{
  std::map<QString, Setting> defaults;
  for (const auto& option : options) { defaults.emplace(option.property, option.defaultValue); }
  if (defaults != m_overlay && writeOverlay(m_file, {}, defaults)) {
    m_overlay = std::move(defaults);
    overlayChanged();
  }
}

void Settings::cyclePointerMode()
{
  auto next = m_overlay;
  auto& shade = std::get<bool>(next.at("showSpotShade"));
  auto& dot = std::get<bool>(next.at("showCenterDot"));
  auto& zoom = std::get<bool>(next.at("zoomEnabled"));
  auto& border = std::get<bool>(next.at("showBorder"));
  if (zoom) {
    zoom = false;
    shade = false;
    dot = true;
    border = false;
  } else if (dot && !shade) {
    dot = false;
    shade = true;
  } else {
    shade = false;
    dot = false;
    zoom = true;
    border = true;
  }
  if (writeOverlay(m_file, {}, next)) {
    m_overlay = std::move(next);
    overlayChanged();
  }
}

void Settings::applyCommand(const QString& command)
{
  const auto equals = command.find('=');
  if (equals == QString::npos) { return; }
  const auto name = command.left(equals).trimmed();
  const auto text = command.mid(equals + 1).trimmed();
  if (name == "spot.overlay") { setOverlayDisabled(!commandBool(text)); return; }
  if (name == "preset") { loadPreset(text); return; }
  if (name == "spot.shape") {
    for (const auto* shape : shapes) {
      if (QString(shape).toLower() == text.toLower()) {
        setValue("spotShape", QString("spotshapes/%1.qml").arg(shape));
        break;
      }
    }
    return;
  }
  for (const auto& option : options) {
    if (name != option.command) { continue; }
    bool valid = false;
    if (std::holds_alternative<bool>(option.defaultValue)) {
      setValue(option.property, commandBool(text));
    } else if (std::holds_alternative<int>(option.defaultValue)) {
      const auto value = text.toInt(&valid);
      if (valid) { setValue(option.property, value); }
    } else if (std::holds_alternative<double>(option.defaultValue)) {
      const auto value = text.toDouble(&valid);
      if (valid) { setValue(option.property, value); }
    } else if (std::holds_alternative<Color>(option.defaultValue)) {
      setValue(option.property, Color::fromString(text));
    } else {
      setValue(option.property, text);
    }
    return;
  }
}

void Settings::setOverlayDisabled(bool disabled)
{
  if (m_overlayDisabled == disabled) { return; }
  m_overlayDisabled = disabled;
  overlayDisabledChanged(disabled);
}

bool Settings::presentationTimerEnabled() const
{
  return std::get<bool>(m_file.read("presentationTimerEnabled", false));
}

void Settings::setPresentationTimerEnabled(bool enabled)
{
  m_file.write("presentationTimerEnabled", enabled);
}

int Settings::presentationTimerDurationSeconds() const
{
  return std::get<int>(m_file.read("presentationTimerDurationSeconds", 15 * 60));
}

void Settings::setPresentationTimerDurationSeconds(int seconds)
{
  m_file.write("presentationTimerDurationSeconds", seconds);
}

int Settings::deviceInputSeqInterval(const DeviceId& id) const
{
  const auto [min, max] = inputSequenceIntervalRange();
  return std::clamp(std::get<int>(m_file.read(deviceOption(id, "inputSequenceInterval"), 250)), min, max);
}

void Settings::setDeviceInputSeqInterval(const DeviceId& id, int interval)
{
  const auto [min, max] = inputSequenceIntervalRange();
  m_file.write(deviceOption(id, "inputSequenceInterval"), std::clamp(interval, min, max));
}

constexpr uint32_t inputMapFormatVersion = 1;

InputMapConfig Settings::getDeviceInputMapConfig(const DeviceId& id) const
{
  const auto stored = std::get<QString>(m_file.read(deviceOption(id, "inputMapConfigData"), QString{}));
  if (stored.empty()) { return {}; }
  QDataStream stream(stored);
  uint32_t version = 0;
  uint32_t count = 0;
  stream >> version >> count;
  if (version != inputMapFormatVersion || count > 1024) { return {}; }
  InputMapConfig result;
  for (uint32_t i = 0; i < count; ++i) {
    KeyEventSequence sequence;
    MappedAction mapping;
    stream >> sequence >> mapping;
    if (stream.status() != QDataStream::Ok || !mapping.action) { return {}; }
    switch (mapping.action->type()) {
      case Action::Type::ScrollHorizontal: mapping.action = GlobalActions::scrollHorizontal(); break;
      case Action::Type::ScrollVertical: mapping.action = GlobalActions::scrollVertical(); break;
      case Action::Type::VolumeControl: mapping.action = GlobalActions::volumeControl(); break;
      default: break;
    }
    result.emplace(std::move(sequence), std::move(mapping));
  }
  return result;
}

void Settings::setDeviceInputMapConfig(const DeviceId& id, const InputMapConfig& config)
{
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream << inputMapFormatVersion << static_cast<uint32_t>(config.size());
  for (const auto& [sequence, mapping] : config) { stream << sequence << mapping; }
  m_file.write(deviceOption(id, "inputMapConfigData"), bytes);
}

int Settings::devicePresentationTimerHapticStrength(const DeviceId& id) const
{
  return std::clamp(std::get<int>(m_file.read(deviceOption(id, "presentationTimerHapticStrength"), 50)), 0, 100);
}

void Settings::setDevicePresentationTimerHapticStrength(const DeviceId& id, int strength)
{
  m_file.write(deviceOption(id, "presentationTimerHapticStrength"), std::clamp(strength, 0, 100));
}
