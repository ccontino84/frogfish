// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "router.h"
#include "back.h"
#include "holdcue.h"
#include "onehanded.h"
#include "zoom.h"

#include <QCoreApplication>
#include <QQuickWindow>
#include <QTimer>
#include <QTouchEvent>

InputRouter::InputRouter(QQuickWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    qApp->installEventFilter(this);
}

InputRouter::~InputRouter()
{
    qApp->removeEventFilter(this);
}

void InputRouter::setModes(ZoomMode *zoom, BackMode *back, OneHandedMode *oneHanded)
{
    m_zoom = zoom;
    m_back = back;
    m_oneHanded = oneHanded;
}

void InputRouter::send(QTouchEvent *event)
{
    if (!m_window)
        return;
    event->setWindow(m_window);
    ++m_sending;
    QCoreApplication::sendEvent(m_window, event);
    --m_sending;
}

bool InputRouter::eventFilter(QObject *object, QEvent *event)
{
    if (object != m_window)
        return false;
    const QEvent::Type type = event->type();
    if (type != QEvent::TouchBegin && type != QEvent::TouchUpdate && type != QEvent::TouchEnd
            && type != QEvent::TouchCancel)
        return false;
    // Given back or made up here: straight through
    if (m_sending)
        return false;

    QTouchEvent *touch = static_cast<QTouchEvent *>(event);
    if (type == QEvent::TouchBegin)
        m_touching = true;
    HoldCue::follow(touch);
    bool taken = m_zoom && m_zoom->touch(touch);
    if (!taken && m_back)
        taken = m_back->touch(touch);
    if (!taken && !(m_zoom && m_zoom->zoomed()) && m_oneHanded)
        taken = m_oneHanded->touch(touch);
    if (type == QEvent::TouchEnd || type == QEvent::TouchCancel) {
        m_touching = false;
        QTimer::singleShot(0, this, &InputRouter::touchEnded);
    }
    return taken;
}
