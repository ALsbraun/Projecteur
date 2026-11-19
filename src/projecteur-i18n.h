// SPDX-License-Identifier: MIT
#pragma once

void initializeTranslations();

#ifdef PROJECTEUR_WITH_KDE
#include <KLocalizedString>
#else
#ifdef PROJECTEUR_NATIVE_GNOME
#include "native/platform.h"
#else
#include <QString>
#endif
#include <libintl.h>
#include <utility>

#ifdef PROJECTEUR_NATIVE_GNOME
template<typename... Args>
QString translatedArguments(QString text, Args&&... args)
{
  ((text = text.arg(std::forward<Args>(args))), ...);
  return text;
}
#endif

template<typename... Args>
QString i18n(const char* text, Args&&... args)
{
  const auto translated = QString::fromUtf8(dgettext("projecteur", text));
#ifdef PROJECTEUR_NATIVE_GNOME
  return translatedArguments(translated, std::forward<Args>(args)...);
#else
  if constexpr (sizeof...(args) == 0) {
    return translated;
  } else {
    return translated.arg(std::forward<Args>(args)...);
  }
#endif
}

template<typename... Args>
QString i18np(const char* singular, const char* plural, unsigned long count, Args&&... args)
{
  const auto translated = QString::fromUtf8(dngettext("projecteur", singular, plural, count));
#ifdef PROJECTEUR_NATIVE_GNOME
  return translatedArguments(translated, count, std::forward<Args>(args)...);
#else
  return translated.arg(count, std::forward<Args>(args)...);
#endif
}
#endif
