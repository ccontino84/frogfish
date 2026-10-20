// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "statusservice.h"
#include "controller.h"
#include "log.h"
#include <frogfish_version.h>

#include <QDBusError>

const char *const FrogfishStatusService::ServiceName = "io.github.ccontino84.frogfish";
const char *const FrogfishStatusService::ObjectPath = "/frogfish";

FrogfishStatusService::FrogfishStatusService(FrogfishController *controller)
    : QObject(controller)
    , m_controller(controller)
{
}

bool FrogfishStatusService::registerOn(QDBusConnection connection)
{
    if (!connection.registerObject(QLatin1String(ObjectPath), this, QDBusConnection::ExportAllSlots)) {
        qCWarning(lcFrogfish) << "Cannot register" << ObjectPath << "on D-Bus";
        return false;
    }
    if (!connection.registerService(QLatin1String(ServiceName))) {
        qCWarning(lcFrogfish) << "Cannot register" << ServiceName << "on D-Bus:"
                              << connection.lastError().message();
        connection.unregisterObject(QLatin1String(ObjectPath));
        return false;
    }
    return true;
}

QString FrogfishStatusService::status() const
{
    return m_controller->status();
}

QString FrogfishStatusService::modeStatus(const QString &mode) const
{
    return m_controller->modeStatus(mode);
}

QString FrogfishStatusService::version() const
{
    return QStringLiteral(FROGFISH_VERSION);
}

void FrogfishStatusService::resetCrashGuard()
{
    m_controller->resetCrashGuard();
}

void FrogfishStatusService::reload()
{
    m_controller->reload();
}

QString FrogfishStatusService::diagnostics() const
{
    return m_controller->diagnostics();
}
