// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_STATUSSERVICE_H
#define FROGFISH_STATUSSERVICE_H

#include <QDBusConnection>
#include <QObject>

class FrogfishController;

// Tells the settings page on the session bus whether the plugin runs in the
// compositor. No reply means it is not loaded. From a terminal:
//   dbus-send --session --print-reply --dest=io.github.ccontino84.frogfish
//             /frogfish io.github.ccontino84.frogfish.status
class FrogfishStatusService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.ccontino84.frogfish")

public:
    static const char *const ServiceName;
    static const char *const ObjectPath;

    explicit FrogfishStatusService(FrogfishController *controller);

    bool registerOn(QDBusConnection connection);

public slots:
    // FrogfishController::status()
    QString status() const;
    // FrogfishController::modeStatus(): "onehanded", "zoom" or "back"
    QString modeStatus(const QString &mode) const;
    // Of the plugin running now, which is older than the installed package
    // until the next restart after an upgrade
    QString version() const;
    void resetCrashGuard();
    // FrogfishController::reload(): after "Reset to defaults"
    void reload();
    // FrogfishController::diagnostics(), for support
    QString diagnostics() const;

private:
    FrogfishController *m_controller;
};

#endif
