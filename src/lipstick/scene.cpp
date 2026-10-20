// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "scene.h"
#include "log.h"
#include "units.h"

#include <QMetaProperty>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>

#ifdef HAVE_MLITE
#include <MDConfItem>
#endif

namespace {

// Of the screen's width, when lipstick's value can't be read
const qreal FallbackSideBand = 0.05;
// An item not found is looked for again after this long
const qint64 RetryMs = 5000;

// The window's properties the modes read. A missing required one keeps the
// mode off; the optional ones each lose only their part (a call, a dialog
// or the device lock not seen, the app grid, home's activity, the close
// swipe, the wallpaper; back closing the app grid or the top menu).
struct Hook
{
    const char *name;
    int requiredBy;
};

const Hook Hooks[] = {
    { "topmostWindow", LipstickScene::OneHanded | LipstickScene::Back },
    { "topmostWindowAngle", LipstickScene::OneHanded | LipstickScene::Zoom | LipstickScene::Back },
    { "keyboardHeight", LipstickScene::OneHanded | LipstickScene::Zoom | LipstickScene::Back },
    { "lockScreenLayer", LipstickScene::OneHanded | LipstickScene::Zoom | LipstickScene::Back },
    { "homeLayer", LipstickScene::OneHanded },
    { "topMenuLayer", LipstickScene::OneHanded },
    // Lipstick's own back, from a right click
    { "synthesizeBackEvent", LipstickScene::Back },
    { "deviceIsLocked", 0 },
    { "homeActive", 0 },
    { "dialogLayer", 0 },
    { "alarmLayer", 0 },
    { "launcherLayer", 0 },
    { "appLayer", 0 },
    { "wallpaper", 0 },
};

}

LipstickScene::LipstickScene(QQuickWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    m_clock.start();
    checkHooks();

    connect(window, &QWindow::widthChanged, this, &LipstickScene::frontChanged);
    connect(window, &QWindow::heightChanged, this, &LipstickScene::frontChanged);
    follow(window, "topmostWindow", "frontChanged()");
    follow(window, "topmostWindowAngle", "frontChanged()");
    follow(window, "keyboardHeight", "keyboardChanged()");
    // A dialog's background tells a panel dialog (lowered) from a call or
    // an alarm (full screen); home's activity tells home from an app
    follow(window, "homeActive", "layersChanged()");
    for (const char *name : { "launcherLayer", "lockScreenLayer", "dialogLayer", "alarmLayer", "topMenuLayer" }) {
        if (QObject *layer = object(name)) {
            follow(layer, "exposed", "layersChanged()");
            follow(layer, "renderDialogBackground", "layersChanged()");
        }
    }
    // Lipstick's compositor announces new windows (the keyboard's first)
    if (window->metaObject()->indexOfSignal("windowAdded(QObject*)") >= 0)
        connect(window, SIGNAL(windowAdded(QObject*)), this, SIGNAL(windowAdded(QObject*)));
}

LipstickScene::~LipstickScene()
{
    // Whoever still uses it is gone with the scene
    if (m_wallpaperLayer)
        m_wallpaperLayer->setProperty("enabled", false);
}

void LipstickScene::checkHooks()
{
    for (const Hook &hook : Hooks) {
        // A property declared in compositor.qml exists also while it is null
        if (m_window->property(hook.name).isValid())
            continue;
        m_missingHooks << QLatin1String(hook.name);
        m_unsupported |= hook.requiredBy;
    }
    if (!m_missingHooks.isEmpty())
        FrogfishLog::trace("scene", QStringLiteral("missing %1%2%3%4")
                           .arg(m_missingHooks.join(QStringLiteral(", ")),
                                m_unsupported & OneHanded ? QStringLiteral("; one-handed not supported") : QString(),
                                m_unsupported & Zoom ? QStringLiteral("; zoom not supported") : QString(),
                                m_unsupported & Back ? QStringLiteral("; back not supported") : QString()));
}

void LipstickScene::follow(QObject *object, const char *property, const char *signal)
{
    const int index = object->metaObject()->indexOfProperty(property);
    if (index < 0 || !object->metaObject()->property(index).hasNotifySignal())
        return;
    connect(object, object->metaObject()->property(index).notifySignal(),
            this, metaObject()->method(metaObject()->indexOfSignal(signal)));
}

QQuickWindow *LipstickScene::window() const
{
    return m_window;
}

bool LipstickScene::supports(Feature feature) const
{
    return !(m_unsupported & feature);
}

QStringList LipstickScene::missing() const
{
    QStringList missing = m_missingHooks;
    for (auto it = m_lookups.constBegin(); it != m_lookups.constEnd(); ++it) {
        if (!it.value())
            missing << it.key();
    }
    missing.sort();
    return missing;
}

