// SPDX-License-Identifier: MIT
// Exercise the production service against a private GNOME Shell endpoint.
#include "native/service.h"
#include "native/files.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace DeviceScan {
ScanResult getDevices(const std::vector<SupportedDevice>&) { return {}; }
}

namespace {
void check(bool success, const char* message)
{
  if (!success) { throw std::runtime_error(message); }
}

void until(const std::function<bool()>& condition, const char* message)
{
  const auto deadline = g_get_monotonic_time() + 4 * G_TIME_SPAN_SECOND;
  while (!condition()) {
    check(g_get_monotonic_time() < deadline, message);
    g_main_context_iteration(nullptr, false);
    g_usleep(1000);
  }
}

struct ShellState
{
  bool active;
  nlohmann::json settings;
};

class MockShell
{
public:
  explicit MockShell(GDBusConnection* bus) : m_bus(bus)
  {
    static constexpr auto xml = R"xml(<node><interface name="org.projecteur.Gnome">
      <method name="SetState"><arg type="b" direction="in"/><arg type="s" direction="in"/></method>
      </interface></node>)xml";
    static const GDBusInterfaceVTable vtable = {
      [](GDBusConnection*, const char*, const char*, const char*, const char*, GVariant* parameters,
         GDBusMethodInvocation* invocation, gpointer data) {
        auto& shell = *static_cast<MockShell*>(data);
        gboolean active;
        const char* settings;
        g_variant_get(parameters, "(b&s)", &active, &settings);
        shell.states.push_back({active != 0, nlohmann::json::parse(settings)});
        g_dbus_method_invocation_return_value(invocation, nullptr);
      },
      nullptr, nullptr, {}
    };
    auto* info = g_dbus_node_info_new_for_xml(xml, nullptr);
    m_id = g_dbus_connection_register_object(bus, "/org/projecteur/Gnome", info->interfaces[0],
      &vtable, this, nullptr, nullptr);
    g_dbus_node_info_unref(info);
    check(m_id != 0, "Could not register the test Shell endpoint");
    auto* reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", "org.projecteur.Gnome", 4u),
      G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
    guint ownership = 0;
    if (reply) {
      g_variant_get(reply, "(u)", &ownership);
      g_variant_unref(reply);
    }
    check(ownership == 1, "Run the test on an isolated session bus");
  }

  ~MockShell() { g_dbus_connection_unregister_object(m_bus, m_id); }

  std::vector<ShellState> states;

private:
  GDBusConnection* m_bus;
  guint m_id = 0;
};

constexpr auto legacyConfig = R"ini([Device_046d_c53e]
inputMapConfigData=\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x02\x00\x01\x00\x1c\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00
inputSequenceInterval=400
presentationTimerHapticStrength=77

[General]
dotColor=18,171,239,180
futureKey=preserve this entry
presentationTimerDurationSeconds=151
presentationTimerEnabled=true
spotSize=53

[Preset_Legacy \x5bDemo\x5d]
borderColor=19,42,230
spotSize=61

[Preset_Legacy \x5bDemo\x5d][Shape.Square]
radius=72
)ini";

enum class Mode { Spotlight, Magnifier, Pointer };

bool matchesMode(const ShellState& state, Mode mode)
{
  return state.settings.at("showSpotShade") == (mode == Mode::Spotlight)
      && state.settings.at("zoomEnabled") == (mode == Mode::Magnifier)
      && state.settings.at("showCenterDot") == (mode == Mode::Pointer);
}

void checkMode(const ShellState& state, Mode mode, bool active)
{
  check(matchesMode(state, mode), "The selected mode did not reach the GNOME overlay coherently");
  check(state.active == active, "Changing mode changed overlay activation");
  if (mode == Mode::Pointer) {
    check(state.settings.at("showBorder") == false, "Pointer mode retained the magnifier border");
  } else if (mode == Mode::Magnifier) {
    check(state.settings.at("showBorder") == true, "Magnifier mode lost its border");
  }
}
}

