// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "zoom.h"
#include "haptics.h"
#include "log.h"
#include "onehanded.h"
#include "qml.h"
#include "router.h"
#include "scene.h"
#include "units.h"

#include <QDBusConnection>
#include <QLineF>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVector3D>

#include <cmath>

namespace {

const qreal DoublingMm = 20;
// There is no room below the strip for a drag down: zoomed, its hold zooms
// out instead, halving as evenly as the drag doubles
const qreal ZoomOutHalvingMs = 1000;
const int ZoomOutStepMs = 16;
// Let go below this, the zoom ends
const qreal EndLevel = 1.1;
// While zoomed: how often what may end it is checked (the compositor's
// layers have no change signals this could rely on)
const int CheckMs = 250;
// Below the colour filters (z 1e9), above the rings of the other gestures
const qreal OverlayZ = HoldCue::AboveLipstickZ + 0.5;
const int LevelLabelMs = 1200;
const qreal ButtonWidthMm = 9;
const qreal ButtonHeightMm = 17;
const qreal ButtonInsetMm = 2;
// Touches this close to the button count
const qreal ButtonMarginMm = 1.5;

QPointF middle(const QPointF &a, const QPointF &b)
{
    return (a + b) / 2;
}

}

ZoomMode::ZoomMode(LipstickScene *scene, InputRouter *router, OneHandedMode *oneHanded, QObject *parent)
    : QObject(parent)
    , m_scene(scene)
    , m_router(router)
    , m_oneHanded(oneHanded)
{
    m_holdTimer.setSingleShot(true);
    connect(&m_holdTimer, &QTimer::timeout, this, &ZoomMode::holdTimeout);
    m_checkTimer.setInterval(CheckMs);
    connect(&m_checkTimer, &QTimer::timeout, this, &ZoomMode::checkEnd);
    m_labelTimer.setSingleShot(true);
    m_labelTimer.setInterval(LevelLabelMs);
    connect(&m_labelTimer, &QTimer::timeout, this, &ZoomMode::hideLevel);
    m_zoomOutTimer.setInterval(ZoomOutStepMs);
    connect(&m_zoomOutTimer, &QTimer::timeout, this, &ZoomMode::zoomOutStep);
    connect(scene, &LipstickScene::frontChanged, this, &ZoomMode::checkEnd);
    connect(scene, &LipstickScene::keyboardChanged, this, &ZoomMode::checkEnd);
    QDBusConnection::systemBus().connect(QStringLiteral("com.nokia.mce"),
                                         QStringLiteral("/com/nokia/mce/signal"),
                                         QStringLiteral("com.nokia.mce.signal"),
                                         QStringLiteral("display_status_ind"),
                                         this, SLOT(displayStatus(QString)));
}

ZoomMode::~ZoomMode()
{
    m_holdTimer.stop();
    m_checkTimer.stop();
    if (m_zoomed)
        end(QStringLiteral("removed"));
    delete m_overlay;
}

QQuickWindow *ZoomMode::window() const
{
    return m_scene->window();
}

void ZoomMode::trace(const QString &message)
{
    FrogfishLog::trace("zoom", message);
}

void ZoomMode::configure(const FrogfishSettings::Values &values, bool allowed)
{
    m_config = values;
    const bool on = allowed && values.zoom;
    if (!on && m_zoomed)
        end(QStringLiteral("switched off"));
    if (on == m_on && m_configured)
        return;
    m_configured = true;
    m_on = on;
    trace(on ? QStringLiteral("on") : QStringLiteral("off"));
}

QString ZoomMode::diagnostics() const
{
    QString line = QStringLiteral("zoom: %1").arg(m_on ? QStringLiteral("on") : QStringLiteral("off"));
    if (m_zoomed)
        line += QStringLiteral(", zoomed %1x").arg(m_level, 0, 'f', 1);
    return line;
}

qreal ZoomMode::levelForDrag(qreal up) const
{
    return std::pow(2.0, up / (DoublingMm * mm()));
}

qreal ZoomMode::mm() const
{
    return Units::pixelsPerMm(window());
}

