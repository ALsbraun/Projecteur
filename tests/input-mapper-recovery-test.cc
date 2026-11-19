// This file is part of Projecteur - see LICENSE.md.
#include "deviceinput.h"
#include "virtualdevice.h"
#ifdef PROJECTEUR_NATIVE_GNOME
#include "native/settings.h"
#else
#include "settings.h"

#include <QCoreApplication>
#endif

#include <cstdlib>
#include <iostream>
#include <map>

namespace {
std::map<const VirtualDevice*, std::vector<input_event>> emitted;

void check(bool success, const char* message)
{
  if (!success) { std::cerr << message << '\n'; std::exit(1); }
}
}

// Replace only the kernel output endpoint. InputMapper and its sequence state
// machine below are the production implementation; this test needs no uinput.
struct VirtualDevice::Token {};
VirtualDevice::VirtualDevice(Token, int, const char*, const char*) {}
VirtualDevice::~VirtualDevice() { emitted.erase(this); }
std::shared_ptr<VirtualDevice> VirtualDevice::create(Type, const char*, uint16_t,
    uint16_t, uint16_t, const char*)
{
  return std::make_shared<VirtualDevice>(Token{}, -1, "test", "test");
}
void VirtualDevice::emitEvents(const input_event events[], size_t count)
{
  if (count) { emitted[this].insert(emitted[this].end(), events, events + count); }
}
void VirtualDevice::emitEvents(const std::vector<input_event>& events)
{
  emitEvents(events.data(), events.size());
}
void VirtualDevice::releasePressedKeys() {}
#ifndef PROJECTEUR_NATIVE_GNOME
const Settings::SettingRange<int>& Settings::inputSequenceIntervalRange()
{
  static const SettingRange<int> range{50, 2000};
  return range;
}
#endif

int main(int argc, char* argv[])
{
#ifdef PROJECTEUR_NATIVE_GNOME
  (void)argc;
  (void)argv;
#else
  QCoreApplication app(argc, argv);
#endif
  InputMapper mapper({}, {});
  const KeyEvent down{{EV_KEY, KEY_PAGEDOWN, 1}};
  const KeyEvent up{{EV_KEY, KEY_PAGEDOWN, 0}};
  mapper.addEvents(down);
  mapper.addEvents(KeyEvent{});
  check(!mapper.hasVirtualDevice() && emitted.empty(), "missing uinput must not emit events");

  const auto mouse = VirtualDevice::create(VirtualDevice::Type::Mouse);
  const auto keyboard = VirtualDevice::create(VirtualDevice::Type::Keyboard);
  mapper.setVirtualDevices(mouse, {});
  check(!mapper.hasVirtualDevice(), "mapping needs both virtual endpoints");
  mapper.setVirtualDevices(mouse, keyboard);
  check(mapper.hasVirtualDevice(), "existing mapper must recover after uinput becomes available");
  mapper.addEvents(down);
  mapper.addEvents(up);
  check(emitted[keyboard.get()].size() == 4 && emitted[mouse.get()].empty(),
        "recovered mapper must forward one native keyboard press/release");
  emitted.clear();

  InputMapConfig config;
  config.emplace(KeyEventSequence{down, up}, MappedAction{std::make_shared<ToggleSpotlightAction>()});
  mapper.setConfiguration(config);
  int mapped = 0;
#ifdef PROJECTEUR_NATIVE_GNOME
  mapper.actionMapped.add([&mapped](const auto&) { ++mapped; });
#else
  QObject::connect(&mapper, &InputMapper::actionMapped, [&mapped](const auto&) { ++mapped; });
#endif
  mapper.addEvents(down);
  mapper.setVirtualDevices(mouse, keyboard);
  mapper.addEvents(up);
  check(mapped == 1 && emitted.empty(), "an unchanged retry must preserve the current mapping sequence");

  mapper.addEvents(down);
  mapper.setVirtualDevices({}, {});
  mapper.setVirtualDevices(mouse, keyboard);
  mapper.addEvents(up);
  check(mapped == 1 && emitted[keyboard.get()].size() == 2
        && emitted[keyboard.get()].front().value == 0,
        "recovery must discard a stale buffered press instead of replaying it");
  check(mapper.configuration() == config, "permission recovery must preserve user mappings");
  emitted.clear();
  mapper.addEvents(down);
  mapper.addEvents(up);
  check(mapped == 2 && emitted.empty(), "saved mappings must work after permission recovery");
  std::cout << "Input mapper recovery checks passed\n";
}
