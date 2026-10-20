// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_ONEHANDED_H
#define FROGFISH_ONEHANDED_H

#include "moveditem.h"
#include "policy.h"
#include "settings.h"
#include "hold.h"
#include "holdcue.h"

#include <QList>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QTimer>

class InputRouter;
class LipstickScene;
class QQuickItem;
class QTouchDevice;
class QTouchEvent;

// One-handed mode (onehanded_mode): the picture in front comes down to a
// share of the screen's height (onehanded_size), pulled, shrunk or both. The
// corner's hold and the top menu switch turn it on and off; a hold on the
// edge strip, then a drag, sets the size. A black cover lies over the empty
// part: a swipe down there opens the top menu, near a side it closes the
// app, a tap ends one-handed mode. Portrait only. What follows what is in
// front is decided in policy.h; everything changed is put back when it ends.
//
// While typing the app's text field is above the keyboard in the app's own
// coordinates: Pull puts the app back at the top and leaves the edge to it;
// Shrink and Pull and shrink keep their scale and shrink the app with the
// keyboard from the bottom corner.
class OneHandedMode : public QObject
{
    Q_OBJECT

public:
    OneHandedMode(LipstickScene *scene, InputRouter *router, QObject *parent = nullptr);
    ~OneHandedMode() override;

    // The settings, and whether it may run at all (supported, not stopped
    // by the in-use guard)
    void configure(const FrogfishSettings::Values &values, bool allowed);
    // From the router: true if the touch is taken
    bool touch(QTouchEvent *event);
    // On right now: pulled, or shrunk below full size
    bool active() const { return m_active; }
    // The top menu switch: on or off as the corner does it; refused where
    // the corner is (the lock screen, a call, typing, landscape, mode none)
    void setActive(bool active);

    // Something of lipstick's is changed
    bool inUse() const { return m_inUse; }
    // Where the picture in front is, for the zoom: the screen when nothing
    // is lowered
    QRectF picture() const { return m_picture; }
    // A line for the diagnostics
    QString diagnostics() const;

    // What lipstick shows, for the rules
    static OneHandedPolicy::Scene policyScene(const LipstickScene *scene);

signals:
    void inUseChanged(bool inUse);
    void activeChanged(bool active);

private slots:
    void apply();
    void placeApplication();
    void updateLayers();
    void keyboardChanged();
    void updateGridRows();
    void folderChanged();
    void windowAdded(QObject *window);
    void holdTimeout();
    void updateTopMenu();

private:
    OneHandedPolicy::State state() const;
    // Where the picture in front starts for that placement
    qreal pictureTop(OneHandedPolicy::Placement placement) const;
    QQuickWindow *window() const;
    bool pullMode() const { return m_config.mode == FrogfishSettings::Pull; }
    bool shrinkMode() const { return m_config.mode == FrogfishSettings::Shrink; }
    bool pullShrinkMode() const { return m_config.mode == FrogfishSettings::PullShrink; }
    // The corner and the top menu: on at the size set, or off. Applied once
    // the event that asked is through.
    void toggle(const QString &by);
    void reportActive();
    // Where the picture's top is, which everything follows; 0 when nothing
    // is lowered
    qreal sharedOffset() const;
    qreal shrinkFactor() const;
    // Where the application in front starts: the offset, 0 while typing
    qreal applicationOffset() const;
    void setInUse(bool inUse);
    void updateMovedWindow(bool lowered);
    void restoreWindow();
    void scaleKeyboard(qreal factor);
    // Everything to the shared offset; home is laid out again only when
    // final
    void offsetChanged(bool final);
    void setHomeTop(qreal top, qreal factor);
    // Its height and rows only when final (the end of a drag)
    void updateLauncher(qreal offset, bool final = true);
    int defaultGridRows(QQuickItem *grid, int fit) const;
    void refreshBlurSources();
    void setWallpaperLayer(bool on);
    void updateDialogLayers();
    void updateNotificationLayer();
    void updateCover();
    void updateKeyboard();
    bool edgeSwitchesOn() const;
    bool inTriggerCorner(const QPointF &point) const;
    bool inEdgeStrip(const QPointF &point) const;
    // Beside what is shrunk, below the top
    bool inEmptySide(const QPointF &point, qreal top) const;
    // A shown notification banner under the point: its touches are its own
    bool onBanner(const QPointF &point);
    // Where the empty area that is ours ends; -1 if there is none
    qreal emptyAreaTop() const;
    // The corner and the edge: true if the event is held back or taken
    bool holdBack(QTouchEvent *event);
    // The edge drag, the finger at y: the point under it stays there
    void dragSize(qreal y);
    void snapToFullSize();
    void switchOffAtFullSize();
    void switchOff(const QString &why);
    void endHold();
    // True if the touch is taken (a swipe or a tap on the empty area)
    bool emptyAreaTouch(QTouchEvent *event);
    void directGesture(QTouchEvent *event);
    // A tap above a lowered dialog: given to the dialog below its panel,
    // where it dismisses it like a tap outside
    bool dismissDialog(QTouchDevice *device);
    qreal mm() const;
    void trace(const QString &message);

