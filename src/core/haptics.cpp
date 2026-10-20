// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "haptics.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QVariantMap>

void Haptics::press()
{
    // ngfd, as libngf-qt calls it: on the system bus, interface without
    // ".Backend" (ngfd's introspection says a(sv), but it takes a{sv}); a
    // touch haptic without sound (ngfd-settings-sailfish's feedback.ini)
    QDBusMessage message = QDBusMessage::createMethodCall(
                QStringLiteral("com.nokia.NonGraphicFeedback1.Backend"),
                QStringLiteral("/com/nokia/NonGraphicFeedback1"),
                QStringLiteral("com.nokia.NonGraphicFeedback1"),
                QStringLiteral("Play"));
    message << QStringLiteral("feedback_press_strong") << QVariantMap();
    QDBusConnection::systemBus().asyncCall(message);
}
