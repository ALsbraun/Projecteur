// Run without hardware: c++ -std=c++17 -I src tests/device-input-test.cc -o /tmp/device-input-test
#include "inputevents.h"
#include "presentercontrols.h"

#include <cstdlib>
#include <iostream>

static void check(bool success, const char* message)
{
  if (!success) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

int main()
{
  check(PresenterControls::notificationDeviceMatches(1, 1, false),
        "USB notifications from the selected presenter slot must be accepted");
  check(!PresenterControls::notificationDeviceMatches(2, 1, false),
        "another receiver slot must not change presenter state");
  check(!PresenterControls::notificationDeviceMatches(0xff, 1, false),
        "receiver-address notifications must not be treated as USB presenter buttons");
  check(PresenterControls::notificationDeviceMatches(0xff, 1, true),
        "direct Bluetooth device notifications may use the local-device address");
  check(PresenterControls::notificationDeviceMatches(1, 1, true),
        "Bluetooth notifications that echo the command address must still work");
  check(!PresenterControls::notificationDeviceMatches(2, 1, true),
        "Bluetooth support must not accept arbitrary wireless slots");
  check(PresenterControls::reportingStateMatches(0x03, 0x01),
        "Bluetooth state responses omit write-valid bits for Switch Highlight");
  check(PresenterControls::reportingStateMatches(0x23, 0x01),
        "Bluetooth state responses omit write-valid bits for Pointer Hold");
  check(PresenterControls::reportingStateMatches(0x23, 0x23),
        "an echoed request must also confirm the requested reporting state");
  check(!PresenterControls::reportingStateMatches(0x23, 0x00),
        "disabled diversion must not be accepted as enabled");
  check(!PresenterControls::reportingStateMatches(0x23, 0x11),
        "raw XY diversion must be disabled when native pointer movement is requested");
  check(PresenterControls::reportingStateMatches(0x03, 0x11),
        "state bits not requested by a valid bit must remain unrestricted");
  // Bluetooth/composite reports can begin with a scan code or a button rather
  // than REL_X. All movements must activate the overlay, wheels must not.
  const input_event mixed[] = {
    {{}, EV_MSC, MSC_SCAN, 0x7004f},
    {{}, EV_KEY, KEY_RIGHT, 1},
    {{}, EV_REL, REL_Y, -3},
    {{}, EV_KEY, BTN_LEFT, 1},
    {{}, EV_SYN, SYN_REPORT, 0},
  };
  check(InputEvents::hasPointerMotion(mixed, 5), "motion after a key must activate the pointer");
  const auto mixedFrame = InputEvents::routeFrame(mixed, 5);
  check(mixedFrame.mouse.size() == 3 && mixedFrame.keyboard.size() == 2,
        "a mixed HID report must preserve both mouse and keyboard input");
  check(mixedFrame.mouse[0].code == REL_Y && mixedFrame.mouse[1].code == BTN_LEFT,
        "mouse report must preserve event order");
  check(mixedFrame.keyboard[0].code == KEY_RIGHT,
        "slide navigation must go to the keyboard device");
  check(mixedFrame.mouse.back().type == EV_SYN && mixedFrame.keyboard.back().type == EV_SYN,
        "both virtual devices require complete reports");

  const input_event wheel[] = {{{}, EV_REL, REL_WHEEL, 1}, {{}, EV_SYN, SYN_REPORT, 0}};
  check(!InputEvents::hasPointerMotion(wheel, 2), "scrolling must not activate the overlay");
  const auto wheelFrame = InputEvents::routeFrame(wheel, 2);
  check(wheelFrame.mouse.size() == 2 && wheelFrame.keyboard.empty(), "wheel input must reach the mouse");

  const input_event volume[] = {{{}, EV_KEY, KEY_VOLUMEUP, 1}, {{}, EV_SYN, SYN_REPORT, 0}};
  const auto volumeFrame = InputEvents::routeFrame(volume, 2);
  check(volumeFrame.keyboard.size() == 2 && volumeFrame.mouse.empty(), "volume is a keyboard key");

  const input_event special[] = {{{}, EV_KEY, 0x0e10, 1}, {{}, EV_SYN, SYN_REPORT, 0}};
  const auto specialFrame = InputEvents::routeFrame(special, 2);
  check(specialFrame.keyboard.empty() && specialFrame.mouse.empty(),
        "unmapped HID++ pseudo keys must never be injected into the OS");
  check(!InputEvents::hasPointerMotion(nullptr, 0), "an empty frame has no motion");
  const auto empty = InputEvents::routeFrame(nullptr, 0);
  check(empty.keyboard.empty() && empty.mouse.empty(), "empty input must not emit a sync report");

  for (int speed = 0; speed <= 255; ++speed) {
    check(InputEvents::pointerSpeed(speed) == (speed > 9 ? 0x19 : 0x10 + speed),
          "all pointer speed settings must encode as 0x10 through 0x19");
  }
  check(InputEvents::signedMotion(0x00, 0xff) == 255, "fast rightward motion must not reverse direction");
  check(InputEvents::signedMotion(0xff, 0x01) == -255, "fast leftward motion must not reverse direction");
  check(InputEvents::signedMotion(0x80, 0x00) == -32768, "minimum signed HID++ motion");
  check(InputEvents::signedMotion(0x7f, 0xff) == 32767, "maximum signed HID++ motion");
  const uint8_t controls[] = {0, 0xda, 0, 0xdc, 0, 0xd8, 0, 0xdf};
  check(PresenterControls::contains(controls, 8, PresenterControls::PointerHold),
        "pointer hold must also be recognized after two already-held buttons");
  check(PresenterControls::contains(controls, 8, PresenterControls::SwitchHighlight),
        "all four control slots must be read");
  const uint8_t unrelated[] = {1, 0xd8, 0, 0, 0, 0, 0, 0};
  check(!PresenterControls::contains(unrelated, 8, PresenterControls::PointerHold),
        "a matching low byte is not the same control ID");
  const uint8_t released[] = {0, 0, 0, 0, 0, 0, 0, 0};
  check(!PresenterControls::contains(released, 8, PresenterControls::PointerHold),
        "all-zero control report releases the pointer");
  check(!PresenterControls::contains(controls, 5, PresenterControls::PointerHold),
        "truncated control IDs must not be read");

  using PresenterControls::PointerTransition;
  PresenterControls::PointerActivity<int, int> activity;
  check(!activity.hasReports(1) && activity.shouldExpire(),
        "firmware without hold reports must keep the motion timeout fallback");
  check(activity.observe(1, 10, true) == PointerTransition::Activate,
        "pressing the pointer activates the overlay");
  check(activity.hasReports(1) && !activity.shouldExpire(),
        "holding still past the motion timeout must keep the overlay visible");
  check(activity.observe(1, 10, false) == PointerTransition::Deactivate,
        "release must hide the overlay immediately");
  check(activity.hasReports(1), "trailing motion after release must not activate the overlay again");
  activity.observe(1, 10, true);
  activity.observe(2, 20, true);
  check(activity.observe(1, 10, false) == PointerTransition::None && !activity.shouldExpire(),
        "releasing one presenter must not hide another held presenter");
  activity.observe(1, 10, true);
  check(!activity.removeConnection(10) && activity.anyHeld(),
        "disconnecting one presenter must preserve another held presenter");
  check(!activity.hasReports(1) && activity.hasReports(2),
        "disconnect must clear only the disconnected presenter's observed reports");
  check(activity.removeDevice(2) && !activity.anyHeld() && !activity.hasReports(2),
        "power-off must release a held pointer and reset report detection");

  InputEvents::PressedKeys keys;
  const input_event heldKeys[] = {
    {{}, EV_KEY, KEY_PAGEDOWN, 1}, {{}, EV_KEY, BTN_LEFT, 1},
    {{}, EV_KEY, KEY_LEFTCTRL, 1}, {{}, EV_KEY, KEY_PAGEDOWN, 2},
    {{}, EV_SYN, SYN_REPORT, 0},
  };
  keys.record(heldKeys, 5);
  auto releases = keys.releases();
  check(releases.size() == 4, "disconnect must release each pressed key and mouse button exactly once");
  check(std::all_of(releases.begin(), releases.end() - 1, [](const input_event& event) {
    return event.type == EV_KEY && event.value == 0;
  }), "disconnect must emit only key-up events followed by synchronization");
  check(releases.back().type == EV_SYN, "disconnect key-up events need a final synchronization");
  keys.record(releases.data(), releases.size());
  check(keys.releases().empty(), "successfully released keys must not remain tracked");
  const input_event click[] = {{{}, EV_KEY, BTN_LEFT, 1}, {{}, EV_KEY, BTN_LEFT, 0}};
  keys.record(click, 2);
  check(keys.releases().empty(), "normal complete clicks must not add unnecessary releases");
  std::cout << "Device input regression checks passed\n";
}
