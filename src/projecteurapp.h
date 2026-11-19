// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include "devicescan.h"

#ifdef PROJECTEUR_WITH_KDE
#include <QApplication>
#include <QPointer>

#include <map>
#include <memory>
#else
#include <QCoreApplication>
#endif

class DeviceCommandHelper;
class GnomeOverlay;
class GnomePreferences;
class KAboutApplicationDialog;
class KActionCollection;
class KDBusService;
class LinuxDesktop;
class PreferencesDialog;
class PresentationTimer;
class ProjecteurControl;
class QQmlApplicationEngine;
class QQmlComponent;
class Settings;
class Spotlight;
class QTimer;

#ifdef PROJECTEUR_WITH_KDE
class ProjecteurApplication : public QApplication
#else
class ProjecteurApplication : public QCoreApplication
#endif
{
  Q_OBJECT
  Q_PROPERTY(bool overlayVisible READ overlayVisible NOTIFY overlayVisibleChanged)
#ifdef PROJECTEUR_WITH_KDE
  Q_PROPERTY(quint64 currentSpotScreen READ currentSpotScreen NOTIFY currentSpotScreenChanged)
  Q_PROPERTY(QPoint currentCursorPos READ currentCursorPos NOTIFY currentCursorPosChanged)
#endif

public:
  struct Options {
    QString configFile;
    bool enableUInput = true; // enable virtual uinput device
    bool showPreferencesOnStart = false;
    bool activateExisting = false;
    bool dialogMinimizeOnly = false;
    bool disableOverlay = false;
    bool hideSysTrayIcon = false;
    QStringList commands;
    std::vector<SupportedDevice> additionalDevices;
  };

  explicit ProjecteurApplication(int &argc, char **argv, const Options& options);
  virtual ~ProjecteurApplication() override;

#ifdef PROJECTEUR_WITH_KDE
  KDBusService* dbusService() const { return m_dbusService; }
#endif
  bool isPrimaryInstance() const { return m_primaryInstance; }
  int startupExitCode() const { return m_startupExitCode; }
  bool overlayVisible() const { return m_overlayVisible; }
  void activate();
  void applyCommands(const QStringList& commands);

signals:
  void overlayVisibleChanged(bool visible);
#ifdef PROJECTEUR_WITH_KDE
  void currentSpotScreenChanged(quint64 screen);
  void currentCursorPosChanged(const QPoint& pos);

public slots:
  void cursorExitedWindow();
  void cursorEntered(quint64 screen);
  void spotlightWindowClicked();
  void cursorPositionChanged(const QPoint& pos);
#endif

private:
  friend class ProjecteurControl;

  void applyCommand(const QString& command);
  void showPreferences(bool show = true);
  void showAbout();
  void setupControlService(Options const& options);
  void setupNotifications();
  void setupGnomeOverlay(Options const& options);

#ifdef PROJECTEUR_WITH_KDE
  void showAndActivate(QWidget* widget);
  void setScreenForCursorPos();
  QScreen* screenAtCursorPos() const;
  QWindow* createOverlayWindow();
  void updateOverlayWindow(QWindow* window, QScreen* screen);
  void setupScreenOverlays();
  quint64 currentSpotScreen() const;
  void setCurrentSpotScreen(quint64 screen);
  QPoint currentCursorPos() const;
  void setCurrentCursorPos(const QPoint& pos);
  void setupGlobalShortcuts();
  void setupSpotlight();

  std::unique_ptr<PreferencesDialog> m_dialog;
  QPointer<KAboutApplicationDialog> m_aboutDialog;
  KActionCollection* m_actionCollection = nullptr;
  KDBusService* m_dbusService = nullptr;
  LinuxDesktop* m_linuxDesktop = nullptr;
  QQmlApplicationEngine* m_qmlEngine = nullptr;
  QQmlComponent* m_windowQmlComponent = nullptr;
  QList<QWindow*> m_overlayWindows;
  std::map<QScreen*, QWindow*> m_screenWindowMap;
  quint64 m_currentSpotScreen = 0;
  QPoint m_currentCursorPos;
#endif

  ProjecteurControl* m_control = nullptr;
  Settings* m_settings = nullptr;
  Spotlight* m_spotlight = nullptr;
  DeviceCommandHelper* m_deviceCommandHelper = nullptr;
  PresentationTimer* m_presentationTimer = nullptr;
  GnomeOverlay* m_gnomeOverlay = nullptr;
#ifdef PROJECTEUR_WITH_GNOME
  GnomePreferences* m_gnomePreferences = nullptr;
#endif
  QTimer* m_previewTimer = nullptr;
  bool m_primaryInstance = false;
  int m_startupExitCode = 0;
  bool m_overlayVisible = false;

};
