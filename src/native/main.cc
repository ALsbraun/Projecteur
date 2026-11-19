// SPDX-License-Identifier: MIT
#include "service.h"

#include "projecteur-GitVersion.h"
#include "projecteur-i18n.h"

#include <csignal>
#include <iostream>

namespace {
struct OptionStorage
{
  ~OptionStorage() { g_free(config); g_strfreev(commands); g_strfreev(devices); }

  gboolean version = false;
  gboolean fullVersion = false;
  gboolean scan = false;
  gboolean help = false;
  gboolean helpAll = false;
  gboolean disableUInput = false;
  gboolean disableOverlay = false;
  gboolean show = false;
  gboolean background = false;
  gboolean minimizeOnly = false;
  gboolean hideTray = false;
  char* config = nullptr;
  char** commands = nullptr;
  char** devices = nullptr;
};

int sendCommand(GDBusConnection* bus, const char* method, GVariant* parameters, int errorCode)
{
  GError* error = nullptr;
  auto* reply = g_dbus_connection_call_sync(bus, native::serviceName, native::controlPath,
    native::serviceName, method, parameters, G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NO_AUTO_START,
    3000, nullptr, &error);
  if (reply) { g_variant_unref(reply); return 0; }
  g_printerr("%s\n", error->message);
  g_error_free(error);
  return errorCode;
}

void scanDevices(const Spotlight::Options& options, bool detailed)
{
  const auto result = DeviceScan::getDevices(options.additionalDevices);
  for (const auto& error : result.errorMessages) { std::cerr << error << '\n'; }
  std::cout << i18np(" * Found %1 supported device. (%2 readable, %3 writable)",
                     " * Found %1 supported devices. (%2 readable, %3 writable)",
                     result.devices.size(), result.numDevicesReadable, result.numDevicesWritable) << '\n';
  if (!detailed) { return; }
  for (const auto& device : result.devices) {
    const auto& id = device.id;
    std::cout << "  " << device.getName() << " (" << formatHexId(id.vendorId) << ':'
              << formatHexId(id.productId) << ", " << toString(id.busType, false) << ")\n";
    std::cout << "    Physical path: " << id.phys << '\n';
    for (const auto& sub : device.subDevices) {
      std::cout << "    " << sub.deviceFile << " (readable: " << (sub.deviceReadable ? "yes" : "no")
                << ", writable: " << (sub.deviceWritable ? "yes" : "no") << ")\n";
    }
  }
}
}

