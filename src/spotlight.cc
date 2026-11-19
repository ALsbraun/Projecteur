// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "spotlight.h"

#include "device-hidpp.h"
#include "deviceinput.h"
#include "inputevents.h"
#include "presentercontrols.h"
#include "projecteur-logging.h"
#ifdef PROJECTEUR_NATIVE_GNOME
#include "native/settings.h"
#else
#include "settings.h"
#endif
#include "virtualdevice.h"

#include "projecteur-i18n.h"

#ifdef PROJECTEUR_NATIVE_GNOME
#include "native/files.h"
#else
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QTimer>
#include <QVarLengthArray>
#endif

#include <cmath>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
  const auto hexId = formatHexId;

  QElapsedTimer lastLogitechSlideNavigation;

  bool isLogitechSpotlight(const DeviceId& id)
  {
    return id.vendorId == 0x46d
      && (id.productId == 0xc53e
          || id.productId == 0xb503
          || id.productId == 0xc548
          || id.productId == 0xb506);
  }

} // end anonymous namespace


// -------------------------------------------------------------------------------------------------
// Hold button state. Very much Logitech Spotlight specific.
struct HoldButtonStatus
{
  void setButtonsPressed(bool nextPressed, bool backPressed)
  {
    if (!m_nextPressed && nextPressed) {
      m_moveKeyEvSeq = SpecialKeys::eventSequenceInfo(SpecialKeys::Key::NextHoldMove).keyEventSeq;
    } else if (!m_backPressed && backPressed) {
      m_moveKeyEvSeq = SpecialKeys::eventSequenceInfo(SpecialKeys::Key::BackHoldMove).keyEventSeq;
    } else if (m_nextPressed && !nextPressed && backPressed) {
      m_moveKeyEvSeq = SpecialKeys::eventSequenceInfo(SpecialKeys::Key::BackHoldMove).keyEventSeq;
    } else if (m_backPressed && !backPressed && nextPressed) {
      m_moveKeyEvSeq = SpecialKeys::eventSequenceInfo(SpecialKeys::Key::NextHoldMove).keyEventSeq;
    }

    m_nextPressed = nextPressed;
    m_backPressed = backPressed;

    if (!nextPressed && !backPressed) { m_moveKeyEvSeq.clear(); }
  }

  bool nextPressed() const { return m_nextPressed; }
  bool backPressed() const { return m_backPressed; }

  void reset() { m_nextPressed = m_backPressed = false; m_moveKeyEvSeq.clear(); };

  const KeyEventSequence& moveKeyEventSeq() const {
    return m_moveKeyEvSeq;
  };

private:
  bool m_nextPressed = false;
  bool m_backPressed = false;

  KeyEventSequence m_moveKeyEvSeq;
};

