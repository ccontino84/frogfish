// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "controller.h"
#include "back.h"
#include "log.h"
#include "onehanded.h"
#include "router.h"
#include "scene.h"
#include "settings.h"
#include "zoom.h"
#include <frogfish_version.h>

#include <QFile>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStringList>

const char *const FrogfishController::OneHanded = "onehanded";
const char *const FrogfishController::Zoom = "zoom";
const char *const FrogfishController::Back = "back";

namespace {

const int FindIntervalMs = 500;
// The compositor window exists within seconds of startup; give up after 2 min
const int MaxFindAttempts = 240;
// A start counts as healthy once it has run this long on the window
const int HealthyAfterMs = 30000;
// Settings changed during a touch wait at most this long for its end
const int PendingSettingsMs = 5000;

struct Mode
{
    const char *name;
    // In the diagnostics
    const char *label;
    LipstickScene::Feature feature;
    bool (FrogfishSettings::*setting)() const;
};

const Mode Modes[] = {
    { FrogfishController::OneHanded, "one-handed", LipstickScene::OneHanded, &FrogfishSettings::oneHanded },
    { FrogfishController::Zoom, "zoom", LipstickScene::Zoom, &FrogfishSettings::zoom },
    { FrogfishController::Back, "back", LipstickScene::Back, &FrogfishSettings::back },
};

const Mode *findMode(const QString &name)
{
    for (const Mode &mode : Modes) {
        if (name == QLatin1String(mode.name))
            return &mode;
    }
    return nullptr;
}

// os-release's PRETTY_NAME, e.g. "Sailfish OS 5.2.0.17 (Tampella)"
QString systemName()
{
    QFile file(QStringLiteral("/etc/os-release"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (!line.startsWith(QLatin1String("PRETTY_NAME=")))
                continue;
            QString name = line.mid(12);
            if (name.size() >= 2 && name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"')))
                name = name.mid(1, name.size() - 2);
            if (!name.isEmpty())
                return name;
        }
    }
    return QStringLiteral("unknown");
}

}

FrogfishController::FrogfishController(const QByteArray &windowClass, const QString &cacheDirectory,
                                       QObject *parent)
    : QObject(parent)
    , m_windowClass(windowClass)
    , m_guard(cacheDirectory + QStringLiteral("/unhealthy-starts"))
    , m_useGuard(cacheDirectory)
    , m_active(false)
    , m_settings(new FrogfishSettings(this))
    , m_findAttempts(0)
{
    // Before anything else: a mode in use when the last run ended crashed it
    for (const QString &mode : m_useGuard.recover()) {
        FrogfishLog::trace("guard", QStringLiteral("%1 was in use when the home screen ended: crash %2")
                           .arg(mode).arg(m_useGuard.crashes(mode)));
    }

    // The top menu switch: nothing is on at start, also when inactive; its
    // taps go to one-handed mode, or are put right
    connect(m_settings, &FrogfishSettings::oneHandedActiveRequested,
            this, &FrogfishController::applyActiveRequest);
    QTimer::singleShot(0, this, [this]() { m_settings->publishOneHandedActive(false); });

    if (!m_guard.begin()) {
        qCWarning(lcFrogfish) << "Disabled after" << m_guard.unhealthyStarts()
                              << "unhealthy compositor starts; remove"
                              << m_guard.filePath() << "to enable again";
        return;
    }
    m_active = true;

    connect(m_settings, &FrogfishSettings::changed, this, &FrogfishController::applySettings);
    m_pendingTimer.setSingleShot(true);
    m_pendingTimer.setInterval(PendingSettingsMs);
    connect(&m_pendingTimer, &QTimer::timeout, this, [this]() {
        if (m_router && m_router->touching())
            qCWarning(lcFrogfish) << "A touch did not end; settings applied during it";
        m_settingsPending = false;
        if (m_router)
            m_router->forgetTouch();
        applySettings();
        applyActiveRequest();
    });

    m_healthyTimer.setSingleShot(true);
    m_healthyTimer.setInterval(HealthyAfterMs);
    connect(&m_healthyTimer, &QTimer::timeout, this, [this]() { m_guard.markHealthy(); });

    // Constructed while QGuiApplication itself is being set up; look for the
    // window once the event loop runs
    m_findTimer.setInterval(FindIntervalMs);
    connect(&m_findTimer, &QTimer::timeout, this, &FrogfishController::findWindow);
    m_findTimer.start();
    QTimer::singleShot(0, this, &FrogfishController::findWindow);
}

FrogfishController::~FrogfishController()
{
    detach();
}

QString FrogfishController::defaultCacheDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
            + QStringLiteral("/frogfish");
}

QQuickWindow *FrogfishController::window() const
{
    return m_window;
}

OneHandedMode *FrogfishController::oneHanded() const
{
    return m_oneHanded;
}

ZoomMode *FrogfishController::zoom() const
{
    return m_zoom;
}

BackMode *FrogfishController::back() const
{
    return m_back;
}

QString FrogfishController::status() const
{
    if (!m_active)
        return QStringLiteral("crash-guard");
    if (m_window)
        return QStringLiteral("active");
    if (m_findAttempts >= MaxFindAttempts)
        return QStringLiteral("no-window");
    return QStringLiteral("starting");
}

QString FrogfishController::modeStatus(const QString &name) const
{
    const Mode *mode = findMode(name);
    if (!mode || !(m_settings->*mode->setting)())
        return QStringLiteral("off");
    if (m_useGuard.tripped(name))
        return QStringLiteral("crash-guard");
    if (m_scene && !m_scene->supports(mode->feature))
        return QStringLiteral("unsupported");
    return QStringLiteral("on");
}

