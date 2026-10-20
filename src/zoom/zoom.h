// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_ZOOM_H
#define FROGFISH_ZOOM_H

#include "settings.h"
#include "hold.h"
#include "holdcue.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QTimer>
#include <QTouchEvent>

class InputRouter;
class LipstickScene;
class OneHandedMode;
class QQuickItem;
class QQuickWindow;

// Zoom: a hold on the bottom strip starts it, a drag up then zooms in. While
// zoomed, a pinch zooms and pans; in pan mode one finger pans, a drag up from
// the strip zooms in and the strip's hold zooms out; in touch mode (the
// button) touches reach the apps. It ends with the keyboard, the lock
// screen, a dialog, rotation or the display off. Portrait only.
//
// Lipstick's top-level items are scaled (transforms appended to theirs), so
// whatever is shown is magnified, other add-ons' items too; only its own
// overlay isn't. LayersParent is drawn into a layer first: Silica's
// backgrounds pick their image by screen position. With one-handed mode the
// zoom stays inside its picture, within reach.
class ZoomMode : public QObject
{
    Q_OBJECT

public:
    // oneHanded gives the picture to stay in; may be null
    ZoomMode(LipstickScene *scene, InputRouter *router, OneHandedMode *oneHanded, QObject *parent = nullptr);
    ~ZoomMode() override;

    // The settings, and whether it may run at all (supported, not stopped
    // by the in-use guard)
    void configure(const FrogfishSettings::Values &values, bool allowed);
    // From the router: true if the touch is taken
    bool touch(QTouchEvent *event);

    // 1 when not zoomed
    qreal level() const { return m_level; }
    bool zoomed() const { return m_zoomed; }
    // Touch mode (the button): touches reach the apps
    bool touchMode() const { return m_edit; }
    // The level change for a drag of that many pixels up
    qreal levelForDrag(qreal up) const;
    qreal mm() const;
    // The floating button, in screen coordinates
    QRectF buttonRect() const;
    // Where the zoom shows: one-handed mode's picture, or the screen
    QRectF picture() const;
    // A line for the diagnostics
    QString diagnostics() const;

signals:
    void inUseChanged(bool inUse);

private slots:
    void holdTimeout();
    // Ends the zoom when something else needs the screen
    void checkEnd();
    void displayStatus(const QString &status);
    void hideLevel();
    void zoomOutStep();

private:
    QQuickWindow *window() const;
    bool canStart() const;
    // While zoomed, pan mode: one finger's drag pans
    bool dragPans() const { return !m_edit; }
    bool inStrip(const QPointF &point) const;
    bool touchBegin(QTouchEvent *event);
    void startPinch(const QList<QTouchEvent::TouchPoint> &points);
    void pinch(const QList<QTouchEvent::TouchPoint> &points);
    // One-handed mode's picture's middle, or the finger's x on the screen's
    QPointF zoomCentre() const;
    // Ends a level change, and the zoom near 1x
    void settle(const QString &what);
    void startZoomOut();
    void stopZoomOut(const QPointF &at);
    void arm();
    // The level around a point of the screen that stays where it is
    void setLevel(qreal level, const QPointF &fixed);
    // Bounded, with a vibration at the limits; shows the level
    void applyLevel(qreal level, const QPointF &fixed);
    void showLevel();
    void pan(const QPointF &delta);
    void place();
    void start();
    void end(const QString &why);
    void scaleScene();
    void unscaleScene();
    void showCue(const QPointF &at);
    void updateOverlay();
    void trace(const QString &message);

    LipstickScene *m_scene;
    InputRouter *m_router;
    QPointer<OneHandedMode> m_oneHanded;
    FrogfishSettings::Values m_config;
    bool m_configured = false;
    bool m_on = false;
    qreal m_level = 1;
    // The scene's position: screen = level * scene + offset
    QPointF m_offset;
    bool m_zoomed = false;
    // Touch mode: input reaches the apps, pan locked
    bool m_edit = false;
    struct Scaled
    {
        QPointer<QQuickItem> item;
        QPointer<QObject> scale;
        QPointer<QObject> shift;
        // Its layer was switched on here, and its smoothing before
        bool layer = false;
        bool smooth = false;
    };
    QList<Scaled> m_scaled;

    enum Hold {
        HoldIdle,
        // On the strip, until the long press
        HoldWaiting, HoldArmed,
        HoldPanning, HoldPinching, HoldButton,
        // Zoomed in pan mode, a drag up from the strip
        HoldLevel,
        // Zoomed, a touch on the black around one-handed mode's picture
        HoldBlack,
        // The rest of a touch that isn't ours, or that was ours
        HoldPassing, HoldSwallowing
    } m_hold = HoldIdle;
    // The waiting touch is held back, to be given back
    bool m_holdBack = false;
    bool m_armedFromOff = false;
    int m_touchId = -1;
    QPointF m_touchStart;
    QPointF m_lastPos;
    qreal m_startLevel = 1;
    // Where the strip's drag counts from: the hold, or where a zoom out
    // stopped
    qreal m_dragFrom = 0;
    // The armed hold's drag sets the level
    bool m_dragging = false;
    bool m_zoomingOut = false;
    QTimer m_zoomOutTimer;
    QElapsedTimer m_zoomOutClock;
    bool m_moved = false;
    // The two fingers of a pinch, their distance and middle at the last step
    int m_pinchIds[2] = { -1, -1 };
    qreal m_pinchDistance = 0;
    QPointF m_pinchMiddle;
    TouchHold m_held;
    QTimer m_holdTimer;
    QTimer m_checkTimer;
    QTimer m_labelTimer;
    bool m_atLimit = false;
    // The level in the corner, while it changes
    bool m_showLevel = false;

    // Unmagnified: the ring, the border, the level and the button
    QPointer<QQuickItem> m_overlay;
    QPointer<QQuickItem> m_controls;
    QPointer<QObject> m_colors;
    HoldCue m_cue;
};

#endif