// -------------------------------------------------------------------------------------------------
Spotlight::Spotlight(QObject* parent, Options options, Settings* settings)
  : QObject(parent)
  , m_options(std::move(options))
  , m_activeTimer(new QTimer(this))
  , m_connectionTimer(new QTimer(this))
  , m_holdMoveEventTimer(new QTimer(this))
  , m_inputRetryTimer(new QTimer(this))
  , m_settings(settings)
  , m_holdButtonStatus(std::make_unique<HoldButtonStatus>())
{
  constexpr int spotlightActiveTimoutMs = 600;
  m_activeTimer->setSingleShot(true);
  m_activeTimer->setInterval(spotlightActiveTimoutMs);

  connect(m_activeTimer, &QTimer::timeout, this, [this](){
    if (m_pointerActivity.shouldExpire()) { setSpotActive(false); }
  });

  m_inputRetryTimer->setInterval(2000);
  m_inputRetryTimer->setTimerType(Qt::CoarseTimer);
  connect(m_inputRetryTimer, &QTimer::timeout, this, &Spotlight::retryInputDevices);
  if (m_options.enableUInput) { retryInputDevices(); }
  else {
    qCInfo(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Virtual device initialization was skipped.");
  }

  m_connectionTimer->setSingleShot(true);
  // From detecting a change with inotify, the device needs some time to be ready for open,
  // otherwise opening the device will fail.
  // TODO: This interval seems to work, but it is arbitrary - there should be a better way.
  constexpr int delayedConnectionTimerIntervalMs = 800;
  m_connectionTimer->setInterval(delayedConnectionTimerIntervalMs);

  connect(m_connectionTimer, &QTimer::timeout, this, [this]() {
    qCDebug(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("New connection check triggered");
    connectDevices();
  });

  m_holdMoveEventTimer->setSingleShot(true);
  m_holdMoveEventTimer->setInterval(30);

  // Try to find already attached device(s) and connect to it.
  connectDevices();
  setupDevEventInotify();
}

// -------------------------------------------------------------------------------------------------
Spotlight::~Spotlight() = default;

#ifdef PROJECTEUR_NATIVE_GNOME
void Spotlight::shutdown()
{
  // Destroy the HID++ connections before the signals' receivers and the
  // virtual input devices. The HID++ destructors restore device reporting.
  auto connections = std::move(m_deviceConnections);
  for (const auto& [id, device] : connections) {
    for (const auto& [path, subDevice] : device->subDevices()) { subDevice->disconnect(); }
  }
  if (m_virtualMouseDevice) { m_virtualMouseDevice->releasePressedKeys(); }
  if (m_virtualKeyDevice) { m_virtualKeyDevice->releasePressedKeys(); }
}
#endif

bool Spotlight::inputMappingAvailable() const
{
  return bool(m_virtualMouseDevice) && bool(m_virtualKeyDevice);
}

QString Spotlight::inputMappingStatus() const
{
  if (inputMappingAvailable()) { return {}; }
  if (!m_options.enableUInput) {
    return i18n("Button mapping was disabled at startup. Restart Projecteur without --disable-uinput to enable it.");
  }
  if (!QFileInfo::exists(QStringLiteral("/dev/uinput"))) {
    return i18n("Button mapping is waiting for the virtual input device. Install the current Projecteur RPM to load the required input driver. Access is checked again automatically.");
  }
  if (::access("/dev/uinput", W_OK) != 0) {
    return i18n("Button mapping is waiting for permission to use the virtual input device. Update the Projecteur RPM to apply its device access rules. Access is checked again automatically.");
  }
  return i18n("The virtual input devices could not be created. Retry device access or check the Projecteur log.");
}

QString Spotlight::inputMappingStatus(const DeviceId& deviceId) const
{
  const auto status = inputMappingStatus();
  if (!status.isEmpty()) { return status; }
  const auto found = m_deviceConnections.find(deviceId);
  if (found == m_deviceConnections.end()) {
    return i18n("Connect the presenter to use its button mappings.");
  }
  for (const auto& [path, subDevice] : found->second->subDevices()) {
    Q_UNUSED(path)
    if (const auto event = dynamic_cast<SubEventConnection*>(subDevice.get()); event && !event->isGrabbed()) {
      return i18n("Button mapping is waiting for access to the presenter. Release its buttons and close other apps using it. Access is checked again automatically.");
    }
  }
  return {};
}

void Spotlight::retryInputDevices()
{
  if (!m_options.enableUInput) { return; }
  if (::access("/dev/uinput", W_OK) == 0) {
    if (!m_virtualMouseDevice) {
      m_virtualMouseDevice = VirtualDevice::create(VirtualDevice::Type::Mouse, "Projecteur_virtual_mouse");
    }
    if (!m_virtualKeyDevice) {
      m_virtualKeyDevice = VirtualDevice::create(VirtualDevice::Type::Keyboard, "Projecteur_virtual_keyboard");
    }
  }
  bool allGrabbed = inputMappingAvailable();
  for (const auto& [id, device] : m_deviceConnections) {
    Q_UNUSED(id)
    device->inputMapper()->setVirtualDevices(m_virtualMouseDevice, m_virtualKeyDevice);
    if (!inputMappingAvailable()) { continue; }
    for (const auto& [path, subDevice] : device->subDevices()) {
      Q_UNUSED(path)
      if (const auto event = dynamic_cast<SubEventConnection*>(subDevice.get())) {
        if (!event->tryGrabInput()) { allGrabbed = false; }
      }
    }
  }
  if (allGrabbed) { m_inputRetryTimer->stop(); }
  else if (!m_inputRetryTimer->isActive()) { m_inputRetryTimer->start(); }
  emit inputDevicesChanged();
}

// -------------------------------------------------------------------------------------------------
bool Spotlight::anySpotlightDeviceConnected() const
{
  for (const auto& dc : m_deviceConnections) {
    if (dc.second->subDeviceCount()) { return true; }
  }
  return false;
}

// -------------------------------------------------------------------------------------------------
uint32_t Spotlight::connectedDeviceCount() const
{
  uint32_t count = 0;
  for (const auto& dc : m_deviceConnections) {
    if (dc.second->subDeviceCount()) { ++count; }
  }
  return count;
}

// -------------------------------------------------------------------------------------------------
void Spotlight::setSpotActive(bool active)
{
  if (m_spotActive == active) { return; }
  m_spotActive = active;
  if (!m_spotActive) { m_activeTimer->stop(); }
  emit spotActiveChanged(m_spotActive);
}

// -------------------------------------------------------------------------------------------------
std::shared_ptr<DeviceConnection> Spotlight::deviceConnection(const DeviceId& deviceId)
{
  const auto find_it = m_deviceConnections.find(deviceId);
  return (find_it != m_deviceConnections.end()) ? find_it->second : std::shared_ptr<DeviceConnection>();
}

// -------------------------------------------------------------------------------------------------
std::vector<Spotlight::ConnectedDeviceInfo> Spotlight::connectedDevices() const
{
  std::vector<ConnectedDeviceInfo> devices;
  devices.reserve(m_deviceConnections.size());
  for (const auto& dc : m_deviceConnections) {
    devices.emplace_back(ConnectedDeviceInfo{ dc.first, dc.second->deviceName() });
  }
  return devices;
}

// -------------------------------------------------------------------------------------------------
int Spotlight::connectDevices()
{
  const auto scanResult = DeviceScan::getDevices(m_options.additionalDevices);

  for (const auto& dev : scanResult.devices)
  {
    auto& dc = m_deviceConnections[dev.id];
    if (!dc) {
      dc = std::make_shared<DeviceConnection>(
        dev.id, dev.getName(), m_virtualMouseDevice, m_virtualKeyDevice);
    }

    const bool anyConnectedBefore = anySpotlightDeviceConnected();
    for (const auto& scanSubDevice : dev.subDevices)
    {
      const bool requiresWriteAccess =
        scanSubDevice.type == DeviceScan::SubDevice::Type::Hidraw;
      if (!scanSubDevice.deviceReadable
          || (requiresWriteAccess && !scanSubDevice.deviceWritable))
      {
        qCWarning(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Sub-device not accessible: %1 (%2:%3) %4").arg(dc->deviceName()).arg(hexId(dev.id.vendorId)).arg(hexId(dev.id.productId)).arg(scanSubDevice.deviceFile);
        QTimer::singleShot(
          0, this,
          [this, name = dc->deviceName(), path = scanSubDevice.deviceFile]() {
            emit deviceAccessError(name, path);
          });
        continue;
      }
      if (dc->hasSubDevice(scanSubDevice.deviceFile)) { continue; }

      std::shared_ptr<SubDeviceConnection> subDeviceConnection =
      [&scanSubDevice, &dc, this]() -> std::shared_ptr<SubDeviceConnection>
      { // Input event sub devices
        if (scanSubDevice.type == DeviceScan::SubDevice::Type::Event) {
          auto devCon = SubEventConnection::create(scanSubDevice, *dc);
          if (addInputEventHandler(devCon)) { return devCon; }
        } // Hidraw sub devices
        else if (scanSubDevice.type == DeviceScan::SubDevice::Type::Hidraw)
        {
          if (dc->hasHidppSupport())
          {
            if (auto hidppCon = SubHidppConnection::create(scanSubDevice, *dc))
            {
              QPointer<SubHidppConnection> connPtr(hidppCon.get());

              connect(&*hidppCon, &SubHidppConnection::pointerButtonChanged, this,
                [this, id = dc->deviceId(), path = scanSubDevice.deviceFile](bool pressed) {
                  const auto transition = m_pointerActivity.observe(id, path, pressed);
                  if (transition == PresenterControls::PointerTransition::Activate) {
                    m_activeTimer->stop();
                    setSpotActive(true);
                  } else if (transition == PresenterControls::PointerTransition::Deactivate) {
                    setSpotActive(false);
                  }
                });
              connect(&*hidppCon, &SubHidppConnection::pointerModeCycleRequested,
                      this, &Spotlight::pointerModeCycleRequested);
              connect(&*hidppCon, &SubHidppConnection::presenterStateChanged, this,
                [this, id = dc->deviceId()](SubHidppConnection::PresenterState state) {
                  if (state == SubHidppConnection::PresenterState::Initialized_Offline
                      || state == SubHidppConnection::PresenterState::Uninitialized_Offline
                      || state == SubHidppConnection::PresenterState::Error) {
                    if (m_pointerActivity.removeDevice(id)) { setSpotActive(false); }
                    m_holdButtonStatus->reset();
                    if (const auto dc = deviceConnection(id)) { dc->inputMapper()->resetState(); }
                    if (m_virtualMouseDevice) { m_virtualMouseDevice->releasePressedKeys(); }
                    if (m_virtualKeyDevice) { m_virtualKeyDevice->releasePressedKeys(); }
                  }
                });

              connect(&*hidppCon, &SubHidppConnection::featureSetInitialized, this,
              [this, connPtr](){
                if (!connPtr) { return; }
                this->registerForNotifications(connPtr.data());
              });

              // Remove device on socketReadError
              connect(&*hidppCon, &SubHidppConnection::socketReadError, this, [this, connPtr](){
                if (!connPtr) { return; }
                const bool anyConnectedBefore = anySpotlightDeviceConnected();
                connPtr->disconnect();
                QTimer::singleShot(0, this, [this, devicePath=connPtr->path(), anyConnectedBefore](){
                  removeDeviceConnection(devicePath);
                  if (!anySpotlightDeviceConnected() && anyConnectedBefore) {
                    emit anySpotlightDeviceConnectedChanged(false);
                  }
                });
              });

              return hidppCon;
            }
          }
          else if (auto hidrawConn = SubHidrawConnection::create(scanSubDevice, *dc))
          {
            QPointer<SubHidrawConnection> connPtr(hidrawConn.get());
            // Remove device on socketReadError
            connect(&*hidrawConn, &SubHidrawConnection::socketReadError, this, [this, connPtr](){
              if (!connPtr) { return; }
              const bool anyConnectedBefore = anySpotlightDeviceConnected();
              connPtr->disconnect();
              QTimer::singleShot(0, this, [this, devicePath=connPtr->path(), anyConnectedBefore](){
                removeDeviceConnection(devicePath);
                if (!anySpotlightDeviceConnected() && anyConnectedBefore) {
                  emit anySpotlightDeviceConnectedChanged(false);
                }
              });
            });

            return hidrawConn;
          }
        }
        return std::shared_ptr<SubDeviceConnection>();
      }();

      if (!subDeviceConnection) { continue; }

      if (dc->subDeviceCount() == 0) {
        // Load Input mapping settings when first sub-device gets added.
        const auto im = dc->inputMapper().get();

        im->setKeyEventInterval(m_settings->deviceInputSeqInterval(dev.id));
        im->setConfiguration(m_settings->getDeviceInputMapConfig(dev.id));

        connect(im, &InputMapper::configurationChanged, this, [this, id=dev.id, im]() {
          m_settings->setDeviceInputMapConfig(id, im->configuration());
        });

        static QString lastPreset;

        connect(im, &InputMapper::actionMapped, this, [this](const std::shared_ptr<Action>& action)
        {
          if (action->type() == Action::Type::CyclePresets)
          {
            auto it = std::find(m_settings->presets().cbegin(), m_settings->presets().cend(), lastPreset);
            if ((it == m_settings->presets().cend()) || (++it == m_settings->presets().cend())) {
              it = m_settings->presets().cbegin();
            }

            if (it != m_settings->presets().cend())
            {
              lastPreset = *it;
              m_settings->loadPreset(lastPreset);
            }
          }
          else if (action->type() == Action::Type::ToggleSpotlight)
          {
            m_settings->setOverlayDisabled(!m_settings->overlayDisabled());
          }
          else if (action->type() == Action::Type::ScrollHorizontal || action->type() == Action::Type::ScrollVertical)
          {
            if (!m_virtualMouseDevice) { return; }

            const int param = (action->type() == Action::Type::ScrollHorizontal)
              ? static_cast<ScrollHorizontalAction*>(action.get())->param
              : static_cast<ScrollVerticalAction*>(action.get())->param;

            if (param)
            {
              const uint16_t wheelCode = (action->type() == Action::Type::ScrollHorizontal) ? REL_HWHEEL : REL_WHEEL;
              const std::vector<input_event> scrollInputEvents = {{{}, EV_REL, wheelCode, param}, {{}, EV_SYN, SYN_REPORT, 0},};
              m_virtualMouseDevice->emitEvents(scrollInputEvents);
            }
          }
          else if (action->type() == Action::Type::VolumeControl)
          {
            if (!m_virtualKeyDevice) { return; }

            auto param = static_cast<VolumeControlAction*>(action.get())->param;
            uint16_t keyCode = (param > 0)? KEY_VOLUMEUP: KEY_VOLUMEDOWN;
            const std::vector<input_event> curVolInputEvents = {{{}, EV_KEY, keyCode, 1}, {{}, EV_SYN, SYN_REPORT, 0},
                                                                {{}, EV_KEY, keyCode, 0}, {{}, EV_SYN, SYN_REPORT, 0},};
            if (param) { m_virtualKeyDevice->emitEvents(curVolInputEvents); }
          }
        });

        connect(m_settings, &Settings::presetLoaded, this, [](const QString& preset){
          lastPreset = preset;
        });
      }

      dc->addSubDevice(std::move(subDeviceConnection));
      if (dc->subDeviceCount() == 1)
      {
        QTimer::singleShot(0, this,
        [this, id = dev.id, devName = dc->deviceName(), anyConnectedBefore](){
          qCInfo(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Connected device: %1 (%2:%3)").arg(devName).arg(hexId(id.vendorId)).arg(hexId(id.productId));
          emit deviceConnected(id, devName);
          if (!anyConnectedBefore) { emit anySpotlightDeviceConnectedChanged(true); }
        });
      }

      qCDebug(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Connected sub-device: %1 (%2:%3) %4").arg(dc->deviceName()).arg(hexId(dev.id.vendorId)).arg(hexId(dev.id.productId)).arg(scanSubDevice.deviceFile);
      emit subDeviceConnected(dev.id, dc->deviceName(), scanSubDevice.deviceFile);
    }

    if (dc->subDeviceCount() == 0) {
      m_deviceConnections.erase(dev.id);
    }
  }
  // A newly discovered event device may be busy or have a button held. Retry
  // after connection setup, including when uinput was ready before discovery.
  QTimer::singleShot(0, this, &Spotlight::retryInputDevices);
  return m_deviceConnections.size();
}

// -------------------------------------------------------------------------------------------------
void Spotlight::removeDeviceConnection(const QString &devicePath)
{
  if (m_pointerActivity.removeConnection(devicePath)) {
    setSpotActive(false);
  }
  for (auto dc_it = m_deviceConnections.begin(); dc_it != m_deviceConnections.end(); )
  {
    if (!dc_it->second) {
      dc_it = m_deviceConnections.erase(dc_it);
      continue;
    }

    auto& dc = dc_it->second;
    if (dc->removeSubDevice(devicePath)) {
      // A wireless disconnect can lose the native key-up report. The virtual
      // devices are shared; release all their pressed keys as a fail-safe.
      dc->inputMapper()->resetState();
      if (m_virtualMouseDevice) { m_virtualMouseDevice->releasePressedKeys(); }
      if (m_virtualKeyDevice) { m_virtualKeyDevice->releasePressedKeys(); }
      emit subDeviceDisconnected(dc_it->first, dc->deviceName(), devicePath);
    }

    if (dc->subDeviceCount() == 0)
    {
      if (m_pointerActivity.removeDevice(dc_it->first)) { setSpotActive(false); }
      m_holdButtonStatus->reset();
      qCInfo(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("Disconnected device: %1 (%2:%3)").arg(dc->deviceName()).arg(hexId(dc_it->first.vendorId)).arg(hexId(dc_it->first.productId));
      emit deviceDisconnected(dc_it->first, dc->deviceName());
      dc_it = m_deviceConnections.erase(dc_it);
    }
    else {
      ++dc_it;
    }
  }
}

// -------------------------------------------------------------------------------------------------
void Spotlight::onEventDataAvailable(int fd, SubEventConnection& connection)
{
  const bool isNonBlocking = connection.hasFlags(DeviceFlag::NonBlocking);
  while (true)
  {
    auto& buf = connection.inputBuffer();
    auto& ev = buf.current();
    const auto bytesRead = ::read(fd, &ev, sizeof(ev));
    if (bytesRead != sizeof(ev))
    {
      if (bytesRead < 0 && errno == EINTR) { continue; }
      if (bytesRead >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
      {
        const bool anyConnectedBefore = anySpotlightDeviceConnected();
        connection.disconnect();
        QTimer::singleShot(0, this, [this, devicePath=connection.path(), anyConnectedBefore](){
          removeDeviceConnection(devicePath);
          if (!anySpotlightDeviceConnected() && anyConnectedBefore) {
            emit anySpotlightDeviceConnectedChanged(false);
          }
        });
      }
      break;
    }
    ++buf;

    const bool isSlideNavigationKey =
      ev.type == EV_KEY
      && (ev.code == KEY_RIGHT || ev.code == KEY_LEFT
          || ev.code == KEY_PAGEDOWN || ev.code == KEY_PAGEUP);
    if (isSlideNavigationKey) {
      if (isLogitechSpotlight(connection.deviceId())) {
        lastLogitechSlideNavigation.restart();
      }
      if (ev.value == 1) {
        emit slideNavigationPressed();
      }
    }

    if (ev.type == EV_SYN && ev.code == SYN_REPORT)
    {
      // Check for relative events -> set Spotlight active
      const bool isMouseMoveEvent = InputEvents::hasPointerMotion(buf.data(), buf.pos());

      if (isMouseMoveEvent)
      { // Skip input mapping for mouse move events completely
        // Note: During a Next or Back button press the Logitech Spotlight device can send
        // move events via hid++ notifications. It seems that just when releasing the
        // next or back button sometimes a mouse move event 'leaks' through here as
        // relative input event causing the spotlight to be activated.
        // Suppress only moves immediately adjacent to slide navigation; skipping the
        // first move after every idle period makes genuine activation feel delayed.
        constexpr qint64 leakedMoveSuppressionMs = 250;
        const bool suppressLeakedMove =
          isLogitechSpotlight(connection.deviceId())
          && lastLogitechSlideNavigation.isValid()
          && lastLogitechSlideNavigation.elapsed() < leakedMoveSuppressionMs;
        // Once real hold reports have been observed, they are authoritative.
        // Stillness is not release, and trailing motion after release must not
        // reactivate the overlay. Unsupported firmware retains the fallback.
        const bool hasPointerReports = m_pointerActivity.hasReports(connection.deviceId());

        if (!hasPointerReports && !suppressLeakedMove && !spotActive()) {
          setSpotActive(true);
        }

        if (!hasPointerReports && !suppressLeakedMove && !m_pointerActivity.anyHeld()) {
          m_activeTimer->start();
        }
        if (connection.isGrabbed()) {
          // The compositor already receives ungrabbed events. Injecting them
          // again doubles pointer movement and can advance two slides at once.
          const auto frame = InputEvents::routeFrame(buf.data(), buf.pos());
          if (m_virtualMouseDevice) { m_virtualMouseDevice->emitEvents(frame.mouse); }
          if (m_virtualKeyDevice) { m_virtualKeyDevice->emitEvents(frame.keyboard); }
        }
      }
      else if (connection.isGrabbed())
      { // Forward events to input mapper for the device
        connection.inputMapper()->addEvents(buf.data(), buf.pos());
      }
      buf.reset();
    }
    else if (buf.pos() >= buf.size())
    { // No idea if this will ever happen, but log it to make sure we get notified.
      qCWarning(PROJECTEUR_DEVICE_LOG).noquote() << "Discarded" << buf.size()
                                      << "input events without EV_SYN.";
      connection.inputMapper()->resetState();
      buf.reset();
    }

    if (!isNonBlocking) { break; }
  } // end while loop
}

// -------------------------------------------------------------------------------------------------
void Spotlight::registerForNotifications(SubHidppConnection* connection)
{
  using namespace HIDPP;

  // Logitech button next and back press and hold + movement
  if (const auto rcIndex = connection->featureSet().featureIndex(FeatureCode::ReprogramControlsV4))
  {
    connection->registerNotificationCallback(this, rcIndex, makeSafeCallback(
    [this, connection](Message&& msg)
    {
      if (msg.size() < 8) { return; }
      // Logitech Spotlight:
      //   * Next Button = 0xda
      //   * Back Button = 0xdc
      // Byte 5 and 7 indicate pressed buttons
      // Back and next can be pressed at the same time

      constexpr uint8_t ButtonNext = 0xda;
      constexpr uint8_t ButtonBack = 0xdc;
      const auto isNextPressed = PresenterControls::contains(msg.data() + 4, msg.size() - 4, ButtonNext);
      const auto isBackPressed = PresenterControls::contains(msg.data() + 4, msg.size() - 4, ButtonBack);

      if (!m_holdButtonStatus->nextPressed() && isNextPressed)
      {
        const auto& nextHold = SpecialKeys::eventSequenceInfo(SpecialKeys::Key::NextHold);
        for (const auto& ke: nextHold.keyEventSeq) {
          connection->inputMapper()->addEvents(ke);
        }
      }

      if (!m_holdButtonStatus->backPressed() && isBackPressed)
      {
        const auto& backHold = SpecialKeys::eventSequenceInfo(SpecialKeys::Key::BackHold);
        for (const auto& ke: backHold.keyEventSeq) {
          connection->inputMapper()->addEvents(ke);
        }
      }

      m_holdButtonStatus->setButtonsPressed(isNextPressed, isBackPressed);
    }), 0 /* function 0 */);

    connection->registerNotificationCallback(this, rcIndex,
    makeSafeCallback([this, connection](Message&& msg)
    {
      if (msg.size() < 8) { return; }
      // Block some of the move events
      // TODO This works quiet okay in combination with adjusting x and y values,
      // but needs to be a more solid option to accumulate the mass of move events
      // and consolidate them to a number of meaningful action special key events.
      if (m_holdMoveEventTimer->isActive()) { return; }
      m_holdMoveEventTimer->start();

      // byte 4 : -1 for left movement, 0 for right movement
      // byte 5 : horizontal movement speed -128 to 127
      // byte 6 : -1 for up movement, 0 for down movement
      // byte 7 : vertical movement speed -128 to 127

      const int x = InputEvents::signedMotion(msg[4], msg[5]);
      const int y = InputEvents::signedMotion(msg[6], msg[7]);

      static const auto getReducedParam = [](int param) -> int {
        constexpr int divider = 5;
        constexpr int minimum = 5;
        constexpr int maximum = 10;
        if (std::abs(param) < minimum) { return 0; }
        const auto sign = (param == 0) ? 0 : ((param > 0) ? 1 : -1);
        return std::floor(1.0 * ((abs(param) > maximum)? sign * maximum : param) / divider);
      };

      const int adjustedX = getReducedParam(x);
      const int adjustedY = getReducedParam(y);

      if (adjustedX == 0 && adjustedY == 0) { return; }
      static const auto scrollHAction = GlobalActions::scrollHorizontal();
      scrollHAction->param = -adjustedX;

      static const auto scrollVAction = GlobalActions::scrollVertical();
      scrollVAction->param = adjustedY;

      static const auto volumeControlAction = GlobalActions::volumeControl();
      volumeControlAction->param = -adjustedY;

      if (!connection->inputMapper()->recordingMode())
      {
          for (const auto& key_event : m_holdButtonStatus->moveKeyEventSeq()) {
            connection->inputMapper()->addEvents(key_event);
          }
      }
    }), 1 /* function 1 */);
  }
}

// -------------------------------------------------------------------------------------------------
bool Spotlight::addInputEventHandler(std::shared_ptr<SubEventConnection> connection)
{
  if (!connection || connection->type() != ConnectionType::Event || !connection->isConnected()) {
    return false;
  }

  QSocketNotifier* const readNotifier = connection->socketReadNotifier();
  connect(readNotifier, &QSocketNotifier::activated, this,
  [this, weak = std::weak_ptr<SubEventConnection>(connection)](int fd) {
    if (const auto current = weak.lock()) { onEventDataAvailable(fd, *current); }
  });

  return true;
}

// -------------------------------------------------------------------------------------------------
bool Spotlight::setupDevEventInotify()
{
  int fd = -1;
#if defined(IN_CLOEXEC)
  fd = inotify_init1(IN_CLOEXEC);
#endif
  if (fd == -1)
  {
    fd = inotify_init();
    if (fd == -1) {
      qCCritical(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("inotify_init() failed. Detection of new attached devices will not work.");
      return false;
    }
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  // ACLs are applied after node creation; watch attribute changes as well. The
  // hidraw node can arrive after its input node, especially over Bluetooth.
  const int wd = inotify_add_watch(fd, "/dev/input", IN_CREATE | IN_ATTRIB | IN_DELETE);
  const int devWd = inotify_add_watch(fd, "/dev", IN_CREATE | IN_ATTRIB);

  if (wd < 0 && devWd < 0) {
    ::close(fd);
    qCCritical(PROJECTEUR_DEVICE_LOG).noquote() << QStringLiteral("inotify_add_watch for /dev/input returned with failure.");
    return false;
  }

  const auto notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
  connect(notifier, &QSocketNotifier::activated, this, [this](int fd)
  {
    int bytesAvaibable = 0;
    if (ioctl(fd, FIONREAD, &bytesAvaibable) < 0 || bytesAvaibable <= 0) {
      return; // Error or no bytes available
    }
    QVarLengthArray<char, 2048> buffer(bytesAvaibable);
    const auto bytesRead = read(fd, buffer.data(), static_cast<size_t>(bytesAvaibable));
    if (bytesRead <= 0) { return; }
    const char* at = buffer.data();
    const char* const end = at + bytesRead;
    while (at < end)
    {
      const auto event = reinterpret_cast<const inotify_event*>(at);

      const QString name = QString::fromLocal8Bit(event->name);
      if (name == QStringLiteral("input") && (event->mask & IN_CREATE)) {
        inotify_add_watch(fd, "/dev/input", IN_CREATE | IN_ATTRIB | IN_DELETE);
      }
      if ((event->mask & (IN_CREATE | IN_ATTRIB))
          && (name.startsWith(QStringLiteral("event"))
              || name.startsWith(QStringLiteral("hidraw"))))
      {
        // Trigger new device scan and connect if a new event device was created.
        m_connectionTimer->start();
      }
      if (name == QStringLiteral("uinput") && (event->mask & (IN_CREATE | IN_ATTRIB))) {
        retryInputDevices();
      }

      at += sizeof(inotify_event) + event->len;
    }
  });

  connect(notifier, &QSocketNotifier::destroyed, [fd]() {
    ::close(fd);
  });
  return true;
}
