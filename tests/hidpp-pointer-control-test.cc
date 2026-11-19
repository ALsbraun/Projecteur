// This file is part of Projecteur - see LICENSE.md.
#include "device-hidpp.h"
#include "deviceinput.h"
#include "presentercontrols.h"
#ifdef PROJECTEUR_NATIVE_GNOME
#include "native/settings.h"
#else
#include "settings.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QThread>
#endif

#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sys/socket.h>
#include <unistd.h>

namespace {
void check(bool success, const char* message)
{
  if (!success) { std::cerr << message << '\n'; std::exit(1); }
}

template<typename Predicate>
void waitFor(Predicate condition, const char* message, int timeout = 2000)
{
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < timeout) {
#ifdef PROJECTEUR_NATIVE_GNOME
    g_main_context_iteration(nullptr, false);
    g_usleep(1000);
#else
    QCoreApplication::processEvents();
    QThread::msleep(1);
#endif
  }
  check(condition(), message);
}
}

#ifndef PROJECTEUR_NATIVE_GNOME
const Settings::SettingRange<int>& Settings::inputSequenceIntervalRange()
{
  static const SettingRange<int> range{50, 2000};
  return range;
}
#endif

// A packet socket replaces only the physical hidraw endpoint. Initialization,
// request matching, timeout handling, readback and event dispatch use the real
// SubHidppConnection implementation. No virtual or physical input is opened.
class HidppPointerControlTest : public SubHidppConnection
{
public:
  enum class Scenario { ZeroAck, WrongReadbackCid, DisabledReadback, SetError, LostAck };

  explicit HidppPointerControlTest(Scenario scenario)
    : SubHidppConnection(Token{}, DeviceId{0x046d, 0xb503, BusType::Bluetooth, {}},
                        DeviceScan::SubDevice{}), m_scenario(scenario)
  {
    int sockets[2];
    check(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
                       0, sockets) == 0, "cannot create simulated HID++ transport");
    m_peer = sockets[1];
    createSocketNotifiers(sockets[0], QStringLiteral("simulated Spotlight"));
    m_inputMapper = std::make_shared<InputMapper>(nullptr, nullptr);
    connect(socketReadNotifier(), &QSocketNotifier::activated, this,
            &SubHidppConnection::onHidppDataAvailable);
    const auto peerNotifier = new QSocketNotifier(m_peer, QSocketNotifier::Read, this);
    connect(peerNotifier, &QSocketNotifier::activated, this, [this]() { answerRequest(); });
    connect(this, &SubHidppConnection::pointerModeCycleRequested,
            this, [this]() { ++modeCycles; });
    connect(this, &SubHidppConnection::pointerButtonChanged,
            this, [this](bool pressed) { holds.push_back(pressed); });
    initPresenter({});
  }

  ~HidppPointerControlTest() override
  {
    disconnect();
    ::close(m_peer);
  }

  bool initializationFinished() const { return m_lastRowAnswered && m_requests.empty(); }

  void buttons(uint16_t cid, uint8_t deviceIndex = 0xff)
  {
    const HIDPP::Message report(HIDPP::Message::Type::Long, deviceIndex, 7, 0, 0,
                               {uint8_t(cid >> 8), uint8_t(cid)});
    writeReport(report);
    // All packets preceding this ping must have been consumed before its reply.
    bool pong = false;
    sendPing([&pong](MsgResult result, HIDPP::Message&&) { pong = result == MsgResult::Ok; });
    waitFor([&pong]() { return pong; }, "notification dispatch did not complete");
  }

  int modeCycles = 0;
  std::vector<bool> holds;
  int highlightReadbacks = 0;

