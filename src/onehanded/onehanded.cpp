// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "onehanded.h"
#include "haptics.h"
#include "log.h"
#include "qml.h"
#include "router.h"
#include "scene.h"
#include "units.h"

#include <QDateTime>
#include <QLineF>
#include <QMetaProperty>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTouchEvent>

#include <cmath>

namespace {

// The edge drag goes no smaller: below it too little is left to use
const qreal SmallestSize = FrogfishSettings::MinimumSize / 100.0;
// Lifted this close to full size: full size, not a few pixels nobody notices
const qreal SnapToFullSizeMm = 10;
// Lipstick's own threshold for edge swipes; its top accept margin (the close
// gesture's corners) is at least as wide
const qreal SwipeMm = 10;
// Above lipstick's layers
const qreal CoverZ = 1e6;

// Lies over the empty part of the screen above a laid-out application:
// lipstick's edge drags need an item under the finger, and taps there must
// not reach what is behind
class EmptyArea : public QQuickItem
{
public:
    explicit EmptyArea(QQuickItem *parent, const QString &name)
        : QQuickItem(parent)
    {
        setObjectName(name);
        setAcceptedMouseButtons(Qt::LeftButton);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override { event->accept(); }
};

}

OneHandedMode::OneHandedMode(LipstickScene *scene, InputRouter *router, QObject *parent)
    : QObject(parent)
    , m_scene(scene)
    , m_router(router)
{
    m_holdTimer.setSingleShot(true);
    connect(&m_holdTimer, &QTimer::timeout, this, &OneHandedMode::holdTimeout);
    connect(scene, &LipstickScene::frontChanged, this, &OneHandedMode::apply);
    connect(scene, &LipstickScene::layersChanged, this, &OneHandedMode::updateLayers);
    connect(scene, &LipstickScene::keyboardChanged, this, &OneHandedMode::keyboardChanged);
    connect(scene, &LipstickScene::windowAdded, this, &OneHandedMode::windowAdded);
    if (QQuickWindow *window = scene->window())
        m_picture = QRectF(0, 0, window->width(), window->height());
}

OneHandedMode::~OneHandedMode()
{
    m_holdTimer.stop();
    m_active = false;
    m_size = 1;
    m_lowered = false;
    reportActive();
    if (window()) {
        updateMovedWindow(false);
        offsetChanged(true);
    }
    for (MovedItem *moved : { &m_movedWallpaper, &m_movedHome, &m_movedStatusBar, &m_movedTopMenu,
                              &m_movedLauncher, &m_movedDialogs, &m_movedAlarms, &m_movedBanners })
        moved->reset();
    m_homeLayout.restore();
    m_switcherLayout.restore();
    setWallpaperLayer(false);
    delete m_cover;
    delete m_coverSide;
    delete m_emptyArea;
    delete m_homeEmptyArea;
    setInUse(false);
}

QQuickWindow *OneHandedMode::window() const
{
    return m_scene->window();
}

qreal OneHandedMode::mm() const
{
    return Units::pixelsPerMm(window());
}

void OneHandedMode::trace(const QString &message)
{
    FrogfishLog::trace("one-handed", message);
}

void OneHandedMode::configure(const FrogfishSettings::Values &values, bool allowed)
{
    const bool modeChanged = values.mode != m_config.mode;
    if (modeChanged) {
        m_active = false;
        m_size = 1;
    }
    m_config = values;
    m_allowed = allowed;
    apply();
}

void OneHandedMode::setInUse(bool inUse)
{
    if (inUse == m_inUse)
        return;
    m_inUse = inUse;
    emit inUseChanged(inUse);
}

void OneHandedMode::apply()
{
    QQuickWindow *window = this->window();
    if (!window)
        return;

    m_enabled = m_allowed && m_config.mode != FrogfishSettings::None;
    if (!m_enabled) {
        m_active = false;
        m_size = 1;
    }
    // Portrait only: lipstick turns its UI for the window in front, the
    // changes here are in the screen's coordinates
    const bool portrait = m_scene->portrait();
    const bool lowered = m_active && portrait;
    // In use before the first change, until all is put back
    if (lowered)
        setInUse(true);
    m_lowered = lowered;
    updateMovedWindow(lowered);
    offsetChanged(true);
    if (!lowered)
        setInUse(false);

    const QString applied = lowered
            ? (pullMode() ? QStringLiteral("on, pulled")
               : shrinkMode() ? QStringLiteral("on, shrunk") : QStringLiteral("on, pulled and shrunk"))
            : !m_enabled ? QStringLiteral("off")
            : !portrait ? QStringLiteral("off in landscape")
            : QStringLiteral("off, waiting for the corner or the edge");
    if (applied != m_appliedMode) {
        m_appliedMode = applied;
        trace(QStringLiteral("mode: ") + applied);
    }
    reportActive();
}

void OneHandedMode::reportActive()
{
    if (m_active == m_reportedActive)
        return;
    m_reportedActive = m_active;
    emit activeChanged(m_active);
}

void OneHandedMode::setActive(bool active)
{
    if (active == m_active)
        return;
    if (active && (!m_enabled || !m_scene->portrait()
                   || !OneHandedPolicy::triggerAllowed(policyScene(m_scene), state()))) {
        trace(QStringLiteral("top menu: not here"));
        return;
    }
    toggle(QStringLiteral("top menu"));
}

void OneHandedMode::toggle(const QString &by)
{
    if (m_active) {
        switchOff(by + QStringLiteral(": one-handed off"));
        return;
    }
    m_active = true;
    m_size = m_config.size;
    trace(by + QStringLiteral(": one-handed on"));
    QTimer::singleShot(0, this, &OneHandedMode::apply);
}

QString OneHandedMode::diagnostics() const
{
    QString state = m_appliedMode.isEmpty() ? QStringLiteral("off") : m_appliedMode;
    // The size changes with the edge, after the mode was applied
    if (m_lowered)
        state += QStringLiteral(" to %1 %").arg(qRound(m_size * 100));
    return QStringLiteral("one-handed: %1, in front %2")
            .arg(state, m_frontName.isEmpty() ? QStringLiteral("unknown") : m_frontName);
}

OneHandedPolicy::Scene OneHandedMode::policyScene(const LipstickScene *lipstick)
{
    OneHandedPolicy::Scene scene;
    if (!lipstick->window())
        return scene;
    const auto layer = [lipstick](const char *name, const char *property) {
        QObject *item = lipstick->object(name);
        return item && item->property(property).toBool();
    };
    scene.lockScreen = layer("lockScreenLayer", "exposed");
    scene.deviceLocked = lipstick->deviceLocked();
    scene.alarm = layer("alarmLayer", "exposed");
    scene.alarmPanel = layer("alarmLayer", "renderDialogBackground");
    scene.dialog = layer("dialogLayer", "exposed");
    scene.dialogPanel = layer("dialogLayer", "renderDialogBackground");
    scene.launcher = layer("launcherLayer", "exposed");
    scene.topMenu = layer("topMenuLayer", "exposed");
    scene.homeActive = lipstick->window()->property("homeActive").toBool();
    scene.application = lipstick->topmostApplication();
    return scene;
}

OneHandedPolicy::State OneHandedMode::state() const
{
    OneHandedPolicy::State state;
    QObject *application = m_scene->topmostApplication();
    state.lowered = m_lowered;
    state.keyboard = m_scene->keyboardOpen();
    state.moved = application && application == m_movedWindow;
    state.launcherLowered = m_launcherOffset > 0;
    state.homeLowered = m_homeTop > 0;
    state.edgeSwitchesOn = edgeSwitchesOn();
    return state;
}

qreal OneHandedMode::pictureTop(OneHandedPolicy::Placement placement) const
{
    switch (placement) {
    case OneHandedPolicy::Placement::Lowered:
        return sharedOffset();
    case OneHandedPolicy::Placement::AboveKeyboard:
        return std::round(window()->height() * (1 - shrinkFactor()));
    case OneHandedPolicy::Placement::FullScreen:
        break;
    }
    return 0;
}

qreal OneHandedMode::sharedOffset() const
{
    return m_lowered ? std::round(window()->height() * (1 - m_size)) : 0;
}

qreal OneHandedMode::shrinkFactor() const
{
    if (!m_lowered)
        return 1;
    if (shrinkMode())
        return m_size;
    // Pull and shrink: as much of the app shown as its scale
    return pullShrinkMode() ? std::sqrt(m_size) : 1;
}

qreal OneHandedMode::applicationOffset() const
{
    // The keyboard: the app's text field is above it in the app's own
    // coordinates, which means in the hidden part
    if (m_scene->keyboardOpen())
        return 0;
    return sharedOffset();
}

void OneHandedMode::updateMovedWindow(bool lowered)
{
    if (!lowered) {
        restoreWindow();
        return;
    }
    // While lipstick's own screens are in front (the top menu, home), the
    // application keeps its place: nothing to do at every switch
    QObject *topmost = m_scene->topmostApplication();
    if (!topmost)
        return;
    // Moved, not resized: Silica apps size their content from the screen
    // (the same top part, with a redraw), Android apps get squashed
    if (topmost != m_movedWindow) {
        restoreWindow();
        trace(QStringLiteral("moved \"%1\"").arg(topmost->property("title").toString()));
        m_movedWindow = topmost;
    }
    placeApplication();
}

void OneHandedMode::restoreWindow()
{
    delete m_emptyArea;
    m_application.reset();
    m_movedWindow = nullptr;
    // Also when the moved application was closed meanwhile
    scaleKeyboard(1);
}

void OneHandedMode::placeApplication()
{
    QObject *topmost = m_scene->topmostApplication();
    QQuickWindow *window = this->window();
    if (!window || !topmost || topmost != m_movedWindow)
        return;
    QQuickItem *item = qobject_cast<QQuickItem *>(topmost);
    const qreal offset = applicationOffset();
    const qreal factor = shrinkFactor();
    const bool keyboard = m_scene->keyboardOpen();
    const bool left = m_config.leftHanded;
    // The window's item; no QML positions it inside its wrapper. Shrunk from
    // its top corner on the hand's side; with the keyboard, from the bottom
    // corner, like the keyboard itself: the app places its text field above
    // the keyboard's height in its own coordinates, which then lands exactly
    // on the shrunk keyboard
    if (item) {
        m_application.setY(item, offset);
        m_application.setScale(item, factor,
                               keyboard ? (left ? QQuickItem::BottomLeft : QQuickItem::BottomRight)
                                        : (left ? QQuickItem::TopLeft : QQuickItem::TopRight));
    }
    // The keyboard has the shrink whether open or not: back to full size
    // when it closes, it showed big on its way out; shrunk only once it
    // opened, big on its way in. It's put back with the window.
    scaleKeyboard(factor);
    QQuickItem *wrapper = item ? item->parentItem() : nullptr;
    if (offset > 0 && wrapper) {
        if (!m_emptyArea || m_emptyArea->parentItem() != wrapper) {
            delete m_emptyArea;
            m_emptyArea = new EmptyArea(wrapper, QStringLiteral("frogfish-onehanded-empty"));
        }
        m_emptyArea->setPosition(QPointF(0, 0));
        m_emptyArea->setSize(QSizeF(window->width(), offset));
    } else {
        delete m_emptyArea;
    }
    updateCover();
}

void OneHandedMode::keyboardChanged()
{
    placeApplication();
    updateLayers();
}

void OneHandedMode::scaleKeyboard(qreal factor)
{
    // Looked for once (a walk through the scene); one created later comes
    // through windowAdded()
    if (factor != 1 && m_keyboards.isEmpty() && !m_keyboardsSearched) {
        m_keyboardsSearched = true;
        for (QQuickItem *keyboard : m_scene->keyboards())
            m_keyboards << PlacedItem(keyboard);
        trace(QStringLiteral("keyboard windows: %1").arg(m_keyboards.count()));
    }
    if (factor == 1) {
        for (PlacedItem &keyboard : m_keyboards)
            keyboard.reset();
        m_keyboards.clear();
        m_keyboardsSearched = false;
        return;
    }
    const QQuickItem::TransformOrigin origin = m_config.leftHanded ? QQuickItem::BottomLeft
                                                                   : QQuickItem::BottomRight;
    for (PlacedItem &keyboard : m_keyboards) {
        if (keyboard.item())
            keyboard.setScale(keyboard.item(), factor, origin);
    }
}

void OneHandedMode::windowAdded(QObject *window)
{
    // A new keyboard window gets the shrink before its first frame shows
    QQuickItem *item = qobject_cast<QQuickItem *>(window);
    if (!item || !LipstickScene::isKeyboard(item))
        return;
    const qreal factor = shrinkFactor();
    if (factor == 1 || !OneHandedPolicy::keyboardShrunk(policyScene(m_scene), state()))
        return;
    for (const PlacedItem &known : m_keyboards) {
        if (known.item() == item)
            return;
    }
    m_keyboards << PlacedItem(item);
    trace(QStringLiteral("keyboard window added"));
    scaleKeyboard(factor);
}

void OneHandedMode::offsetChanged(bool final)
{
    QQuickWindow *window = this->window();
    if (!window)
        return;
    const qreal offset = sharedOffset();
    const qreal factor = shrinkFactor();
    const bool left = m_config.leftHanded;
    if (final)
        trace(QStringLiteral("offset %1 px, scale %2").arg(qRound(offset)).arg(factor, 0, 'f', 3));
    placeApplication();
    m_topMenuOffset = offset;
    updateTopMenu();
    updateLauncher(offset, final);
    // The wallpaper moves down whole: cut at the bottom, like the apps
    m_movedWallpaper.set(m_scene->item("wallpaper"), factor, std::round(offset), left);
    setWallpaperLayer(offset > 0);
    QQuickItem *home = m_scene->item("homeLayer");
    const qreal homeFactor = offset > 0 ? factor : 1;
    if (final) {
        // Laying home out again (the cards) only when the finger lifts
        setHomeTop(offset, homeFactor);
        m_movedHome.set(home, homeFactor, 0, left);
        refreshBlurSources();
    } else {
        m_movedHome.set(home, homeFactor, offset - m_homeTop, left);
    }
    updateDialogLayers();
    updateNotificationLayer();
    updateCover();
}

void OneHandedMode::setWallpaperLayer(bool on)
{
    if (on == m_wallpaperLayerOn)
        return;
    m_wallpaperLayerOn = on;
    if (m_scene->useWallpaperLayer(on))
        trace(QStringLiteral("wallpaper layer %1").arg(on ? "on" : "off"));
}

void OneHandedMode::refreshBlurSources()
{
    // The blurred backgrounds (app grid, top menu, dialogs) are snapshots of
    // the wallpaper and home (BlurSource.qml, not live by default) that
    // lipstick refreshes only on rotation and similar: taken again so they
    // match the moved wallpaper, when it moved
    const qreal offset = sharedOffset();
    const qreal factor = shrinkFactor();
    if (qAbs(offset - m_blurOffset) < 0.5 && qAbs(factor - m_blurFactor) < 0.001)
        return;
    m_blurOffset = offset;
    m_blurFactor = factor;
    for (QQuickItem *source : m_scene->blurSources())
        QMetaObject::invokeMethod(source, "update");
}

void OneHandedMode::setHomeTop(qreal top, qreal factor)
{
    // Home (the open apps' cards, Events) laid out below the application's
    // top: its Pannable fills the layers, so a top margin is enough; shrunk,
    // it is laid out taller (a negative bottom margin) so that it still ends
    // at the bottom of the screen
    QQuickItem *home = m_scene->item("homeLayer");
    // Unchanged: the cards are not laid out again at every switch
    if (!home || (qAbs(top - m_homeTop) < 0.5 && qAbs(factor - m_homeFactor) < 0.001))
        return;
    QObject *anchors = home->property("anchors").value<QObject *>();
    if (!anchors)
        return;
    m_homeTop = top;
    m_homeFactor = factor;
    const qreal screen = window()->height();
    const qreal height = (screen - top) / factor;
    QList<QQuickItem *> cardGrids;
    if (QQuickItem *switcher = m_scene->switcher()) {
        for (QQuickItem *grid : switcher->childItems()) {
            if (grid->property("largeRows").isValid())
                cardGrids << grid;
        }
    }

    if (top <= 0) {
        m_homeLayout.restore();
        m_switcherLayout.restore();
        m_movedStatusBar.reset();
        for (QQuickItem *grid : cardGrids)
            QMetaObject::invokeMethod(grid, "updateColumns");
        delete m_homeEmptyArea;
        return;
    }

    m_homeLayout.set(anchors, "topMargin", top);
    m_homeLayout.set(anchors, "bottomMargin", (screen - top) - height);
    // The status bar sits in an item of the full screen's size centred on
    // home, so half of the difference is missing
    m_movedStatusBar.set(home->property("statusBar").value<QQuickItem *>(), 1, (screen - height) / 2,
                         m_config.leftHanded);
    // The cards: the grid counts its rows of large cards from the screen's
    // height (home is not a Silica page), so in the shorter part it switches
    // to small cards only after one app too many
    for (QQuickItem *grid : cardGrids) {
        m_switcherLayout.set(grid, "_pageHeight", height);
        QMetaObject::invokeMethod(grid, "updateColumns");
    }
    // Events is a full-screen window of its own: its list ends where the
    // screen does
    if (QQuickItem *events = m_scene->eventsView())
        m_homeLayout.set(events->property("anchors").value<QObject *>(), "bottomMargin", screen - height);
    if (!m_homeEmptyArea)
        m_homeEmptyArea = new EmptyArea(home, QStringLiteral("frogfish-onehanded-empty-home"));
    m_homeEmptyArea->setPosition(QPointF(0, -top / factor));
    m_homeEmptyArea->setSize(QSizeF(window()->width(), top / factor));
}

void OneHandedMode::updateLauncher(qreal offset, bool final)
{
    // The app grid, like the top menu: moved down and its pages (a
    // SilicaListView in its own full-screen window) shortened to the space
    // below. Setting the height ends the pager's binding until the next
    // compositor start.
    const qreal factor = offset > 0 ? shrinkFactor() : 1;
    m_movedLauncher.set(m_scene->item("launcherLayer"), factor, offset, m_config.leftHanded);
    // During a drag only the position follows: a new height and row count
    // at every movement lays the icon pages out again and makes them flash
    if (!final)
        return;
    QQuickItem *pager = offset > 0 || m_launcherPager ? m_scene->launcherPager() : nullptr;
    if (!pager)
        return;
    if (pager != m_launcherPager) {
        m_launcherPager = pager;
        if (pager->parentItem())
            connect(pager->parentItem(), &QQuickItem::heightChanged, this, [this]() {
                updateLauncher(m_launcherOffset);
            });
        // A folder opens its own grid, created then
        const int folder = pager->metaObject()->indexOfProperty("openedChildFolder");
        const QMetaProperty property = pager->metaObject()->property(folder);
        if (property.hasNotifySignal())
            connect(pager, property.notifySignal(),
                    this, metaObject()->method(metaObject()->indexOfSlot("folderChanged()")));
    }
    // Once the rows were set here, they follow every change (rotation too)
    if (offset == 0 && m_launcherOffset == 0 && m_gridRows.isEmpty())
        return;
    m_launcherOffset = offset;
    const qreal page = pager->parentItem() ? pager->parentItem()->height() : window()->height();
    pager->setHeight(std::ceil((page - offset) / factor));
    updateGridRows();
}

void OneHandedMode::folderChanged()
{
    // The folder's grid gets its size after it is created
    QTimer::singleShot(0, this, &OneHandedMode::updateGridRows);
    QTimer::singleShot(300, this, &OneHandedMode::updateGridRows);
}

void OneHandedMode::updateGridRows()
{
    // Silica's icon grids keep at least 6 rows in portrait
    // (IconGridViewBase.qml: max(6, pageHeight / minimumCellHeight)), so in
    // the shorter page the icons overlap: as many rows as fit (ends the
    // binding until the next start). A folder lies over the app grid with the
    // same cells and shows rows - 2 of them (LauncherFolder.qml), so it gets
    // the app grid's count. Grids set once follow Silica's rule again when not
    // lowered.
    const QList<QQuickItem *> grids = m_scene->iconGrids();
    QQuickItem *root = nullptr;
    for (QQuickItem *grid : grids) {
        if (grid->property("rootFolder").toBool()) {
            root = grid;
            break;
        }
    }
    if (!root)
        return;
    const qreal pageHeight = root->property("pageHeight").toReal();
    const qreal minimum = root->property("minimumCellHeight").toReal();
    if (pageHeight <= 0 || minimum <= 0)
        return;
    const bool lowered = m_launcherOffset > 0;
    const int fit = int(pageHeight / minimum);
    const int rows = lowered ? qMax(3, fit) : defaultGridRows(root, fit);

    for (QQuickItem *grid : grids) {
        bool known = false;
        for (const QPointer<QQuickItem> &set : m_gridRows)
            known = known || set == grid;
        if (!lowered && !known)
            continue;
        if (!known)
            m_gridRows << grid;
        if (grid->property("rows").toInt() != rows) {
            grid->setProperty("rows", rows);
            trace(QStringLiteral("%1 grid: %2 rows").arg(grid == root ? "app" : "folder").arg(rows));
        }
        // The folder shows rows - 2: the header, and a band kept free at the
        // bottom for edit mode's drop area. Lowered, that band costs a whole
        // row out of about four: icons there too
        for (QQuickItem *folder = grid->parentItem(); folder && grid != root; folder = folder->parentItem()) {
            if (!folder->property("visibleRowCount").isValid())
                continue;
            const int visible = lowered ? rows - 1 : rows - 2;
            if (folder->property("visibleRowCount").toInt() != visible) {
                folder->setProperty("visibleRowCount", visible);
                trace(QStringLiteral("folder: %1 rows visible").arg(visible));
            }
            break;
        }
    }
}

int OneHandedMode::defaultGridRows(QQuickItem *grid, int fit) const
{
    // Silica's own rule (IconGridViewBase.qml): the configured rows, or at
    // least 6 in portrait and 3 in landscape
    const QVariant isPortrait = grid->property("isPortrait");
    const bool upright = isPortrait.isValid() ? isPortrait.toBool() : m_scene->portrait();
    const int configured = m_scene->launcherRows(upright);
    return configured > 0 ? configured : qMax(upright ? 6 : 3, fit);
}

void OneHandedMode::updateTopMenu()
{
    QQuickItem *layer = m_scene->item("topMenuLayer");
    if (!layer || (m_topMenuOffset == 0 && !m_movedTopMenu.used() && !m_topMenuSized))
        return;
    const qreal factor = m_topMenuOffset > 0 ? shrinkFactor() : 1;
    m_movedTopMenu.set(layer, factor, m_topMenuOffset, m_config.leftHanded);

    // The menu (a SilicaFlickable) gets the height below the application's
    // top, so what doesn't fit scrolls. Its own binding is
    // min(implicitHeight, page height); setting it ends that binding until
    // the next compositor start, so from then on it follows here.
    QQuickItem *menu = layer->property("topMenu").value<QQuickItem *>();
    if (!menu)
        return;
    if (menu != m_topMenu) {
        m_topMenu = menu;
        connect(menu, &QQuickItem::implicitHeightChanged, this, &OneHandedMode::updateTopMenu);
        if (menu->parentItem())
            connect(menu->parentItem(), &QQuickItem::heightChanged, this, &OneHandedMode::updateTopMenu);
    }
    const qreal page = menu->parentItem() ? menu->parentItem()->height() : window()->height();
    menu->setHeight(qMax(qreal(0), qMin(menu->implicitHeight(), (page - m_topMenuOffset) / factor)));
    m_topMenuSized = true;
}

void OneHandedMode::updateLayers()
{
    updateDialogLayers();
    updateNotificationLayer();
    updateCover();
}

void OneHandedMode::updateDialogLayers()
{
    // The whole layer, with lipstick's dimming and the panel's background
    // drawn behind the window: moving only the window leaves them behind
    const qreal offset = sharedOffset();
    const qreal factor = shrinkFactor();
    const bool left = m_config.leftHanded;
    QQuickItem *front = OneHandedPolicy::dialogFollows(policyScene(m_scene), state())
            ? m_scene->frontDialogLayer() : nullptr;
    QQuickItem *dialogs = m_scene->item("dialogLayer");
    m_movedDialogs.set(dialogs, front == dialogs ? factor : 1, front == dialogs ? offset : 0, left);
    QQuickItem *alarms = m_scene->item("alarmLayer");
    m_movedAlarms.set(alarms, front == alarms ? factor : 1, front == alarms ? offset : 0, left);
}

void OneHandedMode::updateNotificationLayer()
{
    // Not looked for until one-handed mode is first used
    if (OneHandedPolicy::bannersFollow(policyScene(m_scene), state()))
        m_movedBanners.set(m_scene->notificationLayer(), shrinkFactor(), sharedOffset(), m_config.leftHanded);
    else
        m_movedBanners.reset();
}

void OneHandedMode::updateKeyboard()
{
    // Shrunk only for a lowered application in front (placeApplication(),
    // whether open or not); anywhere else back to full size, once closed so
    // that it doesn't jump on its way out
    if (!m_scene->keyboardOpen() && !OneHandedPolicy::keyboardShrunk(policyScene(m_scene), state()))
        scaleKeyboard(1);
}

void OneHandedMode::updateCover()
{
    QQuickWindow *window = this->window();
    if (!window)
        return;
    updateKeyboard();
    const OneHandedPolicy::Scene scene = policyScene(m_scene);
    const OneHandedPolicy::State state = this->state();
    const QString front = QLatin1String(OneHandedPolicy::name(OneHandedPolicy::front(scene)));
    if (front != m_frontName) {
        m_frontName = front;
        trace(QStringLiteral("in front: ") + front);
    }
    // Black over the empty part: on the OLED panel it looks like the end of
    // the screen, and it hides what lipstick leaves there (home behind
    // native apps, Android's black, a top menu pushed up). What is in front
    // starts at its top, scaled from the hand's side: the far side is empty
    // too.
    const OneHandedPolicy::Placement placement = OneHandedPolicy::placement(scene, state);
    const bool fullScreen = placement == OneHandedPolicy::Placement::FullScreen;
    const qreal top = pictureTop(placement);
    const qreal factor = shrinkFactor();
    const qreal width = window->width();
    const qreal height = window->height();
    const qreal side = !fullScreen && (top > 0 || factor < 1) ? width * (1 - factor) : 0;
    const bool covered = !fullScreen && (top > 0 || side > 0);
    m_picture = covered ? QRectF(m_config.leftHanded ? 0 : side, top, width - side, height - top)
                        : QRectF(0, 0, width, height);
    if (!covered) {
        if (m_cover)
            m_cover->setVisible(false);
        if (m_coverSide)
            m_coverSide->setVisible(false);
        return;
    }
    QQuickItem *parent = m_scene->coverParent();
    if (!parent)
        parent = window->contentItem();
    QQmlEngine *engine = qmlEngine(window);
    for (QPointer<QQuickItem> *cover : { &m_cover, &m_coverSide }) {
        if (*cover)
            continue;
        QQuickItem *item = Qml::createItem(engine, "Cover.qml");
        if (!item)
            item = new QQuickItem;
        item->setObjectName(cover == &m_cover ? QStringLiteral("frogfish-onehanded-cover")
                                              : QStringLiteral("frogfish-onehanded-cover-side"));
        item->setParent(this);
        item->setParentItem(parent);
        item->setZ(CoverZ);
        *cover = item;
    }
    m_cover->setPosition(QPointF(0, 0));
    m_cover->setSize(QSizeF(width, top));
    m_cover->setVisible(top > 0);
    m_coverSide->setPosition(QPointF(m_config.leftHanded ? width - side : 0, top));
    m_coverSide->setSize(QSizeF(side, height - top));
    m_coverSide->setVisible(side > 0);
}

bool OneHandedMode::edgeSwitchesOn() const
{
    return m_enabled && !m_active;
}

bool OneHandedMode::inTriggerCorner(const QPointF &point) const
{
    if (!m_enabled || !m_scene->portrait())
        return false;
    // Where first: lipstick is read only for touches in the corner
    QQuickWindow *window = this->window();
    const qreal square = HoldGesture::CornerMm * mm();
    return point.y() >= window->height() - square
            && (m_config.leftHanded ? point.x() < square : point.x() >= window->width() - square)
            && OneHandedPolicy::triggerAllowed(policyScene(m_scene), state());
}

bool OneHandedMode::inEdgeStrip(const QPointF &point) const
{
    // While typing the pull waits (the app is at the top): the edge is the
    // app's
    if (!m_enabled || !m_scene->portrait() || (pullMode() && m_scene->keyboardOpen()))
        return false;
    // Where first: lipstick is read only for touches on the edge
    QQuickWindow *window = this->window();
    const qreal strip = HoldGesture::StripMm * mm();
    if (m_config.leftHanded ? point.x() >= strip : point.x() < window->width() - strip)
        return false;
    switch (OneHandedPolicy::edge(policyScene(m_scene), state())) {
    case OneHandedPolicy::Edge::AboveKeyboard: {
        const qreal keyboardTop = window->height() - m_scene->keyboardHeight() * shrinkFactor();
        return point.y() < keyboardTop - HoldGesture::SlopMm * mm();
    }
    case OneHandedPolicy::Edge::Side:
        // Clear of the corner, where the thumb rests
        return point.y() >= sharedOffset() && point.y() < window->height() - HoldGesture::CornerMm * mm();
    case OneHandedPolicy::Edge::None:
        break;
    }
    return false;
}

bool OneHandedMode::inEmptySide(const QPointF &point, qreal top) const
{
    const qreal width = window()->width();
    const qreal side = width * (1 - shrinkFactor());
    if (side <= 0 || point.y() < top)
        return false;
    return m_config.leftHanded ? point.x() >= width - side : point.x() < side;
}

bool OneHandedMode::onBanner(const QPointF &point)
{
    // Hidden by its opacity between notifications
    QQuickItem *banner = m_scene->bannerArea();
    return banner && banner->isVisible() && banner->opacity() > 0
            && banner->contains(banner->mapFromScene(point));
}

qreal OneHandedMode::emptyAreaTop() const
{
    const OneHandedPolicy::Scene scene = policyScene(m_scene);
    const OneHandedPolicy::State state = this->state();
    if (!OneHandedPolicy::ownsEmptyArea(scene, state))
        return -1;
    // With the keyboard, the application is shrunk from the bottom corner:
    // its top is inside the screen, where lipstick's close swipe (from the
    // window's own top) can't start, so the empty area above it is ours
    const qreal top = pictureTop(OneHandedPolicy::placement(scene, state));
    return top > 0 ? top : -1;
}

bool OneHandedMode::touch(QTouchEvent *event)
{
    if (!window())
        return false;
    if (holdBack(event))
        return true;
    return emptyAreaTouch(event);
}

bool OneHandedMode::holdBack(QTouchEvent *event)
{
    const QList<QTouchEvent::TouchPoint> &points = event->touchPoints();
    if (event->type() == QEvent::TouchBegin) {
        endHold();
        if (points.count() != 1)
            return false;
        if (inTriggerCorner(points.first().pos()))
            m_holdOnEdge = false;
        else if (inEdgeStrip(points.first().pos()))
            m_holdOnEdge = true;
        else
            return false;
        m_hold = HoldWaiting;
        m_holdId = points.first().id();
        m_holdStart = points.first().pos();
        m_held.hold(event);
        m_holdTimer.start(HoldGesture::ArmMs);
        return true;
    }
    if (m_hold == HoldIdle)
        return false;
    const bool end = event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel;

    if (m_hold == HoldDragging) {
        for (const QTouchEvent::TouchPoint &point : points) {
            if (point.id() == m_holdId)
                dragSize(point.pos().y());
        }
        if (end)
            snapToFullSize();
        offsetChanged(end);
        if (end) {
            endHold();
            switchOffAtFullSize();
        }
        return true;
    }

    // The rest of a touch that was ours: nobody saw it begin
    if (m_hold == HoldDropping) {
        if (end)
            endHold();
        return true;
    }
    if (event->type() == QEvent::TouchCancel) {
        endHold();
        return true;
    }

    qreal moved = 0;
    QPointF delta;
    for (const QTouchEvent::TouchPoint &point : points) {
        if (point.id() == m_holdId) {
            delta = point.pos() - m_holdStart;
            moved = QLineF(m_holdStart, point.pos()).length();
        }
    }
    const bool anotherFinger = points.count() > 1 || (event->touchPointStates() & Qt::TouchPointPressed);
    if (m_holdOnEdge && m_hold == HoldArmed) {
        if (anotherFinger) {
            m_hold = HoldDropping;
        } else if (moved > HoldGesture::SlopMm * mm()) {
            if (edgeSwitchesOn()) {
                if (delta.y() <= 0) {
                    // Already full size: nothing to enlarge
                    m_hold = HoldDropping;
                    return true;
                }
                // Switched on from full size: the size follows the finger
                m_active = true;
                m_size = 1;
                trace(QStringLiteral("edge hold and drag: one-handed on"));
                apply();
            }
            m_hold = HoldDragging;
            m_dragStartSize = m_size;
            trace(QStringLiteral("edge hold and drag: size from %1 %").arg(qRound(m_size * 100)));
        } else if (event->type() == QEvent::TouchEnd) {
            endHold();
        }
        return true;
    }
    if (anotherFinger || moved > HoldGesture::SlopMm * mm()) {
        if (m_hold == HoldWaiting) {
            // A swipe or a second finger: given back, this event follows
            m_held.replay(m_router);
            endHold();
            return false;
        }
        // Too late to give back
        trace(QStringLiteral("corner: moved while held, dropped"));
        m_hold = HoldDropping;
        return true;
    }

    if (event->type() == QEvent::TouchEnd) {
        if (m_hold == HoldWaiting) {
            // A tap: given back, the release follows
            m_held.replay(m_router);
            endHold();
            return false;
        }
        if (m_hold == HoldArmed)
            toggle(QStringLiteral("corner"));
        endHold();
        return true;
    }

    if (m_hold == HoldWaiting)
        m_held.hold(event);
    return true;
}

void OneHandedMode::holdTimeout()
{
    if (m_hold == HoldWaiting) {
        // Ours now: what was held is never given back
        m_held.clear();
        m_hold = HoldArmed;
        trace(QStringLiteral("held: %1, keyboard %2")
              .arg(m_holdOnEdge ? "edge" : "corner").arg(m_scene->keyboardHeight()));
        if (QQuickWindow *window = this->window())
            m_cue.show(window->contentItem(), m_holdStart, mm(), m_holdId);
        Haptics::press();
        // The corner has to be lifted in time; on the edge a drag follows
        if (!m_holdOnEdge)
            m_holdTimer.start(HoldGesture::MaximumMs - HoldGesture::ArmMs);
    } else if (m_hold == HoldArmed) {
        trace(QStringLiteral("corner: held too long, dropped"));
        m_hold = HoldDropping;
    }
}

void OneHandedMode::endHold()
{
    m_holdTimer.stop();
    m_held.clear();
    m_hold = HoldIdle;
}

void OneHandedMode::dragSize(qreal y)
{
    // Pulled, the point moves with the finger; shrunk, its distance from the
    // bottom corner everything shrinks into scales with the size
    const qreal height = window()->height();
    qreal size = m_dragStartSize - (y - m_holdStart.y()) / height;
    const qreal from = height - m_holdStart.y();
    if (shrinkMode()) {
        if (from <= 0)
            return;
        size = m_dragStartSize * (height - y) / from;
    } else if (pullShrinkMode() && m_scene->keyboardOpen()) {
        // From the bottom corner by the scale, sqrt(size)
        if (from <= 0)
            return;
        const qreal scale = std::sqrt(m_dragStartSize) * (height - y) / from;
        size = scale * scale;
    } else if (pullShrinkMode()) {
        // The app's point a under the finger is drawn at H (1 - s) + a sqrt(s):
        // for the finger at y, sqrt(s) is the root of H u^2 - a u + (y - H) = 0
        // that is positive (a single one: y <= H). Down makes it smaller.
        const qreal startScale = std::sqrt(m_dragStartSize);
        const qreal a = (m_holdStart.y() - height * (1 - m_dragStartSize)) / startScale;
        const qreal u = (a + std::sqrt(a * a + 4 * height * qMax(qreal(0), height - y))) / (2 * height);
        size = u * u;
    }
    m_size = qBound(SmallestSize, size, qreal(1));
}

void OneHandedMode::snapToFullSize()
{
    const qreal lost = window()->height() * (1 - m_size);
    if (lost > 0 && lost < SnapToFullSizeMm * mm()) {
        trace(QStringLiteral("snapped to full size from %1 %").arg(qRound(m_size * 100)));
        m_size = 1;
    }
}

void OneHandedMode::switchOffAtFullSize()
{
    if (m_size >= 1 && m_active)
        switchOff(QStringLiteral("dragged to full size: one-handed off"));
}

void OneHandedMode::switchOff(const QString &why)
{
    m_active = false;
    m_size = 1;
    trace(why);
    QTimer::singleShot(0, this, &OneHandedMode::apply);
}

bool OneHandedMode::emptyAreaTouch(QTouchEvent *event)
{
    if (event->type() == QEvent::TouchBegin && m_lowered && !event->touchPoints().isEmpty()) {
        // The empty part above what is in front: the system's swipes from the
        // top edge (top menu, close) start where they can be reached, taken
        // whole, since lipstick's own drags would act on the rest of the
        // touch (the top menu closing at once). The side strips keep their
        // own swipes.
        const qreal top = emptyAreaTop();
        const QPointF start = event->touchPoints().first().pos();
        QString decision;
        if (top <= 0) {
            decision = QStringLiteral("nothing lowered in front");
        } else if (start.y() >= top && !inEmptySide(start, top)) {
            decision = QString();
        } else if (onBanner(start)) {
            decision = QStringLiteral("notification banner, left alone");
        } else if (start.x() < m_scene->sideBand() || start.x() >= window()->width() - m_scene->sideBand()) {
            decision = QStringLiteral("side strip, left alone");
        } else {
            m_directStart = start;
            m_directActive = true;
            m_directMoved = false;
            m_swallowing = true;
            decision = QStringLiteral("watched for a swipe down");
        }
        if (!decision.isEmpty())
            trace(QStringLiteral("touch at %1,%2 (top %3): %4")
                  .arg(qRound(start.x())).arg(qRound(start.y())).arg(qRound(top)).arg(decision));
    }
    if (!m_swallowing)
        return false;
    const bool end = event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel;
    if (m_directActive || end)
        directGesture(event);
    if (end)
        m_swallowing = false;
    return true;
}

void OneHandedMode::directGesture(QTouchEvent *event)
{
    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        const bool tap = m_directActive && !m_directMoved && event->type() == QEvent::TouchEnd;
        m_directActive = false;
        // A tap on the empty area leaves one-handed mode; above a lowered
        // dialog it dismisses the dialog, like a tap outside its panel
        if (!tap)
            return;
        if (OneHandedPolicy::emptyAreaTap(policyScene(m_scene)) == OneHandedPolicy::Tap::DismissDialog)
            trace(dismissDialog(event->device())
                  ? QStringLiteral("tap above the dialog: given to the dialog")
                  : QStringLiteral("tap above the dialog: no place to give it"));
        else if (m_active)
            switchOff(QStringLiteral("tap on the empty area: one-handed off"));
        return;
    }
    if (!m_directActive || event->touchPoints().isEmpty())
        return;
    const QPointF delta = event->touchPoints().first().pos() - m_directStart;
    const qreal perMm = mm();
    if (QLineF(QPointF(), delta).length() > HoldGesture::SlopMm * perMm)
        m_directMoved = true;
    if (delta.y() < SwipeMm * perMm || delta.y() < 2 * qAbs(delta.x()))
        return;
    m_directActive = false;