QString LipstickScene::summary() const
{
    const QStringList missing = this->missing();
    return QStringLiteral("lipstick: missing %1").arg(missing.isEmpty() ? QStringLiteral("nothing")
                                                                        : missing.join(QStringLiteral(", ")));
}

QObject *LipstickScene::object(const char *property) const
{
    return m_window ? m_window->property(property).value<QObject *>() : nullptr;
}

QQuickItem *LipstickScene::item(const char *property) const
{
    return qobject_cast<QQuickItem *>(object(property));
}

bool LipstickScene::exposed(const char *layer) const
{
    QObject *item = object(layer);
    return item && item->property("exposed").toBool();
}

bool LipstickScene::deviceLocked() const
{
    return m_window && m_window->property("deviceIsLocked").toBool();
}

bool LipstickScene::portrait() const
{
    return !m_window || m_window->property("topmostWindowAngle").toInt() == 0;
}

qreal LipstickScene::keyboardHeight() const
{
    return m_window ? m_window->property("keyboardHeight").toReal() : 0;
}

QObject *LipstickScene::topmostApplication() const
{
    QObject *wrapper = object("topmostWindow");
    QObject *window = wrapper ? wrapper->property("window").value<QObject *>() : nullptr;
    // The home screen, the top menu and the other in-process windows are
    // lipstick's own. Calls, alarms and system dialogs (categories "call",
    // "alarm", "dialog") are in lipstick's layers above the apps and stay
    // full screen: applications by lipstick's own rule
    if (!window || window->property("isInProcess").toBool())
        return nullptr;
    const QString category = window->property("category").toString();
    return category.isEmpty() || category == QLatin1String("silica") ? window : nullptr;
}

QQuickItem *LipstickScene::frontDialogLayer() const
{
    // The alarm layer is above the dialog layer
    for (const char *name : { "alarmLayer", "dialogLayer" }) {
        if (exposed(name))
            return item(name);
    }
    return nullptr;
}

QQuickItem *LipstickScene::findByName(const QString &objectName) const
{
    if (!m_window)
        return nullptr;
    // In the visual tree, in-process windows included
    QList<QQuickItem *> items { m_window->contentItem() };
    while (!items.isEmpty()) {
        QQuickItem *item = items.takeLast();
        if (item->objectName() == objectName)
            return item;
        items += item->childItems();
    }
    return nullptr;
}

bool LipstickScene::mayLook(const char *what) const
{
    const auto missed = m_missedAt.constFind(QLatin1String(what));
    return missed == m_missedAt.constEnd() || m_clock.elapsed() - missed.value() >= RetryMs;
}

QQuickItem *LipstickScene::found(const char *what, QQuickItem *item)
{
    m_lookups.insert(QLatin1String(what), item != nullptr);
    if (item)
        m_missedAt.remove(QLatin1String(what));
    else
        m_missedAt.insert(QLatin1String(what), m_clock.elapsed());
    return item;
}

QQuickItem *LipstickScene::coverParent()
{
    // Above the layers, below notification banners and alarms (siblings of
    // the layers' parent)
    if (!m_coverParent && mayLook("LayersParent_cameraArea"))
        m_coverParent = found("LayersParent_cameraArea", findByName(QStringLiteral("LayersParent_cameraArea")));
    return m_coverParent;
}

QQuickItem *LipstickScene::launcherPager()
{
    if (!m_launcherPager && mayLook("Launcher pager")) {
        QQuickItem *pager = nullptr;
        for (QQuickItem *item = findByName(QStringLiteral("Launcher")); item; item = item->parentItem()) {
            if (item->property("openedChildFolder").isValid() && item->property("snapMode").isValid()) {
                pager = item;
                break;
            }
        }
        m_launcherPager = found("Launcher pager", pager);
    }
    return m_launcherPager;
}

QList<QQuickItem *> LipstickScene::iconGrids() const
{
    QList<QQuickItem *> grids;
    QQuickItem *layer = item("launcherLayer");
    if (!layer)
        return grids;
    QList<QQuickItem *> items { layer };
    while (!items.isEmpty()) {
        QQuickItem *item = items.takeLast();
        items += item->childItems();
        if (item->property("minimumCellHeight").isValid() && item->property("rows").isValid())
            grids << item;
    }
    return grids;
}

QQuickItem *LipstickScene::switcher()
{
    if (!m_switcher && mayLook("Switcher_wrapper"))
        m_switcher = found("Switcher_wrapper", findByName(QStringLiteral("Switcher_wrapper")));
    return m_switcher;
}

QQuickItem *LipstickScene::eventsView()
{
    // Events (notifications) is a full-screen window of its own
    if (!m_eventsView && mayLook("EventsView_window"))
        m_eventsView = found("EventsView_window", findByName(QStringLiteral("EventsView_window")));
    return m_eventsView;
}

