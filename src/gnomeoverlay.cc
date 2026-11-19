// SPDX-License-Identifier: MIT
#include "gnomeoverlay.h"
#include "settings.h"
#include "projecteur-logging.h"

#include <QColor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaProperty>

namespace {
constexpr auto service = "org.projecteur.Gnome";
constexpr auto path = "/org/projecteur/Gnome";
constexpr auto interface = "org.projecteur.Gnome";
}

GnomeOverlay::GnomeOverlay(Settings* settings, QObject* parent)
  : QObject(parent), m_settings(settings)
{
  m_update.setSingleShot(true);
  connect(&m_update, &QTimer::timeout, this, &GnomeOverlay::sync);
  m_heartbeat.setInterval(2000);
  connect(&m_heartbeat, &QTimer::timeout, this, &GnomeOverlay::sync);

  // Coalesce changes when loading a preset into one coherent state update.
  const auto syncMethod = metaObject()->method(metaObject()->indexOfSlot("scheduleSync()"));
  const auto* meta = m_settings->metaObject();
  for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
    const auto property = meta->property(i);
    if (property.hasNotifySignal()) {
      connect(m_settings, property.notifySignal(), this, syncMethod);
    }
  }
  connect(m_settings, &Settings::overlayDisabledChanged, this, &GnomeOverlay::scheduleSync);

  auto* watcher = new QDBusServiceWatcher(QString::fromLatin1(service),
    QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange, this);
  connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
    [this](const QString&, const QString&, const QString& owner) {
      ++m_sequence; // Discard replies from an earlier extension instance.
      if (owner.isEmpty()) { setAvailable(false); emit visibleChanged(false); }
      else { scheduleSync(); }
    });
  scheduleSync();
}

void GnomeOverlay::setActive(bool active)
{
  m_active = active;
  if (active) { m_heartbeat.start(); }
  else { m_heartbeat.stop(); }
  scheduleSync();
}

void GnomeOverlay::scheduleSync()
{
  if (!m_stopping) { m_update.start(0); }
}

QString GnomeOverlay::settingsJson() const
{
  QJsonObject result;
  const auto* meta = m_settings->metaObject();
  for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
    const auto property = meta->property(i);
    const auto value = property.read(m_settings);
    if (value.metaType().id() == QMetaType::QColor) {
      result.insert(QString::fromLatin1(property.name()), value.value<QColor>().name());
    } else if (property.isEnumType()) {
      result.insert(QString::fromLatin1(property.name()), value.toInt());
    } else if (value.metaType().id() != QMetaType::QObjectStar) {
      result.insert(QString::fromLatin1(property.name()), QJsonValue::fromVariant(value));
    }
  }
  return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

void GnomeOverlay::sync()
{
  const bool active = m_active && !m_settings->overlayDisabled();
  auto message = QDBusMessage::createMethodCall(QString::fromLatin1(service),
    QString::fromLatin1(path), QString::fromLatin1(interface), QStringLiteral("SetState"));
  message << active << settingsJson();
  const auto sequence = ++m_sequence;
  auto* pending = new QDBusPendingCallWatcher(
    QDBusConnection::sessionBus().asyncCall(message, 1500), this);
  connect(pending, &QDBusPendingCallWatcher::finished, this,
    [this, sequence, active](QDBusPendingCallWatcher* call) {
      const QDBusPendingReply<> reply = *call;
      call->deleteLater();
      if (sequence != m_sequence || m_stopping) { return; }
      setAvailable(!reply.isError());
      emit visibleChanged(!reply.isError() && active);
      if (reply.isError() && !m_warned) {
        m_warned = true;
        qCWarning(PROJECTEUR_DESKTOP_LOG).noquote()
          << "GNOME overlay unavailable. Enable projecteur@projecteur.github.io with "
             "gnome-extensions enable after logging out and back in."
          << reply.error().message();
      }
    });
}

void GnomeOverlay::setAvailable(bool available)
{
  if (available) { m_warned = false; }
  if (available == m_available) { return; }
  m_available = available;
  emit availableChanged(available);
}

void GnomeOverlay::stop()
{
  m_stopping = true;
  ++m_sequence;
  m_update.stop();
  m_heartbeat.stop();
  auto message = QDBusMessage::createMethodCall(QString::fromLatin1(service),
    QString::fromLatin1(path), QString::fromLatin1(interface), QStringLiteral("SetState"));
  message << false << settingsJson();
  // Fire-and-forget on shutdown. The shell also clears on owner loss/lease expiry.
  QDBusConnection::sessionBus().send(message);
}
