// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#ifdef PROJECTEUR_NATIVE_GNOME
#include "native/platform.h"
#else
#include <QObject>
class QTimer;
#endif

#include <map>
#include <memory>
#include <vector>

#include "asynchronous.h"
#include "devicescan.h"
#include "presentercontrols.h"

class Settings;
class VirtualDevice;
class DeviceConnection;
class SubEventConnection;
class SubHidppConnection;

struct HoldButtonStatus;

/// Class handling spotlight device connections and indicating if a device is sending
/// sending mouse move events.
class Spotlight : public QObject, public async::Async<Spotlight>
{
  Q_OBJECT

public:
  struct Options {
    bool enableUInput = true; // enable virtual uinput device
    std::vector<SupportedDevice> additionalDevices;
  };

  explicit Spotlight(QObject* parent, Options options, Settings* settings);
  virtual ~Spotlight();

  bool spotActive() const { return m_spotActive; }
  void setSpotActive(bool active);

  struct ConnectedDeviceInfo {
    DeviceId id;
    QString name;
  };

  bool anySpotlightDeviceConnected() const;
  uint32_t connectedDeviceCount() const;
  std::vector<ConnectedDeviceInfo> connectedDevices() const;
  std::shared_ptr<DeviceConnection> deviceConnection(const DeviceId& deviceId);
  bool inputMappingAvailable() const;
  QString inputMappingStatus() const;
  QString inputMappingStatus(const DeviceId& deviceId) const;
  void retryInputDevices();
#ifdef PROJECTEUR_NATIVE_GNOME
  void shutdown();
#endif

#ifdef PROJECTEUR_NATIVE_GNOME
public:
  native::Signal<const DeviceId&, const QString&> deviceConnected;
  native::Signal<const DeviceId&, const QString&> deviceDisconnected;
  native::Signal<const DeviceId&, const QString&, const QString&> subDeviceConnected;
  native::Signal<const DeviceId&, const QString&, const QString&> subDeviceDisconnected;
  native::Signal<const QString&, const QString&> deviceAccessError;
  native::Signal<bool> anySpotlightDeviceConnectedChanged;
  native::Signal<bool> spotActiveChanged;
  native::Signal<> slideNavigationPressed;
  native::Signal<> pointerModeCycleRequested;
  native::Signal<> inputDevicesChanged;
#else
signals:
  void deviceConnected(const DeviceId& id, const QString& name);
  void deviceDisconnected(const DeviceId& id, const QString& name);
  void subDeviceConnected(const DeviceId& id, const QString& name, const QString& path);
  void subDeviceDisconnected(const DeviceId& id, const QString& name, const QString& path);
  void deviceAccessError(const QString& name, const QString& path);
  void anySpotlightDeviceConnectedChanged(bool connected);
  void spotActiveChanged(bool isActive);
  void slideNavigationPressed();
  void pointerModeCycleRequested();
  void inputDevicesChanged();
#endif

private:
  enum class ConnectionResult { CouldNotOpen, NotASpotlightDevice, Connected };
  ConnectionResult connectSpotlightDevice(const QString& devicePath, bool verbose = false);

  bool addInputEventHandler(std::shared_ptr<SubEventConnection> connection);
  void registerForNotifications(SubHidppConnection* connection);

  bool setupDevEventInotify();
  int connectDevices();
  void removeDeviceConnection(const QString& devicePath);
  void onEventDataAvailable(int fd, SubEventConnection& connection);

  const Options m_options;
  std::map<DeviceId, std::shared_ptr<DeviceConnection>> m_deviceConnections;
  PresenterControls::PointerActivity<DeviceId, QString> m_pointerActivity;

  QTimer* m_activeTimer = nullptr;
  QTimer* m_connectionTimer = nullptr;
  QTimer* m_holdMoveEventTimer = nullptr;
  QTimer* m_inputRetryTimer = nullptr;
  bool m_spotActive = false;
  std::shared_ptr<VirtualDevice> m_virtualMouseDevice;
  std::shared_ptr<VirtualDevice> m_virtualKeyDevice;
  Settings* m_settings = nullptr;
  std::unique_ptr<HoldButtonStatus> m_holdButtonStatus;
};