bool ZoomMode::canStart() const
{
    if (!window() || !m_on || !m_scene->portrait())
        return false;
    if (m_scene->exposed("lockScreenLayer") || m_scene->deviceLocked()
            || m_scene->exposed("dialogLayer") || m_scene->exposed("alarmLayer"))
        return false;
    return !m_scene->keyboardOpen();
}

bool ZoomMode::inStrip(const QPointF &point) const
{
    QQuickWindow *window = this->window();
    const qreal corner = HoldGesture::CornerMm * mm();
    return point.y() >= window->height() - HoldGesture::StripMm * mm()
            && point.x() >= corner && point.x() < window->width() - corner;
}

QRectF ZoomMode::buttonRect() const
{
    QQuickWindow *window = this->window();
    if (!window)
        return QRectF();
    const qreal width = ButtonWidthMm * mm();
    const qreal height = ButtonHeightMm * mm();
    const qreal inset = ButtonInsetMm * mm();
    return QRectF(m_config.leftHanded ? inset : window->width() - inset - width,
                  window->height() - inset - height, width, height);
}

QRectF ZoomMode::picture() const
{
    QQuickWindow *window = this->window();
    if (!window)
        return QRectF();
    const QRectF screen(0, 0, window->width(), window->height());
    const QRectF picture = m_oneHanded ? m_oneHanded->picture() : QRectF();
    return picture.isValid() && screen.contains(picture) ? picture : screen;
}

bool ZoomMode::touch(QTouchEvent *event)
{
    if (!window())
        return false;
    const bool finished = event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel;
    if (event->type() == QEvent::TouchBegin) {
        m_holdTimer.stop();
        m_held.clear();
        m_hold = HoldIdle;
        return m_on && touchBegin(event);
    }
    if (m_hold == HoldIdle)
        return false;
    if (m_hold == HoldSwallowing || m_hold == HoldPassing) {
        const bool ours = m_hold == HoldSwallowing;
        if (finished)
            m_hold = HoldIdle;
        return ours;
    }

    const QList<QTouchEvent::TouchPoint> &points = event->touchPoints();
    QPointF pos = m_lastPos;
    // Our finger lifted while another stays down: no TouchEnd comes, but our
    // gesture is over (the rest of the touch is swallowed)
    bool released = false;
    for (const QTouchEvent::TouchPoint &point : points) {
        if (point.id() == m_touchId) {
            pos = point.pos();
            released = point.state() == Qt::TouchPointReleased;
        }
    }
    bool done = finished || released;
    if (QLineF(m_touchStart, pos).length() > HoldGesture::SlopMm * mm())
        m_moved = true;
    const bool anotherFinger = points.count() > 1 || (event->touchPointStates() & Qt::TouchPointPressed);

    switch (m_hold) {
    case HoldWaiting:
        if (m_zoomed && dragPans() && anotherFinger && !done) {
            m_holdTimer.stop();
            m_held.clear();
            startPinch(points);
            break;
        }
        if (m_zoomed && dragPans() && m_moved && !done) {
            m_holdTimer.stop();
            m_held.clear();
            const QPointF delta = pos - m_touchStart;
            if (-delta.y() >= qAbs(delta.x())) {
                m_hold = HoldLevel;
                m_startLevel = m_level;
                m_dragFrom = m_touchStart.y();
                trace(QStringLiteral("strip: drag up at level %1").arg(m_level, 0, 'f', 2));
                applyLevel(m_startLevel * levelForDrag(m_dragFrom - pos.y()), zoomCentre());
                break;
            }
            m_hold = HoldPanning;
            pan(pos - m_lastPos);
            break;
        }
        if (anotherFinger || m_moved || done) {
            m_holdTimer.stop();
            if (!m_holdBack)
                break;  // read-only: a tap is nothing
            // Not a hold: given back, this event and the rest follow
            m_held.replay(m_router);
            m_hold = finished ? HoldIdle : HoldPassing;
            return false;
        }
        if (m_holdBack)
            m_held.hold(event);
        break;
    case HoldArmed:
        if (!done) {
            if (!m_dragging && m_touchStart.y() - pos.y() > HoldGesture::SlopMm * mm()) {
                m_dragging = true;
                if (m_zoomingOut)
                    stopZoomOut(pos);
            }
            if (m_dragging)
                applyLevel(m_startLevel * levelForDrag(m_dragFrom - pos.y()), zoomCentre());
        }
        if (done) {
            m_zoomOutTimer.stop();
            m_zoomingOut = false;
            settle(QStringLiteral("strip"));
        }
        break;
    case HoldLevel:
        applyLevel(m_startLevel * levelForDrag(m_dragFrom - pos.y()), zoomCentre());
        if (done)
            settle(QStringLiteral("strip drag"));
        break;
    case HoldPanning:
        if (anotherFinger && !done) {
            startPinch(points);
            break;
        }
        pan(pos - m_lastPos);
        break;
    case HoldPinching:
        pinch(points);
        if (m_hold == HoldPinching && finished)
            settle(QStringLiteral("pinch"));
        done = finished;
        break;
    case HoldBlack:
        if (dragPans() && anotherFinger && !done) {
            startPinch(points);
            break;
        }
        if (dragPans() && m_moved && !done) {
            m_hold = HoldPanning;
            pan(pos - m_lastPos);
            break;
        }
        if (done && !m_moved && !anotherFinger && event->type() != QEvent::TouchCancel)
            end(QStringLiteral("tap on the black: zoom off"));
        break;
    case HoldButton: {
        // The half touched: pan above, touch below
        const QRectF button = buttonRect();
        const qreal margin = ButtonMarginMm * mm();
        if (done && event->type() != QEvent::TouchCancel
                && button.adjusted(-margin, -margin, margin, margin).contains(pos)
                && (pos.y() >= button.center().y()) != m_edit) {
            m_edit = !m_edit;
            trace(m_edit ? QStringLiteral("touch mode: input on, pan locked")
                         : QStringLiteral("pan mode: read-only"));
            updateOverlay();
        }
        break;
    }
    case HoldIdle:
    case HoldPassing:
    case HoldSwallowing:
        break;
    }
    m_lastPos = pos;
    if (done && m_hold != HoldIdle) {
        m_holdTimer.stop();
        m_hold = finished ? HoldIdle : HoldSwallowing;
    }
    return true;
}

