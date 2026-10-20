// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_SCENE_H
#define FROGFISH_SCENE_H

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QQuickItem;
class QQuickWindow;

// Every read of lipstick's compositor QML, in one place: the window's
// properties (compositor.qml's root), items found by objectName, class name
// or marker. Items are looked up when first needed and kept; a lookup runs
// again only once the item is gone. Nothing of Frogfish is left on
// lipstick's objects.
//
// Hooks are the window's properties the modes rely on. They are checked
// once, at start: a mode whose required hooks are missing stays off
// (supports()), an optional one only loses its own part.
class LipstickScene : public QObject
{
    Q_OBJECT

public:
    enum Feature { OneHanded = 1, Zoom = 2, Back = 4 };

    explicit LipstickScene(QQuickWindow *window, QObject *parent = nullptr);
    ~LipstickScene() override;

    QQuickWindow *window() const;

    // All of the feature's required hooks were found
    bool supports(Feature feature) const;
    // Hooks and items not found, for the diagnostics
    QStringList missing() const;
    // One line for the diagnostics
    QString summary() const;

    // The window's properties
    QObject *object(const char *property) const;
    QQuickItem *item(const char *property) const;
    // A layer property (lockScreenLayer...) whose "exposed" is true
    bool exposed(const char *layer) const;
    bool deviceLocked() const;
    // The window in front is upright; the gestures apply only then
    bool portrait() const;
    qreal keyboardHeight() const;
    bool keyboardOpen() const { return keyboardHeight() > 0; }
    // The window in front if it's an application's, not lipstick's own
    QObject *topmostApplication() const;
    // The dialog or alarm layer in front, if one is shown
    QQuickItem *frontDialogLayer() const;

    // Items found in the scene
    // The empty area's cover goes in here (objectName LayersParent_cameraArea)
    QQuickItem *coverParent();
    // The app grid's pager: the ancestor of the grid ("Launcher") with
    // openedChildFolder
    QQuickItem *launcherPager();
    // Every icon grid under the launcher layer (folders come and go)
    QList<QQuickItem *> iconGrids() const;
    QQuickItem *switcher();
    QQuickItem *eventsView();
    // The notification banner's swipeable area
    QQuickItem *bannerArea();
    // The layer of notification banners, marked for OverlayLayer.qml
    QQuickItem *notificationLayer();
    // The blurred background snapshots (BlurSource.qml)
    QList<QQuickItem *> blurSources();
    // The keyboard's windows (category "overlay")
    QList<QQuickItem *> keyboards() const;
    static bool isKeyboard(const QObject *window);
    // Lipstick's top-level parent of home, apps and the menus
    static bool isLayersParent(const QQuickItem *item);
    // The width of lipstick's side strips, where its edge swipes start
    qreal sideBand();
    // Silica's configured app grid rows; 0 if not set
    int launcherRows(bool portrait) const;

    // The wallpaper's layer, counted: on while anyone needs it, never off if
    // lipstick had it on. True if this switched it.
    bool useWallpaperLayer(bool use);

signals:
    // The window in front, its rotation, the window's size
    void frontChanged();
    // A layer shown or hidden, home active or not
    void layersChanged();
    void keyboardChanged();
    // A new compositor window (the keyboard's first)
    void windowAdded(QObject *window);

private:
    void checkHooks();
    void follow(QObject *object, const char *property, const char *signal);
    QQuickItem *findByName(const QString &objectName) const;
    // Not again within a few seconds of a lookup that found nothing: some
    // are asked for at every touch
    bool mayLook(const char *what) const;
    // Records a lookup's result for the diagnostics
    QQuickItem *found(const char *what, QQuickItem *item);

    QPointer<QQuickWindow> m_window;
    // Required hooks by feature, and every hook missing
    QStringList m_missingHooks;
    int m_unsupported = 0;
    // Lookups run, and whether they found something
    QHash<QString, bool> m_lookups;
    QElapsedTimer m_clock;
    QHash<QString, qint64> m_missedAt;
    QPointer<QQuickItem> m_coverParent;
    QPointer<QQuickItem> m_launcherPager;
    QPointer<QQuickItem> m_switcher;
    QPointer<QQuickItem> m_eventsView;
    QPointer<QQuickItem> m_bannerArea;
    QPointer<QQuickItem> m_notificationLayer;
    QList<QPointer<QQuickItem>> m_blurSources;
    qreal m_sideBand = -1;
    int m_wallpaperUsers = 0;
    // The wallpaper's layer was switched on here
    QPointer<QObject> m_wallpaperLayer;
};

#endif