bool FrogfishController::allowed(const char *name) const
{
    const Mode *mode = findMode(QLatin1String(name));
    return mode && m_scene && m_scene->supports(mode->feature) && !m_useGuard.tripped(QLatin1String(name));
}

void FrogfishController::setInUse(const char *mode, bool inUse)
{
    if (inUse)
        m_useGuard.begin(QLatin1String(mode));
    else
        m_useGuard.end(QLatin1String(mode));
}

void FrogfishController::applySettings()
{
    if (!m_oneHanded && !m_zoom && !m_back)
        return;
    // Not in the middle of a gesture: its sizes and its side stay as they
    // were when it began
    if (m_router && m_router->touching()) {
        if (!m_settingsPending) {
            m_settingsPending = true;
            m_pendingTimer.start();
        }
        return;
    }
    m_settingsPending = false;
    m_pendingTimer.stop();
    const FrogfishSettings::Values values = m_settings->values();
    if (m_oneHanded)
        m_oneHanded->configure(values, allowed(OneHanded));
    if (m_zoom)
        m_zoom->configure(values, allowed(Zoom));
    if (m_back)
        m_back->configure(values, allowed(Back));
}

void FrogfishController::applyActiveRequest()
{
    if (!m_oneHanded) {
        m_settings->publishOneHandedActive(false);
        return;
    }
    if (m_router && m_router->touching()) {
        if (!m_activePending) {
            m_activePending = true;
            m_pendingTimer.start();
        }
        return;
    }
    m_activePending = false;
    m_oneHanded->setActive(m_settings->oneHandedActive());
    // Also when refused: the switch shows the state again
    m_settings->publishOneHandedActive(m_oneHanded->active());
}

void FrogfishController::detach()
{
    // Zoom first: it magnifies what one-handed mode shows
    delete m_zoom;
    delete m_back;
    delete m_oneHanded;
    delete m_router;
    delete m_scene;
}

void FrogfishController::resetCrashGuard()
{
    m_guard.markHealthy();
    for (const Mode &mode : Modes)
        m_useGuard.reset(QLatin1String(mode.name));
    FrogfishLog::trace("guard", QStringLiteral("reset"));
    applySettings();
}

void FrogfishController::reload()
{
    m_settings->reload();
}

QString FrogfishController::diagnostics() const
{
    QString guards = QStringLiteral("guards: unhealthy starts %1").arg(m_guard.unhealthyStarts());
    for (const Mode &mode : Modes)
        guards += QStringLiteral(", %1 crashes %2").arg(QLatin1String(mode.label))
                  .arg(m_useGuard.crashes(QLatin1String(mode.name)));
    QStringList lines;
    lines << QStringLiteral("version " FROGFISH_VERSION)
          << QStringLiteral("system ") + systemName()
          << QStringLiteral("status ") + status()
          << m_settings->summary()
          << guards;
    if (m_scene)
        lines << m_scene->summary();
    if (m_oneHanded)
        lines << m_oneHanded->diagnostics();
    if (m_zoom)
        lines << m_zoom->diagnostics();
    if (m_back)
        lines << m_back->diagnostics();
    return lines.join(QLatin1Char('\n'));
}

void FrogfishController::findWindow()
{
    if (m_window)
        return;

    for (QWindow *window : QGuiApplication::topLevelWindows()) {
        QQuickWindow *quickWindow = qobject_cast<QQuickWindow *>(window);
        if (quickWindow && quickWindow->inherits(m_windowClass.constData())) {
            m_findTimer.stop();
            attach(quickWindow);
            return;
        }
    }

    if (++m_findAttempts >= MaxFindAttempts) {
        m_findTimer.stop();
        qCWarning(lcFrogfish) << "No" << m_windowClass << "window found; staying inactive";
        // Nothing was changed in this process, so this start was not risky
        m_guard.markHealthy();
    }
}

void FrogfishController::attach(QQuickWindow *window)
{
    m_window = window;
    FrogfishLog::trace("start", QStringLiteral("version " FROGFISH_VERSION ", attached to %1")
                       .arg(QLatin1String(window->metaObject()->className())));

    m_scene = new LipstickScene(window, this);
    m_router = new InputRouter(window, this);
    m_oneHanded = new OneHandedMode(m_scene, m_router, this);
    m_zoom = new ZoomMode(m_scene, m_router, m_oneHanded, this);
    m_back = new BackMode(m_scene, m_router, this);
    m_router->setModes(m_zoom, m_back, m_oneHanded);
    connect(m_oneHanded.data(), &OneHandedMode::inUseChanged, this, [this](bool inUse) {
        setInUse(OneHanded, inUse);
    });
    connect(m_zoom.data(), &ZoomMode::inUseChanged, this, [this](bool inUse) { setInUse(Zoom, inUse); });
    connect(m_back.data(), &BackMode::inUseChanged, this, [this](bool inUse) { setInUse(Back, inUse); });
    connect(m_oneHanded.data(), &OneHandedMode::activeChanged,
            m_settings, &FrogfishSettings::publishOneHandedActive);
    connect(m_router.data(), &InputRouter::touchEnded, this, [this]() {
        if (m_settingsPending)
            applySettings();
        if (m_activePending)
            applyActiveRequest();
    });
    // While lipstick's scene is still there
    connect(qApp, &QCoreApplication::aboutToQuit, this, &FrogfishController::detach);
    for (const Mode &mode : Modes) {
        if (!m_scene->supports(mode.feature))
            qCWarning(lcFrogfish, "%s not supported: lipstick lacks %s", mode.name,
                      qPrintable(m_scene->missing().join(QStringLiteral(", "))));
    }
    applySettings();

    m_healthyTimer.start();
    emit attached(window);
}
