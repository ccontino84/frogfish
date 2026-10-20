// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_HOLD_H
#define FROGFISH_HOLD_H

#include <QList>
#include <QTouchEvent>

class InputRouter;

// What a hold is, the same for every gesture: held still for ArmMs it is
// recognised, moved further than SlopMm it is a swipe. Where a hold has to
// be lifted in time, a longer one is a grip.
namespace HoldGesture {
const int ArmMs = 500;
const int MaximumMs = 1500;
const qreal SlopMm = 3;
// Where the holds are: the one-handed corner (also kept clear by the zoom
// strip) and the strips along the edges
const qreal CornerMm = 8;
const qreal StripMm = 4;
}

// The events of a touch held back while it may still become a gesture;
// replayed in their order if it doesn't
class TouchHold
{
public:
    void hold(const QTouchEvent *event);
    void clear() { m_events.clear(); }
    bool isEmpty() const { return m_events.isEmpty(); }
    // Past the modes; the hold is empty afterwards
    void replay(InputRouter *router);

private:
    struct Held
    {
        QEvent::Type type;
        QTouchDevice *device;
        Qt::KeyboardModifiers modifiers;
        Qt::TouchPointStates states;
        QList<QTouchEvent::TouchPoint> points;
        ulong timestamp;
    };
    QList<Held> m_events;
};

#endif
