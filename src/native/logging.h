// SPDX-License-Identifier: MIT
#pragma once

#include "platform.h"

#define PROJECTEUR_MAIN_LOG "projecteur"
#define PROJECTEUR_COMMAND_LOG "projecteur.command"
#define PROJECTEUR_DESKTOP_LOG "projecteur.desktop"
#define PROJECTEUR_DEVICE_LOG "projecteur.device"
#define PROJECTEUR_HID_LOG "projecteur.hidpp"
#define PROJECTEUR_INPUT_LOG "projecteur.input"
#define PROJECTEUR_SETTINGS_LOG "projecteur.settings"
#define PROJECTEUR_VIRTUAL_DEVICE_LOG "projecteur.uinput"
#define qCDebug(category) native::Log(G_LOG_LEVEL_DEBUG, category)
#define qCInfo(category) native::Log(G_LOG_LEVEL_INFO, category)
#define qCWarning(category) native::Log(G_LOG_LEVEL_WARNING, category)
#define qCCritical(category) native::Log(G_LOG_LEVEL_CRITICAL, category)
