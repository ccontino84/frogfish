// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_HOLDCUE_H
#define FROGFISH_HOLDCUE_H

#include <QPointer>

class QPointF;
class QQuickItem;
class QTouchEvent;

// The ring around the finger once a hold is recognised (Cue.qml): it grows
// from the finger's size to RingMm and fades. While it plays it stays on its
// finger, whatever the gesture does with the touch: the router passes every
// touch to follow().
class HoldCue
{
public:
    static const qreal FingerMm;
    static const qreal RingMm;
    static const int RingMs;
    // Above lipstick's layers, below the colour filters (z 1e9)
    static const qreal AboveLipstickZ;

    HoldCue();
    ~HoldCue();

    // Centred on the point, in the parent's coordinates
    void show(QQuickItem *parent, const QPointF &at, qreal pixelsPerMm, int pointId,
              qreal z = AboveLipstickZ);

    // Every ring playing moves to its finger in the event
    static void follow(const QTouchEvent *event);

private:
    Q_DISABLE_COPY(HoldCue)

    QPointer<QQuickItem> m_item;
    int m_pointId = -1;
};

#endif
