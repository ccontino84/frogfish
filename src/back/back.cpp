// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "back.h"
#include "haptics.h"
#include "log.h"
#include "router.h"
#include "scene.h"
#include "units.h"

#include <QCoreApplication>
#include <QLineF>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QTouchEvent>

namespace {

// Clear of the corners' gestures (lipstick's close swipe, the zoom strip)
// and of the keyboard's top row
const qreal EndMarginMm = 10;
const qreal KeyboardMarginMm = 3;
// Lipstick's response to the action (a layer's animation) counts as in use
const int InUseAfterActionMs = 500;

}

BackMode::BackMode(LipstickScene *scene, InputRouter *router, QObject *parent)
    : QObject(parent)
    , m_scene(scene)
    , m_router(router)
{
    m_holdTimer.setSingleShot(true);
    connect(&m_holdTimer, &QTimer::timeout, this, &BackMode::holdTimeout);
    m_inUseTimer.setSingleShot(true);
    m_inUseTimer.setInterval(InUseAfterActionMs);
    connect(&m_inUseTimer, &QTimer::timeout, this, [this]() { setInUse(false); });
}

BackMode::~BackMode()
{
    endHold();
    m_inUseTimer.stop();
    setInUse(false);
}

QQuickWindow *BackMode::window() const
{
    return m_scene ? m_scene->window() : nullptr;
}

qreal BackMode::mm() const
{
    return Units::pixelsPerMm(window());
}

void BackMode::trace(const QString &message)
{
    m_last = message;
    FrogfishLog::trace("back", message);
}

void BackMode::configure(const FrogfishSettings::Values &values, bool allowed)
{
    const bool on = values.back && allowed;
    if (on != m_on)
        trace(on ? QStringLiteral("on") : QStringLiteral("off"));
    m_on = on;
    m_leftHanded = values.leftHanded;
    if (!m_on && m_hold == HoldWaiting && m_router)
        m_held.replay(m_router);
    if (!m_on)
        endHold();
}

BackMode::Target BackMode::target() const
{
    QQuickWindow *window = this->window();
    if (!m_on || !window || !m_scene->portrait() || m_scene->deviceLocked()
            || m_scene->exposed("lockScreenLayer") || m_scene->frontDialogLayer())
        return None;
    if (m_scene->exposed("topMenuLayer"))
        return TopMenu;
    if (m_scene->exposed("launcherLayer"))
        return Launcher;
    QObject *application = m_scene->topmostApplication();
    // Lipstick's back is off over its own notification banner and dialogs
    if (!application || !window->property("synthesizeBackEvent").toBool())
        return None;
    return Application;
}

bool BackMode::onEdge(const QPointF &at) const
{
    return onSideStrip(at) && clearOfEnds(at);
}

bool BackMode::onSideStrip(const QPointF &at) const
{
    const qreal strip = m_scene->sideBand();
    return m_leftHanded ? at.x() >= window()->width() - strip : at.x() <= strip;
}

bool BackMode::clearOfEnds(const QPointF &at) const
{
    const qreal height = window()->height();
    qreal bottom = height - EndMarginMm * mm();
    if (m_scene->keyboardOpen())
        bottom = qMin(bottom, height - m_scene->keyboardHeight() - KeyboardMarginMm * mm());
    return at.y() >= EndMarginMm * mm() && at.y() <= bottom;
}

bool BackMode::touch(QTouchEvent *event)
{
    if (!m_on || !m_scene || !m_router || !window())
        return false;
    const QEvent::Type type = event->type();
    const QList<QTouchEvent::TouchPoint> &points = event->touchPoints();
    const bool ends = type == QEvent::TouchEnd || type == QEvent::TouchCancel;

    switch (m_hold) {
    case HoldIdle:
        if (type != QEvent::TouchBegin || points.size() != 1 || !onEdge(points.first().pos())
                || target() == None)
            return false;
        m_held.clear();
        m_held.hold(event);
        m_holdStart = points.first().pos();
        m_pointId = points.first().id();
        m_hold = HoldWaiting;
        m_holdTimer.start(HoldGesture::ArmMs);
        return true;
    case HoldWaiting: {
        m_held.hold(event);
        const bool moved = points.size() != 1 || points.first().id() != m_pointId
                || QLineF(m_holdStart, points.first().pos()).length() > HoldGesture::SlopMm * mm();
        // A tap, a swipe or a second finger: lipstick's, whole
        if (ends || moved || type == QEvent::TouchBegin) {
            m_holdTimer.stop();
            m_hold = HoldIdle;
            m_held.replay(m_router);
        }
        return true;
    }
    case HoldArmed:
        if (type == QEvent::TouchEnd) {
            endHold();
            // After the release is through
            QTimer::singleShot(0, this, &BackMode::act);
        } else if (type == QEvent::TouchCancel) {
            endHold();
            setInUse(false);
        } else if (points.size() != 1 || QLineF(m_holdStart, points.first().pos()).length() > HoldGesture::SlopMm * mm()) {
            trace(QStringLiteral("moved after the hold, dropped"));
            m_holdTimer.stop();
            m_hold = HoldDropping;
            setInUse(false);
        }
        return true;
    case HoldDropping:
        if (ends)
            endHold();
        return true;
    }
    return false;
}

void BackMode::holdTimeout()
{
    if (m_hold == HoldWaiting) {
        // Ours now: what was held is never given back
        m_held.clear();
        m_hold = HoldArmed;
        setInUse(true);
        if (QQuickWindow *window = this->window())
            m_cue.show(window->contentItem(), m_holdStart, mm(), m_pointId);
        Haptics::press();
        m_holdTimer.start(HoldGesture::MaximumMs - HoldGesture::ArmMs);
    } else if (m_hold == HoldArmed) {
        trace(QStringLiteral("held too long, dropped"));
        m_hold = HoldDropping;
        setInUse(false);
    }
}

void BackMode::endHold()
{
    m_holdTimer.stop();
    m_held.clear();
    m_hold = HoldIdle;
}

void BackMode::act()
{
    // What is in front now: the hold took its time
    switch (target()) {
    case TopMenu:
        QMetaObject::invokeMethod(m_scene->object("topMenuLayer"), "hide");
        trace(QStringLiteral("top menu closed"));
        break;
    case Launcher:
        QMetaObject::invokeMethod(m_scene->object("launcherLayer"), "hide");
        trace(QStringLiteral("app grid closed"));
        break;
    case Application:
        sendBack();
        trace(QStringLiteral("back"));
        break;
    case None:
        trace(QStringLiteral("nothing in front any more"));
        break;
    }
    m_inUseTimer.start();
}

void BackMode::sendBack()
{
    // LipstickCompositor::event() turns a right click into Key_Back for the
    // window in front, as for a mouse; the position doesn't matter
    QQuickWindow *window = this->window();
    const QPointF at(window->width() / 2.0, window->height() / 2.0);
    QMouseEvent press(QEvent::MouseButtonPress, at, at, window->mapToGlobal(at.toPoint()),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, at, window->mapToGlobal(at.toPoint()),
                        Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &release);
}

void BackMode::setInUse(bool inUse)
{
    if (inUse)
        m_inUseTimer.stop();
    if (inUse == m_inUse)
        return;
    m_inUse = inUse;
    emit inUseChanged(inUse);
}

QString BackMode::diagnostics() const
{
    if (!m_on)
        return QStringLiteral("back: off");
    return QStringLiteral("back: on, %1 edge, last %2")
            .arg(m_leftHanded ? QStringLiteral("right") : QStringLiteral("left"),
                 m_last.isEmpty() ? QStringLiteral("nothing") : m_last);
}