QQuickItem *LipstickScene::bannerArea()
{
    // NotificationPreview.qml's swipeable area
    if (!m_bannerArea && mayLook("NotificationPreview_popupArea"))
        m_bannerArea = found("NotificationPreview_popupArea",
                             findByName(QStringLiteral("NotificationPreview_popupArea")));
    return m_bannerArea;
}

QQuickItem *LipstickScene::notificationLayer()
{
    // No property of the compositor names it: the one item marked as a
    // layer for OverlayLayer.qml (compositor.qml)
    if (!m_notificationLayer && m_window && mayLook("notification layer")) {
        QQuickItem *layer = nullptr;
        QList<QQuickItem *> items { m_window->contentItem() };
        while (!items.isEmpty() && !layer) {
            QQuickItem *item = items.takeFirst();
            if (item->property("__compositor_is_layer").isValid())
                layer = item;
            items += item->childItems();
        }
        m_notificationLayer = found("notification layer", layer);
    }
    return m_notificationLayer;
}

QList<QQuickItem *> LipstickScene::blurSources()
{
    // Looked for until some are found; they live as long as the compositor
    if (m_blurSources.isEmpty() && m_window) {
        QList<QQuickItem *> items { m_window->contentItem() };
        while (!items.isEmpty()) {
            QQuickItem *item = items.takeLast();
            if (QByteArray(item->metaObject()->className()).startsWith("BlurSource_QML"))
                m_blurSources << item;
            items += item->childItems();
        }
        m_lookups.insert(QStringLiteral("BlurSource"), !m_blurSources.isEmpty());
    }
    QList<QQuickItem *> sources;
    for (const QPointer<QQuickItem> &source : m_blurSources) {
        if (source)
            sources << source;
    }
    return sources;
}

bool LipstickScene::isKeyboard(const QObject *window)
{
    // Maliit's window in lipstick's scene
    return window && window->property("windowId").isValid()
            && window->property("category").toString() == QLatin1String("overlay");
}

QList<QQuickItem *> LipstickScene::keyboards() const
{
    QList<QQuickItem *> keyboards;
    if (!m_window)
        return keyboards;
    QList<QQuickItem *> items { m_window->contentItem() };
    while (!items.isEmpty()) {
        QQuickItem *item = items.takeLast();
        items += item->childItems();
        if (isKeyboard(item))
            keyboards << item;
    }
    return keyboards;
}

bool LipstickScene::isLayersParent(const QQuickItem *item)
{
    return QByteArray(item->metaObject()->className()).startsWith("LayersParent");
}

qreal LipstickScene::sideBand()
{
    if (m_sideBand <= 0 && m_window) {
        // Lipstick's edge strips: compositor.qml sets them on the root as
        // Sailfish.Lipstick's attached PeekFilter properties, read here as
        // a binding there would, through that file's imports
        const QQmlProperty width(m_window, QStringLiteral("PeekFilter.boundaryWidth"), qmlContext(m_window));
        const qreal band = width.isValid() ? width.read().toReal() : 0;
        if (band > 0) {
            m_sideBand = band;
            FrogfishLog::trace("scene", QStringLiteral("side strips %1 px, %2 px/mm")
                               .arg(band).arg(Units::pixelsPerMm(m_window), 0, 'f', 1));
        }
        m_lookups.insert(QStringLiteral("PeekFilter"), m_sideBand > 0);
    }
    if (m_sideBand > 0)
        return m_sideBand;
    return m_window ? m_window->width() * FallbackSideBand : 0;
}

int LipstickScene::launcherRows(bool portrait) const
{
#ifdef HAVE_MLITE
    MDConfItem rows(portrait ? QStringLiteral("/desktop/sailfish/experimental/launcher_rows_portrait")
                             : QStringLiteral("/desktop/sailfish/experimental/launcher_rows_landscape"));
    return qMax(0, rows.value().toInt());
#else
    Q_UNUSED(portrait)
    return 0;
#endif
}

bool LipstickScene::useWallpaperLayer(bool use)
{
    // Silica's shader picks the wallpaper's image from the screen position,
    // so a Translate moves only the quad. In a layer the "screen" is the item
    // itself: the texture holds the normal image and the layer's quad
    // follows the Translate (about 9 MB while used).
    QQuickItem *wallpaper = item("wallpaper");
    QObject *layer = wallpaper ? wallpaper->property("layer").value<QObject *>() : nullptr;
    m_wallpaperUsers = qMax(0, m_wallpaperUsers + (use ? 1 : -1));
    if (use && m_wallpaperUsers == 1 && layer && !layer->property("enabled").toBool()) {
        m_wallpaperLayer = layer;
        layer->setProperty("enabled", true);
        return true;
    }
    if (!use && m_wallpaperUsers == 0 && m_wallpaperLayer) {
        m_wallpaperLayer->setProperty("enabled", false);
        m_wallpaperLayer = nullptr;
        return true;
    }
    return false;
}