    const qreal corner = SwipeMm * perMm;
    if (m_directStart.x() < corner || m_directStart.x() >= window()->width() - corner) {
        // Lipstick's own close from the top corners: the layer closes with
        // _closeDismissedWindow set, the card shows its X, and at the end the
        // layer emits closeWindow(), on which the card destroys the surface
        QObject *appLayer = m_scene->object("appLayer");
        const bool closed = appLayer && m_scene->topmostApplication()
                && appLayer->setProperty("_closeDismissedWindow", true)
                && QMetaObject::invokeMethod(appLayer, "close");
        trace(closed ? QStringLiteral("swipe down near a side: application closed")
                     : QStringLiteral("swipe down near a side: nothing to close"));
        return;
    }
    QObject *layer = m_scene->object("topMenuLayer");
    const bool shown = layer && QMetaObject::invokeMethod(layer, "show");
    trace(shown ? QStringLiteral("swipe down: top menu shown")
                : QStringLiteral("swipe down: no top menu to show"));
}

bool OneHandedMode::dismissDialog(QTouchDevice *device)
{
    QQuickItem *layer = OneHandedPolicy::dialogFollows(policyScene(m_scene), state())
            ? m_scene->frontDialogLayer() : nullptr;
    QQuickWindow *window = this->window();
    if (!layer || !device || !window)
        return false;
    // Below the panel (the dialog's background, at the window's top), inside
    // the window: there a tap dismisses the dialog
    const QRectF panel = layer->property("backgroundRect").toRectF();
    const QPointF point = layer->mapToScene(QPointF(layer->width() / 2, panel.bottom() + 5 * mm()));
    if (panel.isEmpty() || point.y() >= window->height() - mm())
        return false;
    QTouchEvent::TouchPoint touch(0);
    touch.setPos(point);
    touch.setScenePos(point);
    touch.setScreenPos(window->mapToGlobal(point.toPoint()));
    touch.setStartPos(point);
    touch.setStartScenePos(point);
    touch.setStartScreenPos(touch.screenPos());
    touch.setPressure(1);
    for (const QEvent::Type type : { QEvent::TouchBegin, QEvent::TouchEnd }) {
        const Qt::TouchPointState state = type == QEvent::TouchBegin ? Qt::TouchPointPressed
                                                                     : Qt::TouchPointReleased;
        touch.setState(state);
        QTouchEvent event(type, device, Qt::NoModifier, state, { touch });
        event.setTimestamp(QDateTime::currentMSecsSinceEpoch());
        m_router->send(&event);
    }
    return true;
}
