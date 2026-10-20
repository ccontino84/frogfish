// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_BACK_H
#define FROGFISH_BACK_H

#include "settings.h"
#include "hold.h"
#include "holdcue.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

class InputRouter;
class LipstickScene;
class QQuickWindow;
class QTouchEvent;

// Back: hold lipstick's side strip away from the preferred hand and lift.
// The application in front goes back through lipstick's own back (a right
// click becomes Key_Back), also with the keyboard open: only the app can
// close the keyboard so that its field opens it again (Maliit). The app grid
// and the top menu are closed. A swipe or a tap on the strip is always given
// back, so lipstick's side swipes work as set.
class BackMode : public QObject
{
    Q_OBJECT

public:
    BackMode(LipstickScene *scene, InputRouter *router, QObject *parent = nullptr);
    ~BackMode() override;

    // Off unless allowed (supported, not stopped by the in-use guard)
    void configure(const FrogfishSettings::Values &values, bool allowed);
    // From the router: true if the touch is taken
    bool touch(QTouchEvent *event);
    // From the hold until lipstick's response to the action is through
    bool inUse() const { return m_inUse; }
    // A line for the diagnostics
    QString diagnostics() const;

signals:
    void inUseChanged(bool inUse);

private:
    enum Hold { HoldIdle, HoldWaiting, HoldArmed, HoldDropping };
    // What the hold would act on, decided at touch-down
    enum Target { None, Application, Launcher, TopMenu };

    QQuickWindow *window() const;
    qreal mm() const;
    void trace(const QString &message);
    Target target() const;
    bool onEdge(const QPointF &at) const;
    bool onSideStrip(const QPointF &at) const;
    bool clearOfEnds(const QPointF &at) const;
    void holdTimeout();
    void endHold();
    void act();
    void sendBack();
    void setInUse(bool inUse);

    QPointer<LipstickScene> m_scene;
    QPointer<InputRouter> m_router;
    bool m_on = false;
    bool m_leftHanded = false;
    Hold m_hold = HoldIdle;
    TouchHold m_held;
    HoldCue m_cue;
    QTimer m_holdTimer;
    QPointF m_holdStart;
    int m_pointId = -1;
    QString m_last;
    bool m_inUse = false;
    QTimer m_inUseTimer;
};

#endif