bool ZoomMode::touchBegin(QTouchEvent *event)
{
    const QList<QTouchEvent::TouchPoint> &points = event->touchPoints();
    if (points.isEmpty())
        return m_zoomed && dragPans();
    const QPointF pos = points.first().pos();
    m_touchId = points.first().id();
    m_touchStart = pos;
    m_lastPos = pos;
    m_moved = false;
    const bool single = points.count() == 1;

    if (!m_zoomed) {
        // Where first: lipstick is read only for touches on the strip
        if (!single || !inStrip(pos) || !canStart())
            return false;
        m_hold = HoldWaiting;
        m_holdBack = true;
        m_held.hold(event);
        m_holdTimer.start(HoldGesture::ArmMs);
        return true;
    }
    // The button is the zoom's in pan and in touch mode
    const qreal margin = ButtonMarginMm * mm();
    if (single && buttonRect().adjusted(-margin, -margin, margin, margin).contains(pos)) {
        m_hold = HoldButton;
        return true;
    }
    if (!picture().contains(pos)) {
        // The black around one-handed mode's picture: what is under it is
        // hidden. A tap there ends the zoom (and the next one one-handed
        // mode, as everywhere a tap on the black goes back a step).
        if (single) {
            m_hold = HoldBlack;
            return true;
        }
        if (!dragPans()) {
            m_hold = HoldSwallowing;
            return true;
        }
    }
    if (single && inStrip(pos)) {
        // In touch mode a tap there is the app's
        m_hold = HoldWaiting;
        m_holdBack = m_edit;
        if (m_holdBack)
            m_held.hold(event);
        m_holdTimer.start(HoldGesture::ArmMs);
        return true;
    }
    if (!dragPans()) {
        m_hold = HoldPassing;
        return false;
    }
    if (!single) {
        startPinch(points);
        return true;
    }
    m_hold = HoldPanning;
    return true;
}

