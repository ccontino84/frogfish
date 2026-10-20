// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_ROUTER_H
#define FROGFISH_ROUTER_H

#include <QObject>
#include <QPointer>

class BackMode;
class OneHandedMode;
class QQuickWindow;
class QTouchEvent;
class ZoomMode;

// The only event filter, on the application (the compositor's touches reach
// its window through it). Anything but a touch on the window passes at once.
// A touch is offered to zoom first, then to back (its edge is the screen's,
// zoomed or not), then, unless zoomed, to one-handed mode: while zoomed the
// positions don't match what one-handed mode shows.
class InputRouter : public QObject
{
    Q_OBJECT

public:
    explicit InputRouter(QQuickWindow *window, QObject *parent = nullptr);
    ~InputRouter() override;

    void setModes(ZoomMode *zoom, BackMode *back, OneHandedMode *oneHanded);

    // A touch to the window past the modes: held touches given back, a tap
    // made up; also from inside the filter
    void send(QTouchEvent *event);
    // A touch is down on the window
    bool touching() const { return m_touching; }
    // A touch whose end never came (another filter took it)
    void forgetTouch() { m_touching = false; }

    bool eventFilter(QObject *object, QEvent *event) override;

signals:
    // The last finger lifted, once that event is through
    void touchEnded();

private:
    QPointer<QQuickWindow> m_window;
    QPointer<ZoomMode> m_zoom;
    QPointer<BackMode> m_back;
    QPointer<OneHandedMode> m_oneHanded;
    int m_sending = 0;
    bool m_touching = false;
};

#endif
