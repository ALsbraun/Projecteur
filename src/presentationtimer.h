// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md
#pragma once

#include <chrono>

#ifdef PROJECTEUR_NATIVE_GNOME
#include "native/platform.h"
#else
#include <QObject>
class QTimer;
#endif

class DeviceCommandHelper;
class Settings;
class Spotlight;

class PresentationTimer : public QObject
{
  Q_OBJECT

public:
  enum class State { Idle, Running, Completed };
  Q_ENUM(State)

  PresentationTimer(Settings* settings, Spotlight* spotlight,
                    DeviceCommandHelper* deviceCommandHelper, QObject* parent = nullptr);

  State state() const { return m_state; }
  QString stateName() const;
  bool enabled() const { return m_enabled; }
  int durationSeconds() const { return m_durationSeconds; }
  int remainingSeconds() const { return m_remainingSeconds; }

#ifdef PROJECTEUR_NATIVE_GNOME
public:
#else
public slots:
#endif
  void setEnabled(bool enabled);
  void start();
  void restart();
  void reset();
  void setDurationSeconds(int seconds);

#ifdef PROJECTEUR_NATIVE_GNOME
public:
  native::Signal<bool> enabledChanged;
  native::Signal<PresentationTimer::State> stateChanged;
  native::Signal<int> durationSecondsChanged;
  native::Signal<int> remainingSecondsChanged;
#else
signals:
  void enabledChanged(bool enabled);
  void stateChanged(PresentationTimer::State state);
  void durationSecondsChanged(int seconds);
  void remainingSecondsChanged(int seconds);
#endif

#ifdef PROJECTEUR_NATIVE_GNOME
private:
#else
private slots:
#endif
  void updateRemaining();

private:
  using Clock = std::chrono::steady_clock;

  void beginCountdown();
  void setState(State state);
  void setRemainingSeconds(int seconds);

  Settings* const m_settings;
  Spotlight* const m_spotlight;
  DeviceCommandHelper* const m_deviceCommandHelper;
  QTimer* const m_updateTimer;
  Clock::time_point m_deadline;
  bool m_enabled = false;
  State m_state = State::Idle;
  int m_durationSeconds = 15 * 60;
  int m_remainingSeconds = m_durationSeconds;
};
