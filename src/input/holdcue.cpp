// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "holdcue.h"
#include "qml.h"

#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTouchEvent>

const qreal HoldCue::FingerMm = 12;
const qreal HoldCue::RingMm = 30;
const int HoldCue::RingMs = 450;
const qreal HoldCue::AboveLipstickZ = 1e9 - 1;

namespace {

QList<HoldCue *> &cues()
{
    static QList<HoldCue *> list;
    return list;
}

}

HoldCue::HoldCue()
{
    cues() << this;
}

HoldCue::~HoldCue()
{
    cues().removeOne(this);
    delete m_item;
}

void HoldCue::follow(const QTouchEvent *event)
{
    for (HoldCue *cue : cues()) {
        QQuickItem *item = cue->m_item;
        if (!item || !item->isVisible() || !item->parentItem())
            continue;
        for (const QTouchEvent::TouchPoint &point : event->touchPoints()) {
            if (point.id() == cue->m_pointId)
                item->setPosition(item->parentItem()->mapFromScene(point.pos())
                                  - QPointF(item->width() / 2, item->height() / 2));
        }
    }
}

void HoldCue::show(QQuickItem *parent, const QPointF &at, qreal pixelsPerMm, int pointId, qreal z)
{
    if (!parent)
        return;
    m_pointId = pointId;
    if (m_item && m_item->parentItem() != parent)
        delete m_item;
    if (!m_item) {
        m_item = Qml::createItem(qmlEngine(parent) ? qmlEngine(parent) : qmlEngine(parent->window()),
                                 "Cue.qml");
        if (!m_item)
            return;
        m_item->setParentItem(parent);
        m_item->setZ(z);
    }
    const qreal size = RingMm * pixelsPerMm;
    m_item->setProperty("fingerSize", FingerMm * pixelsPerMm);
    m_item->setProperty("duration", RingMs);
    m_item->setSize(QSizeF(size, size));
    m_item->setPosition(at - QPointF(size / 2, size / 2));
    QMetaObject::invokeMethod(m_item, "start");
}