private:
  void writeReport(const HIDPP::Message& report)
  {
    check(::write(m_peer, report.data(), report.size()) == ssize_t(report.size()),
          "simulated firmware could not send its report");
  }

  void answerRequest()
  {
    std::vector<uint8_t> bytes(HIDPP::Message::LONG_MSG_SIZE);
    const auto count = ::read(m_peer, bytes.data(), bytes.size());
    if (count < 0) { return; }
    check(count == HIDPP::Message::LONG_MSG_SIZE, "Bluetooth requests must use long reports");
    const HIDPP::Message request(std::move(bytes));
    HIDPP::Message::Data payload;
    bool error = false;
    if (request.featureIndex() == 0 && request.function() == 1) {
      payload = {6, 4, request[6]}; // Root ping, no firmware data/cache needed.
    } else if (request.featureIndex() == 0 && request.function() == 0) {
      const auto feature = PresenterControls::controlId(request[4], request[5]);
      if (feature == 1) { payload = {1, 0, 0}; }
      else if (feature == 3) { error = true; }
      else { check(false, "unexpected feature discovery request"); }
    } else if (request.featureIndex() == 1 && request.function() == 0) {
      payload = {7};
    } else if (request.featureIndex() == 1 && request.function() == 1) {
      payload = request[4] == 7 ? HIDPP::Message::Data{0x1b, 0x04, 0, 3}
                               : HIDPP::Message::Data{0xee, request[4], 0x40, 0};
    } else if (request.featureIndex() == 7 && request.function() == 0) {
      payload = {7};
    } else if (request.featureIndex() == 7 && request.function() == 1) {
      constexpr std::array<uint8_t, 7> controls{0x50, 0xd8, 0xdf, 0xd9, 0xda, 0xdb, 0xdc};
      check(request[4] < controls.size(), "control enumeration exceeded the device table");
      const auto cid = controls[request[4]];
      payload = {0, cid, 0, 0, 0x34, 0, 0, 0, uint8_t(cid == 0xd8 ? 1 : 0)};
      m_lastRowAnswered = request[4] == controls.size() - 1;
    } else if (request.featureIndex() == 7 && request.function() == 2) {
      const auto cid = PresenterControls::controlId(request[4], request[5]);
      const bool afterSet = m_written.count(cid);
      uint8_t reportedCid = uint8_t(cid);
      uint8_t state = m_state[cid];
      if (cid == PresenterControls::SwitchHighlight && afterSet) {
        ++highlightReadbacks;
        if (m_scenario == Scenario::WrongReadbackCid) { reportedCid = 0xd8; }
        if (m_scenario == Scenario::DisabledReadback) { state = 0; }
      }
      payload = {0, reportedCid, state, 0, 0, 0};
    } else if (request.featureIndex() == 7 && request.function() == 3) {
      const auto cid = PresenterControls::controlId(request[4], request[5]);
      m_written.insert(cid);
      if (cid == PresenterControls::SwitchHighlight && m_scenario == Scenario::SetError) {
        error = true;
      } else {
        m_state[cid] = request[6] & 0x11;
        if (cid == PresenterControls::SwitchHighlight && m_scenario == Scenario::LostAck) { return; }
        // Captured Spotlight 1 firmware behavior: correct response header with
        // an entirely zero payload, not an echo of CID or write-valid flags.
        payload = {};
      }
    } else {
      check(false, "unexpected HID++ request");
    }
    if (error) {
      writeReport(HIDPP::Message(HIDPP::Message::Type::Long, request.deviceIndex(),
        0xff, request.featureIndex() >> 4, request.featureIndex() & 15,
        {request.address(), uint8_t(HIDPP::Error::Unsupported)}));
    } else {
      writeReport(HIDPP::Message(HIDPP::Message::Type::Long, request.deviceIndex(),
        request.featureIndex(), request.function(), request.softwareId(), std::move(payload)));
    }
  }

  int m_peer = -1;
  const Scenario m_scenario;
  bool m_lastRowAnswered = false;
  std::map<uint16_t, uint8_t> m_state;
  std::set<uint16_t> m_written;
};

int main(int argc, char* argv[])
{
#ifdef PROJECTEUR_NATIVE_GNOME
  (void)argc;
  (void)argv;
#else
  QCoreApplication app(argc, argv);
#endif
  using Scenario = HidppPointerControlTest::Scenario;
  for (const auto scenario : {Scenario::ZeroAck, Scenario::WrongReadbackCid,
                             Scenario::DisabledReadback, Scenario::SetError, Scenario::LostAck}) {
    HidppPointerControlTest device(scenario);
    waitFor([&device]() { return device.initializationFinished(); },
            "presenter initialization did not finish", 6000);
    const bool accepted = scenario == Scenario::ZeroAck || scenario == Scenario::LostAck;
    check(device.highlightReadbacks == (scenario == Scenario::SetError ? 0 : 1),
          "success/timeout must read back state; explicit errors must not enable controls");
    device.buttons(PresenterControls::SwitchHighlight, 2);
    check(device.modeCycles == 0, "another device index must not cycle the mode");
    device.buttons(PresenterControls::SwitchHighlight);
    device.buttons(PresenterControls::SwitchHighlight);
    device.buttons(0);
    check(device.modeCycles == (accepted ? 1 : 0),
          "one confirmed Switch Highlight press/release must cycle exactly once");
    device.buttons(PresenterControls::PointerHold);
    device.buttons(PresenterControls::PointerHold);
    device.buttons(0);
    check(device.holds == std::vector<bool>({true, false}),
          "pointer hold/release must remain functional when mode control is accepted or rejected");
  }
  std::cout << "HID++ pointer initialization and notification checks passed\n";
}