void ZoomMode::startPinch(const QList<QTouchEvent::TouchPoint> &points)
{
    QList<QTouchEvent::TouchPoint> down;
    for (const QTouchEvent::TouchPoint &point : points) {
        if (point.state() != Qt::TouchPointReleased)
            down << point;
    }
    if (down.count() < 2) {
        m_hold = HoldSwallowing;
        return;
    }
    m_hold = HoldPinching;
    m_pinchIds[0] = down.at(0).id();
    m_pinchIds[1] = down.at(1).id();
    m_pinchDistance = QLineF(down.at(0).pos(), down.at(1).pos()).length();
    m_pinchMiddle = middle(down.at(0).pos(), down.at(1).pos());
    m_startLevel = m_level;
}

void ZoomMode::pinch(const QList<QTouchEvent::TouchPoint> &points)
{
    QPointF at[2];
    int found = 0;
    bool lifted = false;
    for (const QTouchEvent::TouchPoint &point : points) {
        for (int i = 0; i < 2; ++i) {
            if (point.id() == m_pinchIds[i]) {
                at[i] = point.pos();
                ++found;
                lifted = lifted || point.state() == Qt::TouchPointReleased;
            }
        }
    }
    if (found < 2)
        return;
    const qreal distance = QLineF(at[0], at[1]).length();
    const QPointF centre = middle(at[0], at[1]);
    if (m_pinchDistance > 0 && distance > 0) {
        // The scene's point between the fingers stays between them: the
        // level around it, then the pan by the middle's move
        applyLevel(m_level * distance / m_pinchDistance, m_pinchMiddle);
        pan(centre - m_pinchMiddle);
    }
    m_pinchDistance = distance;
    m_pinchMiddle = centre;
    if (lifted) {
        settle(QStringLiteral("pinch"));
        if (m_hold == HoldPinching)
            m_hold = HoldSwallowing;
    }
}

QPointF ZoomMode::zoomCentre() const
{
    QQuickWindow *window = this->window();
    const QRectF screen(0, 0, window->width(), window->height());
    const QRectF area = picture();
    if (area != screen)
        return area.center();
    return QPointF(m_touchStart.x(), screen.height() / 2);
}

void ZoomMode::settle(const QString &what)
{
    trace(QStringLiteral("%1: level %2").arg(what).arg(m_level, 0, 'f', 2));
    if (m_zoomed && m_level < EndLevel)
        end(what + QStringLiteral(" to 1x: zoom off"));
}

void ZoomMode::holdTimeout()
{
    if (m_hold != HoldWaiting)
        return;
    // Ours now: what was held is never given back
    m_held.clear();
    m_armedFromOff = !m_zoomed;
    if (!m_zoomed)
        start();
    arm();
}

void ZoomMode::arm()
{
    m_hold = HoldArmed;
    m_startLevel = m_level;
    m_dragFrom = m_touchStart.y();
    m_dragging = false;
    m_moved = false;
    trace(QStringLiteral("held on the strip at level %1").arg(m_level, 0, 'f', 2));
    showCue(m_touchStart);
    Haptics::press();
    if (!m_armedFromOff)
        startZoomOut();
}

void ZoomMode::startZoomOut()
{
    if (m_zoomingOut)
        return;
    m_zoomingOut = true;
    m_zoomOutClock.start();
    m_zoomOutTimer.start();
    trace(QStringLiteral("strip: zooming out from %1").arg(m_level, 0, 'f', 2));
}

void ZoomMode::stopZoomOut(const QPointF &at)
{
    m_zoomOutTimer.stop();
    m_zoomingOut = false;
    m_startLevel = m_level;
    m_dragFrom = at.y();
    trace(QStringLiteral("strip: zoom out stopped at %1").arg(m_level, 0, 'f', 2));
}

void ZoomMode::zoomOutStep()
{
    // By the time passed, whatever the timer's pace
    const qreal elapsed = m_zoomOutClock.restart();
    applyLevel(m_level * std::pow(2.0, -elapsed / ZoomOutHalvingMs), zoomCentre());
    if (m_level <= 1)
        end(QStringLiteral("strip held to 1x: zoom off"));
}

