// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_CONTROLLER_H
#define FROGFISH_CONTROLLER_H

#include "crashguard.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

class FrogfishSettings;
class BackMode;
class InputRouter;
class LipstickScene;
class OneHandedMode;
class QQuickWindow;
class ZoomMode;

// Runs inside the compositor process: waits for the compositor's
// QQuickWindow and owns what works on it: the scene, the input router and
// the modes. Both crash guards live here: the startup guard keeps the
// whole plugin inactive, the in-use guard one mode. A mode also stays off
// when lipstick lacks what it needs (an OS update).
class FrogfishController : public QObject
{
    Q_OBJECT

public:
    // The modes' names, for the in-use guard, D-Bus and the page
    static const char *const OneHanded;
    static const char *const Zoom;
    static const char *const Back;

    // cacheDirectory holds the crash guards' files
    explicit FrogfishController(const QByteArray &windowClass,
                                const QString &cacheDirectory = defaultCacheDirectory(),
                                QObject *parent = nullptr);
    ~FrogfishController() override;

    static QString defaultCacheDirectory();

    bool isActive() const { return m_active; }
    // For the settings page: "active", "starting" (still looking for the
    // window), "no-window" or "crash-guard"
    QString status() const;
    // A mode for the settings page: "on", "off" (its setting),
    // "crash-guard" (turned off after repeated crashes while in use) or
    // "unsupported" (lipstick lacks what it needs)
    QString modeStatus(const QString &name) const;
    // Lets the plugin and every mode try again
    void resetCrashGuard();
    // After "Reset to defaults"
    void reload();
    // For support, several lines: version, system, status, settings, the
    // guards, the scene's hooks, each mode's state
    QString diagnostics() const;

    QQuickWindow *window() const;
    FrogfishSettings *settings() const { return m_settings; }
    UseGuard &useGuard() { return m_useGuard; }
    // Null until attached
    OneHandedMode *oneHanded() const;
    ZoomMode *zoom() const;
    BackMode *back() const;

signals:
    void attached(QQuickWindow *window);

private slots:
    void findWindow();
    // The settings to the modes; after the touch that is down, if any
    void applySettings();
    // The top menu switch asked for one-handed mode on or off; also after
    // the touch
    void applyActiveRequest();
    // Everything put back and the modes gone: before lipstick quits
    void detach();

private:
    void attach(QQuickWindow *window);
    // The mode may run: supported, not stopped by the in-use guard
    bool allowed(const char *name) const;
    void setInUse(const char *mode, bool inUse);

    QByteArray m_windowClass;
    CrashGuard m_guard;
    UseGuard m_useGuard;
    bool m_active;
    FrogfishSettings *m_settings;
    QTimer m_findTimer;
    int m_findAttempts;
    QTimer m_healthyTimer;
    QPointer<QQuickWindow> m_window;
    QPointer<LipstickScene> m_scene;
    QPointer<InputRouter> m_router;
    QPointer<OneHandedMode> m_oneHanded;
    QPointer<ZoomMode> m_zoom;
    QPointer<BackMode> m_back;
    // Settings changed during a touch, applied once it ends (or after a
    // while, should the end never come)
    bool m_settingsPending = false;
    bool m_activePending = false;
    QTimer m_pendingTimer;
};

#endif
