// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "hold.h"
#include "router.h"

void TouchHold::hold(const QTouchEvent *event)
{
    Held held;
    held.type = event->type();
    held.device = event->device();
    held.modifiers = event->modifiers();
    held.states = event->touchPointStates();
    held.points = event->touchPoints();
    held.timestamp = event->timestamp();
    m_events << held;
}

void TouchHold::replay(InputRouter *router)
{
    // Taken first: what the window does with them may start a new hold
    const QList<Held> events = m_events;
    m_events.clear();
    for (const Held &held : events) {
        QTouchEvent event(held.type, held.device, held.modifiers, held.states, held.points);
        event.setTimestamp(held.timestamp);
        router->send(&event);
    }
}