void ZoomMode::setLevel(qreal level, const QPointF &fixed)
{
    // The scene's point under the fixed one stays there
    const QPointF scene = (fixed - m_offset) / m_level;
    m_level = level;
    m_offset = fixed - scene * level;
    place();
}

void ZoomMode::applyLevel(qreal level, const QPointF &fixed)
{
    const qreal maximum = m_config.zoomMaximum;
    const qreal bounded = qBound(qreal(1), level, maximum);
    const bool atLimit = bounded != level || bounded == 1 || bounded == maximum;
    if (atLimit && !m_atLimit && bounded != m_level)
        Haptics::press();
    m_atLimit = atLimit;
    setLevel(bounded, fixed);
    showLevel();
}

void ZoomMode::showLevel()
{
    m_showLevel = true;
    m_labelTimer.start();
    updateOverlay();
}

void ZoomMode::hideLevel()
{
    m_showLevel = false;
    updateOverlay();
}

void ZoomMode::pan(const QPointF &delta)
{
    m_offset += delta;
    place();
}

void ZoomMode::place()
{
    if (!window())
        return;
    // Never magnified black from around one-handed mode's picture
    const QRectF area = picture();
    m_offset.setX(qBound(area.right() - m_level * area.right(), m_offset.x(),
                         area.left() - m_level * area.left()));
    m_offset.setY(qBound(area.bottom() - m_level * area.bottom(), m_offset.y(),
                         area.top() - m_level * area.top()));
    scaleScene();
    updateOverlay();
}

void ZoomMode::scaleScene()
{
    // Each of lipstick's top-level items gets a Scale and a Translate at the
    // end of its transform list: screen = level * scene + offset. Touches
    // follow through Qt's own mapping. LayersParent (home, apps, the menus)
    // is drawn into a layer at its normal size first: Silica's backgrounds
    // there (ambience under apps, glass, wallpaper) pick their image by
    // screen position, which inside the layer is the normal one.
    QQuickWindow *window = this->window();
    QQmlEngine *engine = window ? qmlEngine(window) : nullptr;
    if (!engine)
        return;
    for (QQuickItem *child : window->contentItem()->childItems()) {
        // Not its own overlay, and not a layer's drawing item: Qt puts it
        // next to the layered item and gives it that item's transforms itself
        const QByteArray type(child->metaObject()->className());
        if (child == m_overlay || type.startsWith("QQuickShaderEffect")
                || type.startsWith("QQuickOpenGLShaderEffect"))
            continue;
        Scaled *entry = nullptr;
        for (Scaled &scaled : m_scaled) {
            if (scaled.item == child)
                entry = &scaled;
        }
        if (!entry) {
            Scaled scaled;
            scaled.item = child;
            scaled.scale = Qml::appendTransform(engine, child, "Scale", this);
            scaled.shift = Qml::appendTransform(engine, child, "Translate", this);
            if (!scaled.scale || !scaled.shift) {
                Qml::removeTransforms(engine, child, { scaled.scale, scaled.shift });
                return;
            }
            QObject *layer = child->property("layer").value<QObject *>();
            if (layer && LipstickScene::isLayersParent(child) && !layer->property("enabled").toBool()) {
                scaled.smooth = layer->property("smooth").toBool();
                layer->setProperty("smooth", true);
                layer->setProperty("enabled", true);
                scaled.layer = true;
                trace(QStringLiteral("scene layer on"));
            }
            m_scaled << scaled;
            entry = &m_scaled.last();
        }
        // About the scene's origin, in the item's own coordinates
        entry->scale->setProperty("origin", QVector3D(-child->x(), -child->y(), 0));
        entry->scale->setProperty("xScale", m_level);
        entry->scale->setProperty("yScale", m_level);
        entry->shift->setProperty("x", m_offset.x());
        entry->shift->setProperty("y", m_offset.y());
    }
}

void ZoomMode::unscaleScene()
{
    QQuickWindow *window = this->window();
    QQmlEngine *engine = window ? qmlEngine(window) : nullptr;
    for (const Scaled &scaled : m_scaled) {
        if (scaled.item && scaled.layer) {
            if (QObject *layer = scaled.item->property("layer").value<QObject *>()) {
                layer->setProperty("enabled", false);
                layer->setProperty("smooth", scaled.smooth);
            }
            trace(QStringLiteral("scene layer off"));
        }
        Qml::removeTransforms(engine, scaled.item, { scaled.scale, scaled.shift });
    }
    m_scaled.clear();
}

