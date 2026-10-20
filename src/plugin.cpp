// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "controller.h"
#include "log.h"
#include "statusservice.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGenericPlugin>
#include <QTimer>

// Loaded by the lipstickaddons loader (key lipstickaddon-frogfish, see
// lipstick-addons.md), or directly as QT_QPA_GENERIC_PLUGINS=frogfish.
//
// An optional specification tunes where it attaches, for testing:
//   frogfish:class=<QQuickWindow subclass>:process=<executable name or *>
// Defaults: class=LipstickCompositor, process=lipstick
class FrogfishPlugin : public QGenericPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QGenericPluginFactoryInterface_iid FILE "frogfish.json")

public:
    QObject *create(const QString &key, const QString &specification) override
    {
        if (key.compare(QLatin1String("frogfish"), Qt::CaseInsensitive) != 0
                && key.compare(QLatin1String("lipstickaddon-frogfish"), Qt::CaseInsensitive) != 0)
            return nullptr;

        QByteArray windowClass("LipstickCompositor");
        QString process(QStringLiteral("lipstick"));
        for (const QString &option : specification.split(QLatin1Char(':'))) {
            if (option.isEmpty())
                continue;
            const int separator = option.indexOf(QLatin1Char('='));
            const QString name = option.left(separator);
            const QString value = separator >= 0 ? option.mid(separator + 1) : QString();
            if (name == QLatin1String("class"))
                windowClass = value.toLatin1();
            else if (name == QLatin1String("process"))
                process = value;
            else
                qCWarning(lcFrogfish) << "Unknown option" << option;
        }

        // Processes started by the compositor may inherit its environment;
        // stay out of everything but the compositor itself. An empty object
        // rather than none: none makes Qt log "No such plugin".
        const QString executable = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
        if (process != QLatin1String("*") && executable != process) {
            qCDebug(lcFrogfish) << "Not activating in" << executable;
            return new QObject;
        }
        // Once per process: the same plugin may be named by the environment
        // and found by the loader
        if (m_created) {
            qCDebug(lcFrogfish) << "Already running; ignoring" << key;
            return new QObject;
        }
        m_created = true;

        FrogfishController *controller = new FrogfishController(windowClass);
        // Also when the crash guard has tripped, so that the settings page
        // can say so. Created during QGuiApplication's setup; register once
        // the event loop runs.
        FrogfishStatusService *service = new FrogfishStatusService(controller);
        QTimer::singleShot(0, service, [service]() {
            service->registerOn(QDBusConnection::sessionBus());
        });
        return controller;
    }

private:
    // One plugin instance per process: Qt keeps it as long as the library
    bool m_created = false;
};

#include "plugin.moc"
