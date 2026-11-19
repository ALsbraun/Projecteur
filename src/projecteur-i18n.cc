// SPDX-License-Identifier: MIT
#include "projecteur-i18n.h"

#ifndef PROJECTEUR_WITH_KDE
#ifdef PROJECTEUR_NATIVE_GNOME
#include "native/files.h"
#else
#include <QFile>
#include <QFileInfo>
#endif
#include <clocale>
#endif

void initializeTranslations()
{
#ifdef PROJECTEUR_WITH_KDE
  KLocalizedString::setApplicationDomain("projecteur");
#else
  std::setlocale(LC_MESSAGES, "");
#ifdef PROJECTEUR_NATIVE_GNOME
  const auto localDirectory = native::executableDirectory() + QStringLiteral("/locale");
#else
  const auto localDirectory = QFileInfo(QFile::symLinkTarget(QStringLiteral("/proc/self/exe")))
    .absolutePath() + QStringLiteral("/locale");
#endif
  const auto localCatalog = localDirectory + QStringLiteral("/de/LC_MESSAGES/projecteur.mo");
  const auto localePath = QFile::exists(localCatalog)
    ? QFile::encodeName(localDirectory) : QByteArray(PROJECTEUR_LOCALEDIR);
  bindtextdomain("projecteur", localePath.constData());
  bind_textdomain_codeset("projecteur", "UTF-8");
#endif
}