int main(int argc, char** argv)
{
  const bool activateExisting = argc == 1;
  initializeTranslations();
  g_set_prgname("projecteur");
  OptionStorage options;
  const GOptionEntry entries[] = {
    {"version", 'v', 0, G_OPTION_ARG_NONE, &options.version, "Print application version.", nullptr},
    {"fullversion", 'f', 0, G_OPTION_ARG_NONE, &options.fullVersion, "Print version and diagnostics.", nullptr},
    {"help", 'h', 0, G_OPTION_ARG_NONE, &options.help, "Show command line usage.", nullptr},
    {"help-all", 0, 0, G_OPTION_ARG_NONE, &options.helpAll, "Include all command properties.", nullptr},
    {"device-scan", 'd', 0, G_OPTION_ARG_NONE, &options.scan, "Print device-scan results.", nullptr},
    {"cfg", 0, 0, G_OPTION_ARG_FILENAME, &options.config, "Set custom config file.", "FILE"},
    {"command", 'c', 0, G_OPTION_ARG_STRING_ARRAY, &options.commands,
      "Send command/property to a running instance.", "COMMAND"},
    {"additional-device", 'D', 0, G_OPTION_ARG_STRING_ARRAY, &options.devices,
      "Accept an additional presenter.", "VENDOR:PRODUCT[:NAME]"},
    {"disable-uinput", 0, 0, G_OPTION_ARG_NONE, &options.disableUInput, "Disable uinput support.", nullptr},
    {"disable-overlay", 0, 0, G_OPTION_ARG_NONE, &options.disableOverlay,
      "Disable spotlight overlay completely.", nullptr},
    {"show-dialog", 0, 0, G_OPTION_ARG_NONE, &options.show, "Show preferences on start.", nullptr},
    {"background", 0, 0, G_OPTION_ARG_NONE, &options.background, "Start in the background.", nullptr},
    {"minimize-only", 'm', 0, G_OPTION_ARG_NONE, &options.minimizeOnly,
      "Only allow minimizing the dialog.", nullptr},
    {"hide-systray-icon", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &options.hideTray, nullptr, nullptr},
    {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr},
  };

  std::unique_ptr<GOptionContext, decltype(&g_option_context_free)> context(
    g_option_context_new(nullptr), g_option_context_free);
  g_option_context_set_help_enabled(context.get(), false);
  g_option_context_set_summary(context.get(),
    i18n("Wayland application for the Logitech Spotlight device.").c_str());
  g_option_context_add_main_entries(context.get(), entries, "projecteur");
  GError* error = nullptr;
  if (!g_option_context_parse(context.get(), &argc, &argv, &error) || argc > 1) {
    g_printerr("%s\n", error ? error->message : "Unexpected positional argument.");
    g_clear_error(&error);
    return 1;
  }
  if (options.help || options.helpAll) {
    auto* help = g_option_context_get_help(context.get(), true, nullptr);
    std::cout << help;
    g_free(help);
    if (options.helpAll) {
      std::cout << '\n' << "Commands: quit, settings=show|hide, preferences=show|hide, vibrate=INTENSITY,LENGTH\n"
      << "  spot=on|off|toggle, spot.overlay=on|off, spot.multi-screen=on|off, preset=NAME\n"
      << "  spot.size, spot.size.adjust, spot.shape, spot.rotation, shade, shade.color, shade.opacity\n"
      << "  dot, dot.size, dot.color, dot.opacity, dot.mode, dot.trail\n"
        << "  zoom, zoom.factor, zoom.mode, border, border.color, border.size, border.opacity\n";
    }
    return 0;
  }

  Spotlight::Options spotlightOptions;
  spotlightOptions.enableUInput = !options.disableUInput;
  for (auto** device = options.devices; device && *device; ++device) {
    const auto parts = QString(*device).split(':');
    const auto vendor = parts.size() > 1 ? parts[0].toUShort(nullptr, 16) : uint16_t{0};
    const auto product = parts.size() > 1 ? parts[1].toUShort(nullptr, 16) : uint16_t{0};
    if (!vendor || !product) {
      std::cerr << i18n("Invalid vendor/productId pair: ") << *device << '\n';
      continue;
    }
    spotlightOptions.additionalDevices.push_back({vendor, product, false,
      parts.size() > 2 ? parts[2] : QString{}});
  }

  if (options.version || options.fullVersion || options.scan) {
    std::cout << "Projecteur " << projecteur::version_string() << '\n';
    if (options.fullVersion) {
#ifdef PROJECTEUR_PACKAGE_VERSION
      std::cout << "  - package: " << PROJECTEUR_PACKAGE_VERSION << '\n';
#endif
      std::cout << "  - git-branch: " << projecteur::version_branch() << '\n'
                << "  - git-hash: " << projecteur::version_fullhash() << '\n'
                << "  - build-type: " << projecteur::version_buildtype() << '\n'
                << "  - backend: GLib/GIO (" << glib_major_version << '.' << glib_minor_version
                << '.' << glib_micro_version << ")\n";
    }
    if (options.scan || options.fullVersion) { scanDevices(spotlightOptions, options.scan); }
    return 0;
  }

  const std::unique_ptr<GDBusConnection, decltype(&g_object_unref)> bus(
    g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error), g_object_unref);
  if (!bus) {
    g_printerr("Could not connect to the session D-Bus: %s\n", error->message);
    g_error_free(error);
    return 2;
  }
  if (options.commands) {
    GVariantBuilder commands;
    g_variant_builder_init(&commands, G_VARIANT_TYPE_STRING_ARRAY);
    bool any = false;
    for (auto** command = options.commands; *command; ++command) {
      const auto trimmed = QString(*command).trimmed();
      if (trimmed.empty()) { continue; }
      g_variant_builder_add(&commands, "s", trimmed.c_str());
      any = true;
    }
    auto* args = g_variant_new("(as)", &commands);
    if (!any) { g_variant_unref(args); return 44; }
    return sendCommand(bus.get(), "ApplyCommands", args, 43);
  }

  auto* reply = g_dbus_connection_call_sync(bus.get(), "org.freedesktop.DBus", "/org/freedesktop/DBus",
    "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", native::serviceName, 4u),
    G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &error);
  if (!reply) {
    g_printerr("Could not request the Projecteur D-Bus name: %s\n", error->message);
    g_error_free(error);
    return 2;
  }
  guint ownership = 0;
  g_variant_get(reply, "(u)", &ownership);
  g_variant_unref(reply);
  if (ownership != 1u) {
    if (options.show || (activateExisting && !options.background)) {
      return sendCommand(bus.get(), "ShowPreferences", nullptr, 2);
    }
    return 0;
  }

  const std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)> loop(
    g_main_loop_new(nullptr, false), g_main_loop_unref);
  native::Service service(bus.get(), loop.get(), options.config ? options.config : "",
    std::move(spotlightOptions), options.disableOverlay, options.minimizeOnly);
  if (!service.registerObjects()) { return 2; }
  const auto quit = [](gpointer data) -> gboolean {
    static_cast<native::Service*>(data)->quit();
    return G_SOURCE_CONTINUE;
  };
  const auto interrupt = g_unix_signal_add(SIGINT, quit, &service);
  const auto terminate = g_unix_signal_add(SIGTERM, quit, &service);
  if (options.show) { service.present("--show"); }
  else if (options.minimizeOnly) { service.present("--minimize"); }
  g_main_loop_run(loop.get());
  g_source_remove(interrupt);
  g_source_remove(terminate);
  return 0;
}