void ZoomMode::start()
{
    // In use before the first change
    m_zoomed = true;
    emit inUseChanged(true);
    m_edit = false;
    m_level = 1;
    m_offset = QPointF();
    m_checkTimer.start();
    trace(QStringLiteral("zoom on"));
    place();
}

void ZoomMode::end(const QString &why)
{
    if (!m_zoomed)
        return;
    m_zoomed = false;
    m_edit = false;
    m_checkTimer.stop();
    m_labelTimer.stop();
    m_zoomOutTimer.stop();
    m_zoomingOut = false;
    m_showLevel = false;
    m_level = 1;
    m_offset = QPointF();
    unscaleScene();
    updateOverlay();
    // A touch still down stays ours to its end
    if (m_hold != HoldIdle && m_hold != HoldPassing)
        m_hold = HoldSwallowing;
    m_holdTimer.stop();
    m_held.clear();
    trace(why);
    emit inUseChanged(false);
}

void ZoomMode::checkEnd()
{
    if (!m_zoomed || !window())
        return;
    // Typing on a magnified keyboard needs panning all the time, and a zoom
    // coming back by itself after it closes is confusing
    if (m_scene->keyboardOpen())
        end(QStringLiteral("keyboard open: zoom off"));
    else if (!m_scene->portrait())
        end(QStringLiteral("rotated: zoom off"));
    else if (m_scene->exposed("lockScreenLayer") || m_scene->deviceLocked())
        end(QStringLiteral("lock screen: zoom off"));
    else if (m_scene->exposed("dialogLayer") || m_scene->exposed("alarmLayer"))
        end(QStringLiteral("dialog or alarm: zoom off"));
    else
        place();
}

void ZoomMode::displayStatus(const QString &status)
{
    if (status == QLatin1String("off"))
        end(QStringLiteral("display off: zoom off"));
}

void ZoomMode::updateOverlay()
{
    QQuickWindow *window = this->window();
    if (!window)
        return;
    if (!m_overlay) {
        if (!m_zoomed)
            return;
        QQmlEngine *engine = qmlEngine(window);
        if (!engine)
            return;
        m_overlay = new QQuickItem;
        m_overlay->setObjectName(QStringLiteral("frogfish-zoom-overlay"));
        m_overlay->setParent(this);
        m_overlay->setParentItem(window->contentItem());
        m_overlay->setZ(OverlayZ);
        m_colors = Qml::create(engine, "Colors.qml", true);
        if (m_colors)
            m_colors->setParent(m_overlay);
        else
            qCInfo(lcFrogfish) << "Zoom: no Silica theme, plain colours";
        m_controls = Qml::createItem(engine, "Overlay.qml");
        if (m_controls) {
            m_controls->setObjectName(QStringLiteral("frogfish-zoom-controls"));
            m_controls->setParent(m_overlay);
            m_controls->setParentItem(m_overlay);
            m_controls->setProperty("colors", QVariant::fromValue<QObject *>(m_colors));
        }
    }
    m_overlay->setSize(window->size());
    // The ring too: it plays on the overlay, and ends with the zoom
    m_overlay->setVisible(m_zoomed);
    if (m_controls) {
        m_controls->setSize(window->size());
        m_controls->setVisible(m_zoomed);
        m_controls->setProperty("unit", mm());
        m_controls->setProperty("level", m_level);
        m_controls->setProperty("edit", m_edit);
        m_controls->setProperty("showLevel", m_showLevel);
        m_controls->setProperty("rightHand", !m_config.leftHanded);
        m_controls->setProperty("button", buttonRect());
        m_controls->setProperty("picture", picture());
    }
}

void ZoomMode::showCue(const QPointF &at)
{
    updateOverlay();
    if (!m_overlay)
        return;
    m_cue.show(m_overlay, at, mm(), m_touchId);
}