namespace native {
class ModeCycleTest
{
public:
  static void run(GDBusConnection* bus, GMainLoop* loop, const QString& config)
  {
    MockShell shell(bus);
    Service service(bus, loop, config, {false, {}}, false, false);
    check(service.registerObjects(), "Could not register the Projecteur D-Bus objects");
    auto& settings = service.m_settings;
    auto& spotlight = service.m_spotlight;
    const DeviceId remote{0x046d, 0xc53e, BusType::Usb, {}};
    const QString preset = "Legacy [Demo]";
    check(settings.overlayState(true).at("spotSize") == 53
        && settings.overlayState(true).at("dotColor") == "#12abef", "Legacy overlay data was not loaded");
    check(settings.presentationTimerEnabled() && settings.presentationTimerDurationSeconds() == 151,
      "Legacy presentation timer was not loaded");
    check(settings.deviceInputSeqInterval(remote) == 400
        && settings.devicePresentationTimerHapticStrength(remote) == 77,
      "Legacy per-device preferences were not loaded");
    auto mappings = settings.getDeviceInputMapConfig(remote);
    check(mappings.size() == 1 && mappings.begin()->second.action->type() == Action::Type::ToggleSpotlight,
      "Legacy binary input mapping was not loaded");
    check(settings.hasPreset(preset) && settings.loadPreset(preset), "Could not load the legacy preset");
    check(settings.overlayState(true).at("spotSize") == 61
        && settings.overlayState(true).at("borderColor") == "#132ae6", "Legacy preset values were lost");

    // The Qt 6 stream uses one key for single chords and four slots for a
    // multi-chord sequence. Keep the legacy bytes stable in both cases.
    QByteArray bytes;
    QDataStream output(&bytes, QIODevice::WriteOnly);
    output << QKeySequence::fromString("Alt+Tab") << QKeySequence('A', 'B');
    check(bytes.toHex() == "00000001090000010000000400000041000000420000000000000000",
      "Shortcut data no longer matches the Qt 6 configuration format");

    const auto recorded = KeyEventSequence{{DeviceInputEvent(1, 29, 1)}};
    mappings.emplace(recorded, MappedAction{
      std::make_shared<KeySequenceAction>(NativeKeySequence::predefined::altTab())});
    settings.setDeviceInputSeqInterval(remote, 450);
    settings.setDeviceInputMapConfig(remote, mappings);
    const Settings reopened(config);
    check(reopened.overlayState(true).at("spotSize") == 61 && reopened.deviceInputSeqInterval(remote) == 450
        && reopened.getDeviceInputMapConfig(remote) == mappings, "Mapping and settings did not round trip");
    File file(config);
    check(file.open(QIODevice::ReadOnly), "Could not read saved configuration");
    const auto stored = file.readAll();
    check(stored.contains("futureKey=preserve this entry")
        && stored.contains(R"ini([Preset_Legacy \x5bDemo\x5d][Shape.Square])ini")
        && stored.contains("[Shape.Square]\nradius=72")
        && stored.contains("dotColor=255,0,0"), "Settings did not preserve the existing file structure");
    check(!spotlight.anySpotlightDeviceConnected(), "Hardware enumeration was not isolated");

    check(settings.setOverlay("dotColor", "#12abef") && settings.setOverlay("dotSize", 13)
        && settings.setOverlay("spotSize", 41) && settings.setOverlay("zoomFactor", 3.25)
        && settings.savePreset("Before cycle"), "Could not configure the overlay");
    const auto presets = settings.presets();
    until([&] { return !shell.states.empty() && service.m_overlayAvailable; },
      "Initial state did not reach the Shell");
    checkMode(shell.states.back(), Mode::Spotlight, false);

    const auto cycle = [&](Mode expected, bool active) {
      const auto before = shell.states.size();
      spotlight.pointerModeCycleRequested();
      check(spotlight.spotActive() == active, "Hardware mode cycle changed the hold state");
      until([&] { return shell.states.size() > before && matchesMode(shell.states.back(), expected); },
        "Mode cycle did not reach the Shell");
      checkMode(shell.states.back(), expected, active);
      const auto state = settings.overlayState(true);
      check(state.at("dotColor") == "#12abef" && state.at("dotSize") == 13
          && state.at("spotSize") == 41 && state.at("zoomFactor") == 3.25 && settings.presets() == presets,
        "Mode cycling changed the user's appearance settings or presets");
    };
    cycle(Mode::Magnifier, false);
    cycle(Mode::Pointer, false);
    cycle(Mode::Spotlight, false);
    spotlight.setSpotActive(true);
    until([&] { return shell.states.back().active; }, "Activation did not reach the Shell");
    cycle(Mode::Magnifier, true);
    cycle(Mode::Pointer, true);
    cycle(Mode::Spotlight, true);
  }
};
}

int main()
{
  std::unique_ptr<char, decltype(&g_free)> temporary(g_dir_make_tmp("projecteur-mode-test-XXXXXX", nullptr),
    g_free);
  if (!temporary) { return 1; }
  const QString config = QString(temporary.get()) + "/projecteurrc";
  std::unique_ptr<GDBusConnection, decltype(&g_object_unref)> bus(
    g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr), g_object_unref);
  std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)> loop(g_main_loop_new(nullptr, false),
    g_main_loop_unref);
  int result = 0;
  try {
    check(bus && native::replaceFile(config, legacyConfig), "Could not prepare the isolated test");
    native::ModeCycleTest::run(bus.get(), loop.get(), config);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    result = 1;
  }
  std::filesystem::remove_all(temporary.get());
  return result;
}