    LipstickScene *m_scene;
    InputRouter *m_router;
    FrogfishSettings::Values m_config;
    bool m_allowed = false;
    // Allowed and switched on
    bool m_enabled = false;
    // On (pulled, or shrunk below full size); lowered while also portrait
    bool m_active = false;
    bool m_lowered = false;
    bool m_inUse = false;
    bool m_reportedActive = false;
    // The share of the screen's height the picture keeps while on (1: full)
    qreal m_size = 1;
    qreal m_dragStartSize = 1;
    // For the traces
    QString m_appliedMode;
    QString m_frontName;
    QRectF m_picture;

    // The application window moved down, and the keyboard's windows
    QPointer<QObject> m_movedWindow;
    PlacedItem m_application;
    QList<PlacedItem> m_keyboards;
    bool m_keyboardsSearched = false;
    // Lipstick's items that follow the offset
    MovedItem m_movedWallpaper;
    MovedItem m_movedHome;
    MovedItem m_movedStatusBar;
    MovedItem m_movedTopMenu;
    MovedItem m_movedLauncher;
    MovedItem m_movedDialogs;
    MovedItem m_movedAlarms;
    MovedItem m_movedBanners;
    // Home's and Events' margins, the cards' page height
    SavedProperties m_homeLayout;
    SavedProperties m_switcherLayout;
    qreal m_homeTop = 0;
    qreal m_homeFactor = 1;
    qreal m_topMenuOffset = 0;
    QPointer<QQuickItem> m_topMenu;
    // The top menu's height was set here: it follows its content from now on
    bool m_topMenuSized = false;
    QPointer<QQuickItem> m_launcherPager;
    qreal m_launcherOffset = 0;
    // Grids whose rows binding was replaced here
    QList<QPointer<QQuickItem>> m_gridRows;
    bool m_wallpaperLayerOn = false;
    // Where the blurred backgrounds were last taken
    qreal m_blurOffset = 0;
    qreal m_blurFactor = 1;

    // Frogfish's own items
    QPointer<QQuickItem> m_cover;
    QPointer<QQuickItem> m_coverSide;
    QPointer<QQuickItem> m_emptyArea;
    QPointer<QQuickItem> m_homeEmptyArea;
    HoldCue m_cue;

    // The hold in the corner or on the edge
    enum Hold { HoldIdle, HoldWaiting, HoldArmed, HoldDropping, HoldDragging } m_hold = HoldIdle;
    bool m_holdOnEdge = false;
    TouchHold m_held;
    int m_holdId = -1;
    QPointF m_holdStart;
    QTimer m_holdTimer;
    // A touch on the empty area
    bool m_swallowing = false;
    bool m_directActive = false;
    bool m_directMoved = false;
    QPointF m_directStart;
};

#endif
