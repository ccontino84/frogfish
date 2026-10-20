// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "back.h"
#include "controller.h"
#include "crashguard.h"
#include "log.h"
#include "onehanded.h"
#include "policy.h"
#include "router.h"
#include "scene.h"
#include "settings.h"
#include "units.h"
#include "zoom.h"

#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlListReference>
#include <QtQml>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

namespace {

// Records where touches arrive after the application-wide filter
class TouchRecorder : public QObject
{
public:
    QList<QPointF> positions;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate
                || event->type() == QEvent::TouchEnd)
            positions << static_cast<QTouchEvent *>(event)->touchPoints().first().pos();
        return false;
    }
};

// The hold's ring while it plays (Cue.qml), and its centre
QQuickItem *playingRing(QQuickWindow *window)
{
    for (QQuickItem *child : window->contentItem()->childItems()) {
        if (child->property("fingerSize").isValid() && child->isVisible())
            return child;
    }
    return nullptr;
}

QPointF centre(QQuickItem *item)
{
    return item->position() + QPointF(item->width() / 2, item->height() / 2);
}

// Counts the right clicks that reach the window (lipstick's back)
class RightClicks : public QObject
{
public:
    int count = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonPress
                && static_cast<QMouseEvent *>(event)->button() == Qt::RightButton)
            ++count;
        return false;
    }
};

// Sailfish.Lipstick's PeekFilter: compositor.qml sets the side strips'
// width on its root through the attached properties (PeekFilterAttached)
class PeekFilterAttached : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal boundaryWidth MEMBER m_width NOTIFY boundaryWidthChanged)

public:
    explicit PeekFilterAttached(QObject *parent) : QObject(parent) {}

signals:
    void boundaryWidthChanged();

private:
    qreal m_width = 0;
};

class PeekFilter : public QObject
{
    Q_OBJECT

public:
    static PeekFilterAttached *qmlAttachedProperties(QObject *object) { return new PeekFilterAttached(object); }
};

// Lipstick's scene, from Compositor.qml
class Compositor
{
public:
    Compositor()
    {
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(FROGFISH_TEST_QML "/Compositor.qml")));
        window = qobject_cast<QQuickWindow *>(component.create());
        if (!window)
            qWarning() << component.errorString();
    }
    ~Compositor() { delete window; }

    QQuickItem *item(const char *name) const { return window->findChild<QQuickItem *>(QLatin1String(name)); }
    // An application in front
    void showApplication() { window->setProperty("topmostWindow", QVariant::fromValue<QObject *>(item("appWrapper"))); }
    void showHome() { window->setProperty("topmostWindow", QVariant()); }
    int transforms(const char *name) { return QQmlListReference(item(name), "transform", &engine).count(); }

    QQmlEngine engine;
    QQuickWindow *window = nullptr;
};

// The scene, the router and the modes on it, as the controller has them
class Modes
{
public:
    explicit Modes(QQuickWindow *window, bool withZoom = false)
        : scene(window)
        , router(window)
        , oneHanded(new OneHandedMode(&scene, &router))
        , back(new BackMode(&scene, &router))
    {
        if (withZoom)
            zoom.reset(new ZoomMode(&scene, &router, oneHanded.data()));
        router.setModes(zoom.data(), back.data(), oneHanded.data());
    }
    ~Modes()
    {
        zoom.reset();
        back.reset();
        oneHanded.reset();
    }

    void configure(const FrogfishSettings::Values &values)
    {
        oneHanded->configure(values, true);
        back->configure(values, true);
        if (zoom)
            zoom->configure(values, true);
    }

    LipstickScene scene;
    InputRouter router;
    QScopedPointer<OneHandedMode> oneHanded;
    QScopedPointer<BackMode> back;
    QScopedPointer<ZoomMode> zoom;
};

FrogfishSettings::Values pullValues()
{
    FrogfishSettings::Values values;
    values.zoom = false;
    return values;
}

FrogfishSettings::Values shrinkValues()
{
    FrogfishSettings::Values values = pullValues();
    values.mode = FrogfishSettings::Shrink;
    return values;
}

// The one-handed corner, on the right
const QPoint Corner(995, 1995);

}

QML_DECLARE_TYPEINFO(PeekFilter, QML_HAS_ATTACHED_PROPERTIES)

class TestGestures : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void policyFront();
    void policyRules();
    void sceneHooks();
    void sceneFront();
    void pull();
    void pullHome();
    void pullEdge();
    void shrink();
    void shrinkHome();
    void pullShrink();
    void topMenuSwitch();
    void putBack();
    void zoom();
    void zoomInPicture();
    void zoomOutSteadily();
    void back();
    void routerOrder();
    void controllerModes();
    void controllerSettingsAfterTouch();
    void controllerInUse();
    void controllerTopMenu();
    void controllerCrashGuard();
    // Last: it emits aboutToQuit
    void controllerDetachOnQuit();

private:
    QTemporaryDir m_cache;
    QTouchDevice *m_device = nullptr;
};

void TestGestures::initTestCase()
{
    QVERIFY(m_cache.isValid());
    qputenv("XDG_CACHE_HOME", m_cache.path().toUtf8());
    FrogfishLog::setFilePath(m_cache.path() + QStringLiteral("/test-log.txt"));
    m_device = QTest::createTouchDevice();
    qmlRegisterType<PeekFilter>("Sailfish.Lipstick", 1, 0, "PeekFilter");
}

void TestGestures::init()
{
    for (const char *key : { "FROGFISH_ONEHANDED_MODE", "FROGFISH_ONEHANDED_SIZE",
                             "FROGFISH_ZOOM", "FROGFISH_ZOOM_MAX",
                             "FROGFISH_BACK", "FROGFISH_HAND" })
        qunsetenv(key);
}

void TestGestures::policyFront()
{
    using namespace OneHandedPolicy;
    // In lipstick's order: what is shown on top wins
    Scene scene;
    QCOMPARE(front(scene), Front::Home);
    scene.topMenu = true;
    QCOMPARE(front(scene), Front::Other);
    scene.homeActive = true;
    QCOMPARE(front(scene), Front::Home);
    // The top menu over an application leaves it topmost
    scene.application = true;
    QCOMPARE(front(scene), Front::Application);
    scene.launcher = true;
    QCOMPARE(front(scene), Front::Launcher);
    // A system dialog has lipstick's dialog background; without it, a
    // full-screen window
    scene.dialog = true;
    QCOMPARE(front(scene), Front::Alert);
    scene.dialogPanel = true;
    QCOMPARE(front(scene), Front::Dialog);
    // The alarm layer is above the dialog layer
    scene.alarm = true;
    QCOMPARE(front(scene), Front::Alert);
    scene.alarmPanel = true;
    QCOMPARE(front(scene), Front::Dialog);
    scene.lockScreen = true;
    QCOMPARE(front(scene), Front::LockScreen);
}

void TestGestures::policyRules()
{
    using namespace OneHandedPolicy;
    Scene application;
    application.application = true;
    Scene dialog = application;
    dialog.dialog = dialog.dialogPanel = true;
    Scene call = application;
    call.alarm = true;
    Scene lockScreen;
    lockScreen.lockScreen = true;
    Scene overDeviceLock = application;
    overDeviceLock.deviceLocked = true;
    Scene home;
    home.homeActive = true;
    Scene grid = home;
    grid.launcher = true;

    State pulled;
    pulled.lowered = pulled.moved = pulled.homeLowered = pulled.launcherLowered = true;
    State typing = pulled;
    typing.keyboard = true;
    State off;
    off.edgeSwitchesOn = true;
    State gridNotLowered = pulled;
    gridNotLowered.launcherLowered = false;

    struct Rule
    {
        const char *name;
        Scene scene;
        State state;
        Placement placement;
        bool dialog;
        bool banners;
        bool keyboard;
        bool emptyArea;
        bool trigger;
        Edge edge;
    };
    const Rule rules[] = {
        { "application", application, pulled, Placement::Lowered,
          false, true, true, true, true, Edge::Side },
        { "application while typing", application, typing, Placement::AboveKeyboard,
          false, false, true, true, false, Edge::AboveKeyboard },
        { "application, off", application, off, Placement::FullScreen,
          false, false, false, false, true, Edge::Side },
        { "panel dialog", dialog, pulled, Placement::Lowered,
          true, true, false, true, true, Edge::Side },
        { "panel dialog while typing", dialog, typing, Placement::FullScreen,
          false, false, false, false, false, Edge::None },
        { "call", call, pulled, Placement::FullScreen,
          false, false, false, false, false, Edge::None },
        { "lock screen", lockScreen, pulled, Placement::FullScreen,
          false, false, false, false, false, Edge::None },
        { "application over the device lock", overDeviceLock, pulled, Placement::Lowered,
          false, true, false, true, false, Edge::None },
        { "home", home, pulled, Placement::Lowered,
          false, true, false, true, true, Edge::Side },
        { "app grid", grid, pulled, Placement::Lowered,
          false, true, false, true, true, Edge::Side },
        { "app grid not lowered", grid, gridNotLowered, Placement::FullScreen,
          false, false, false, false, true, Edge::None },
    };
    for (const Rule &rule : rules) {
        const QByteArray name(rule.name);
        QVERIFY2(placement(rule.scene, rule.state) == rule.placement, name);
        QVERIFY2(dialogFollows(rule.scene, rule.state) == rule.dialog, name);
        QVERIFY2(bannersFollow(rule.scene, rule.state) == rule.banners, name);
        QVERIFY2(keyboardShrunk(rule.scene, rule.state) == rule.keyboard, name);
        QVERIFY2(ownsEmptyArea(rule.scene, rule.state) == rule.emptyArea, name);
        QVERIFY2(triggerAllowed(rule.scene, rule.state) == rule.trigger, name);
        QVERIFY2(edge(rule.scene, rule.state) == rule.edge, name);
    }
    // A tap above a lowered dialog closes it; elsewhere it ends one-handed mode
    QCOMPARE(emptyAreaTap(dialog), Tap::DismissDialog);
    QCOMPARE(emptyAreaTap(application), Tap::SwitchOff);
}

void TestGestures::sceneHooks()
{
    Compositor lipstick;
    QVERIFY(lipstick.window);
    LipstickScene scene(lipstick.window);
    QVERIFY(scene.supports(LipstickScene::OneHanded));
    QVERIFY(scene.supports(LipstickScene::Zoom));
    QVERIFY(scene.supports(LipstickScene::Back));
    QCOMPARE(scene.summary(), QStringLiteral("lipstick: missing nothing"));
    // Items found when first needed, and kept
    QCOMPARE(scene.bannerArea(), lipstick.item("NotificationPreview_popupArea"));
    QCOMPARE(scene.launcherPager(), lipstick.item("pager"));
    QCOMPARE(scene.coverParent(), lipstick.item("LayersParent_cameraArea"));
    QCOMPARE(scene.notificationLayer(), lipstick.item("notifications"));
    QCOMPARE(scene.keyboards(), QList<QQuickItem *> { lipstick.item("keyboard") });
    QCOMPARE(scene.iconGrids(), QList<QQuickItem *> { lipstick.item("Launcher") });
    QVERIFY(LipstickScene::isLayersParent(lipstick.item("layersParent")));
    QVERIFY(!LipstickScene::isLayersParent(lipstick.item("home")));
    // Without lipstick's gesture filter: 5 % of the width
    QCOMPARE(scene.sideBand(), 50.0);
    QCOMPARE(scene.summary(), QStringLiteral("lipstick: missing PeekFilter"));
    // Set on the root as compositor.qml does: read through the attached
    // properties, then kept
    lipstick.window->setProperty("peekBoundaryWidth", 30);
    QCOMPARE(scene.sideBand(), 30.0);
    QCOMPARE(scene.summary(), QStringLiteral("lipstick: missing nothing"));

    // A window without lipstick's properties: no mode acts on half a scene
    QQuickWindow bare;
    LipstickScene none(&bare);
    QVERIFY(!none.supports(LipstickScene::OneHanded));
    QVERIFY(!none.supports(LipstickScene::Zoom));
    QVERIFY(!none.supports(LipstickScene::Back));
    QVERIFY(none.missing().contains(QStringLiteral("topmostWindow")));
    QVERIFY(none.missing().contains(QStringLiteral("deviceIsLocked")));
    QVERIFY(!none.bannerArea());
    QVERIFY(none.missing().contains(QStringLiteral("NotificationPreview_popupArea")));

    // The wallpaper's layer, counted: on while anyone needs it; lipstick's
    // own is never switched off
    QObject *layer = lipstick.item("wallpaper")->property("layer").value<QObject *>();
    QVERIFY(layer);
    QVERIFY(scene.useWallpaperLayer(true));
    QVERIFY(!scene.useWallpaperLayer(true));
    QVERIFY(!scene.useWallpaperLayer(false));
    QVERIFY(layer->property("enabled").toBool());
    QVERIFY(scene.useWallpaperLayer(false));
    QVERIFY(!layer->property("enabled").toBool());
    layer->setProperty("enabled", true);
    QVERIFY(!scene.useWallpaperLayer(true));
    QVERIFY(!scene.useWallpaperLayer(false));
    QVERIFY(layer->property("enabled").toBool());
}

void TestGestures::sceneFront()
{
    Compositor lipstick;
    LipstickScene scene(lipstick.window);
    QSignalSpy front(&scene, &LipstickScene::frontChanged);
    QSignalSpy layers(&scene, &LipstickScene::layersChanged);
    QSignalSpy keyboard(&scene, &LipstickScene::keyboardChanged);
    QVERIFY(!scene.topmostApplication());
    lipstick.showApplication();
    QCOMPARE(scene.topmostApplication(), lipstick.item("app"));
    QVERIFY(OneHandedMode::policyScene(&scene).application);
    QCOMPARE(front.count(), 1);
    // Lipstick's own windows, and calls, alarms and dialogs, are no
    // applications
    lipstick.item("app")->setProperty("category", QStringLiteral("call"));
    QVERIFY(!scene.topmostApplication());
    lipstick.item("app")->setProperty("category", QStringLiteral("silica"));
    QVERIFY(scene.topmostApplication());
    lipstick.item("app")->setProperty("isInProcess", true);
    QVERIFY(!scene.topmostApplication());
    lipstick.item("app")->setProperty("isInProcess", false);

    lipstick.window->setProperty("topmostWindowAngle", 90);
    QVERIFY(!scene.portrait());
    QCOMPARE(front.count(), 2);
    lipstick.item("dialogs")->setProperty("exposed", true);
    lipstick.item("dialogs")->setProperty("renderDialogBackground", true);
    QCOMPARE(layers.count(), 2);
    QCOMPARE(scene.frontDialogLayer(), lipstick.item("dialogs"));
    QVERIFY(OneHandedMode::policyScene(&scene).dialogPanel);
    lipstick.item("alarms")->setProperty("exposed", true);
    QCOMPARE(scene.frontDialogLayer(), lipstick.item("alarms"));
    lipstick.window->setProperty("keyboardHeight", 400);
    QCOMPARE(keyboard.count(), 1);
    QVERIFY(scene.keyboardOpen());
}

void TestGestures::pull()
{
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    QQuickItem *app = lipstick.item("app");
    lipstick.showApplication();
    QObject *wallpaperLayer = lipstick.item("wallpaper")->property("layer").value<QObject *>();
    TouchRecorder recorder;
    window->installEventFilter(&recorder);
    QTouchDevice *device = m_device;
    auto longPress = [&](const QPoint &at) {
        QTest::touchEvent(window, device).press(0, at, window);
        QTest::qWait(700);
        QTest::touchEvent(window, device).release(0, at, window);
    };
    // Narrower than the close swipe's corners, as on the phone
    window->setProperty("peekBoundaryWidth", 20);
    Modes modes(window);
    OneHandedMode *oneHanded = modes.oneHanded.data();
    const qreal mm = Units::pixelsPerMm(window);

    // None: the corner's long press reaches the window; not allowed (not
    // supported, or stopped by the in-use guard) too
    FrogfishSettings::Values values = pullValues();
    values.mode = FrogfishSettings::None;
    modes.configure(values);
    QCOMPARE(oneHanded->diagnostics(), QStringLiteral("one-handed: off, in front application"));
    longPress(Corner);
    QCOMPARE(recorder.positions.count(), 2);
    QCOMPARE(app->y(), 0.0);
    oneHanded->configure(pullValues(), false);
    longPress(Corner);
    QCOMPARE(recorder.positions.count(), 4);
    QCOMPARE(app->y(), 0.0);

    // Pull mode, on by default: nothing changes until the corner's long press
    modes.configure(pullValues());
    QCOMPARE(app->y(), 0.0);
    QVERIFY(!oneHanded->inUse());
    QCOMPARE(oneHanded->diagnostics(), QStringLiteral("one-handed: off, waiting for the corner or the edge, in front application"));

    // A tap in the corner is given back, whole
    recorder.positions.clear();
    QTest::touchEvent(window, device).press(0, Corner, window);
    QTest::touchEvent(window, device).release(0, Corner, window);
    QCOMPARE(recorder.positions.count(), 2);
    QCOMPARE(app->y(), 0.0);
    // A swipe from the corner too, in order
    recorder.positions.clear();
    QTest::touchEvent(window, device).press(0, Corner, window);
    QTest::touchEvent(window, device).move(0, QPoint(995, 1800), window);
    QTest::touchEvent(window, device).release(0, QPoint(995, 1800), window);
    QCOMPARE(recorder.positions.count(), 3);
    QCOMPARE(recorder.positions.at(0), QPointF(Corner));
    QCOMPARE(recorder.positions.at(1), QPointF(995, 1800));

    // The long press, lifted in time, pulls: moved down, not resized; nothing
    // reaches the window
    recorder.positions.clear();
    QSignalSpy inUse(oneHanded, &OneHandedMode::inUseChanged);
    longPress(Corner);
    QTRY_COMPARE(app->y(), 800.0);
    QCOMPARE(app->size(), QSizeF(1000, 2000));
    QCOMPARE(app->scale(), 1.0);
    QCOMPARE(recorder.positions.count(), 0);
    QVERIFY(wallpaperLayer->property("enabled").toBool());
    QCOMPARE(inUse.count(), 1);
    QVERIFY(oneHanded->inUse());
    QCOMPARE(oneHanded->diagnostics(), QStringLiteral("one-handed: on, pulled to 60 %, in front application"));
    QCOMPARE(oneHanded->picture(), QRectF(0, 800, 1000, 1200));
    // A cover over the empty part, and something under the finger there
    QQuickItem *cover = oneHanded->findChild<QQuickItem *>(QStringLiteral("frogfish-onehanded-cover"));
    QVERIFY(cover);
    QVERIFY(cover->isVisible());
    QCOMPARE(cover->height(), 800.0);
    QCOMPARE(cover->parentItem(), lipstick.item("LayersParent_cameraArea"));
    QQuickItem *empty = window->findChild<QQuickItem *>(QStringLiteral("frogfish-onehanded-empty"));
    QVERIFY(empty);
    QVERIFY(empty->contains(empty->mapFromItem(window->contentItem(), QPointF(500, 100))));
    // Lipstick's screens below the same top
    QCOMPARE(lipstick.item("home")->property("anchors").value<QObject *>()->property("topMargin").toReal(), 800.0);
    QCOMPARE(lipstick.item("topMenuLayer")->mapToScene(QPointF(0, 0)), QPointF(0, 800));
    QCOMPARE(lipstick.item("menu")->height(), 1200.0);
    QCOMPARE(lipstick.item("pager")->height(), 1200.0);
    QCOMPARE(lipstick.item("Launcher")->property("rows").toInt(), 4);

    // Held too long: a grip, nothing changes
    QTest::touchEvent(window, device).press(0, Corner, window);
    QTest::qWait(1700);
    QTest::touchEvent(window, device).release(0, Corner, window);
    QTest::qWait(50);
    QCOMPARE(app->y(), 800.0);

    // A swipe down from the empty area opens the top menu, taken whole
    // (lipstick's own drags would act on the rest)
    QObject *topMenu = lipstick.item("topMenuLayer");
    QTest::touchEvent(window, device).press(0, QPoint(500, 300), window);
    QTest::touchEvent(window, device).move(0, QPoint(510, 700), window);
    QTest::touchEvent(window, device).release(0, QPoint(510, 700), window);
    QCOMPARE(topMenu->property("shown").toInt(), 1);
    QCOMPARE(recorder.positions.count(), 0);
    QCOMPARE(app->y(), 800.0);
    // ... near a side (10 mm), it closes the application
    const int nearSide = qRound((20 + 10 * mm) / 2);
    QTest::touchEvent(window, device).press(0, QPoint(nearSide, 300), window);
    QTest::touchEvent(window, device).move(0, QPoint(nearSide, 700), window);
    QTest::touchEvent(window, device).release(0, QPoint(nearSide, 700), window);
    QCOMPARE(lipstick.item("apps")->property("closed").toInt(), 1);
    QVERIFY(lipstick.item("apps")->property("_closeDismissedWindow").toBool());
    // The side strips keep their own swipes
    QTest::touchEvent(window, device).press(0, QPoint(10, 700), window);
    QTest::touchEvent(window, device).release(0, QPoint(10, 700), window);
    QCOMPARE(recorder.positions.count(), 2);

    // Another size applies at the next pull
    values = pullValues();
    values.size = 0.45;
    modes.configure(values);
    QCOMPARE(app->y(), 800.0);
    longPress(Corner);
    QTRY_COMPARE(app->y(), 0.0);
    longPress(Corner);
    QTRY_COMPARE(app->y(), 1100.0);
    modes.configure(pullValues());
    longPress(Corner);
    QTRY_COMPARE(app->y(), 0.0);
    longPress(Corner);
    QTRY_COMPARE(app->y(), 800.0);

    // Landscape: everything put back until portrait returns
    window->setProperty("topmostWindowAngle", 90);
    QCOMPARE(app->y(), 0.0);
    QVERIFY(!cover->isVisible());
    QVERIFY(!oneHanded->inUse());
    window->setProperty("topmostWindowAngle", 0);
    QCOMPARE(app->y(), 800.0);

    // A tap on the empty area switches off
    recorder.positions.clear();
    QTest::touchEvent(window, device).press(0, QPoint(500, 300), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, 300), window);
    QTRY_COMPARE(app->y(), 0.0);
    QVERIFY(!cover->isVisible());
    QVERIFY(!wallpaperLayer->property("enabled").toBool());
    QVERIFY(!window->findChild<QQuickItem *>(QStringLiteral("frogfish-onehanded-empty")));
    QCOMPARE(recorder.positions.count(), 0);
    QVERIFY(!oneHanded->inUse());
    QCOMPARE(lipstick.item("pager")->height(), 2000.0);
    QCOMPARE(lipstick.item("Launcher")->property("rows").toInt(), 8);

    // Landscape: the corner is the app's
    window->setProperty("topmostWindowAngle", 90);
    longPress(Corner);
    QCOMPARE(recorder.positions.count(), 2);
    QCOMPARE(app->y(), 0.0);
    window->setProperty("topmostWindowAngle", 0);

    // Switched off while pulled: put back
    longPress(Corner);
    QTRY_COMPARE(app->y(), 800.0);
    values = pullValues();
    values.mode = FrogfishSettings::None;
    modes.configure(values);
    QCOMPARE(app->y(), 0.0);
    QVERIFY(!oneHanded->inUse());
    window->removeEventFilter(&recorder);
}

void TestGestures::pullHome()
{
    // Home in front and no application opened yet: the corner still lowers
    // what there is
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    Modes modes(window);
    modes.configure(pullValues());
    auto longPress = [&](const QPoint &at) {
        QTest::touchEvent(window, m_device).press(0, at, window);
        QTest::qWait(700);
        QTest::touchEvent(window, m_device).release(0, at, window);
    };
    QObject *anchors = lipstick.item("home")->property("anchors").value<QObject *>();
    longPress(Corner);
    QQuickItem *cover = nullptr;
    QTRY_VERIFY((cover = modes.oneHanded->findChild<QQuickItem *>(QStringLiteral("frogfish-onehanded-cover"))));
    QVERIFY(cover->isVisible());
    QCOMPARE(cover->height(), 800.0);
    QCOMPARE(anchors->property("topMargin").toReal(), 800.0);
    QCOMPARE(lipstick.item("cards")->property("_pageHeight").toReal(), 1200.0);
    QVERIFY(lipstick.item("cards")->property("columnsUpdated").toInt() > 0);
    // The status bar: half of the difference, in its centred item
    QCOMPARE(lipstick.item("statusBar")->mapToItem(lipstick.item("home"), QPointF(0, 0)), QPointF(0, 400));
    QCOMPARE(modes.oneHanded->diagnostics(), QStringLiteral("one-handed: on, pulled to 60 %, in front home"));

    // The corner again: off; and on, to the same place
    longPress(Corner);
    QTRY_VERIFY(!cover->isVisible());
    QCOMPARE(anchors->property("topMargin").toReal(), 0.0);
    QCOMPARE(lipstick.item("cards")->property("_pageHeight").toReal(), 2000.0);
    longPress(Corner);
    QTRY_VERIFY(cover->isVisible());
    QCOMPARE(cover->height(), 800.0);
    longPress(Corner);
    QTRY_VERIFY(!cover->isVisible());
}

void TestGestures::pullEdge()
{
    // In pull mode the edge pulls with the finger, down to the fixed
    // smallest size and back
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    QQuickItem *app = lipstick.item("app");
    lipstick.showApplication();
    TouchRecorder recorder;
    window->installEventFilter(&recorder);
    auto edgeDrag = [&](int from, int via, int to) {
        QTest::touchEvent(window, m_device).press(0, QPoint(998, from), window);
        QTest::qWait(600);
        QTest::touchEvent(window, m_device).move(0, QPoint(998, via), window);
        QTest::touchEvent(window, m_device).move(0, QPoint(998, to), window);
        QTest::touchEvent(window, m_device).release(0, QPoint(998, to), window);
    };
    Modes modes(window);
    OneHandedMode *oneHanded = modes.oneHanded.data();
    modes.configure(pullValues());

    // The ring, while it plays, stays under the finger through the drag
    QTest::touchEvent(window, m_device).press(0, QPoint(998, 1000), window);
    QTest::qWait(600);
    QTest::touchEvent(window, m_device).move(0, QPoint(998, 1100), window);
    QTest::touchEvent(window, m_device).move(0, QPoint(998, 1300), window);
    QQuickItem *ring = playingRing(window);
    QVERIFY(ring);
    QCOMPARE(centre(ring), QPointF(998, 1300));
    QTest::touchEvent(window, m_device).release(0, QPoint(998, 1300), window);
    QTRY_COMPARE(app->y(), 300.0);
    // From off: the screen follows the finger, moved, not scaled
    edgeDrag(1300, 1200, 1000);
    QTRY_COMPARE(app->y(), 0.0);
    edgeDrag(1000, 1100, 1300);
    QTRY_COMPARE(app->y(), 300.0);
    QCOMPARE(app->scale(), 1.0);
    QVERIFY(oneHanded->active());
    QCOMPARE(oneHanded->diagnostics(), QStringLiteral("one-handed: on, pulled to 85 %, in front application"));
    QCOMPARE(oneHanded->picture(), QRectF(0, 300, 1000, 1700));
    // Further, 1:1 with the finger
    edgeDrag(1400, 1500, 1600);
    QTRY_COMPARE(app->y(), 500.0);
    // ... not below the fixed smallest (45 %)
    edgeDrag(1000, 1500, 1950);
    QTRY_COMPARE(app->y(), 1100.0);
    // Back up
    edgeDrag(1500, 1400, 1300);
    QTRY_COMPARE(app->y(), 900.0);
    // The corner then ends it; again, to the size set
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QTRY_COMPARE(app->y(), 0.0);
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QTRY_COMPARE(app->y(), 800.0);
    // Dragged back up to the top: off
    edgeDrag(1000, 900, 0);
    QTRY_COMPARE(app->y(), 0.0);
    QVERIFY(!oneHanded->active());
    QVERIFY(!oneHanded->inUse());
    QCOMPARE(recorder.positions.count(), 0);

    // While typing the pull waits, and the edge is the app's
    window->setProperty("keyboardHeight", 500);
    QTest::touchEvent(window, m_device).press(0, QPoint(998, 1000), window);
    QTest::touchEvent(window, m_device).release(0, QPoint(998, 1000), window);
    QCOMPARE(recorder.positions.count(), 2);
    window->setProperty("keyboardHeight", 0);
    window->removeEventFilter(&recorder);
}

void TestGestures::shrink()
{
    // The edge shrinks into the screen's bottom corner, the point under the
    // finger staying under it
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    QQuickItem *app = lipstick.item("app");
    QQuickItem *keyboard = lipstick.item("keyboard");
    lipstick.showApplication();
    TouchRecorder recorder;
    window->installEventFilter(&recorder);
    QTouchDevice *device = m_device;
    auto edgeDrag = [&](int from, int via, int to) {
        QTest::touchEvent(window, device).press(0, QPoint(998, from), window);
        QTest::qWait(600);
        QTest::touchEvent(window, device).move(0, QPoint(998, via), window);
        QTest::touchEvent(window, device).move(0, QPoint(998, to), window);
        QTest::touchEvent(window, device).release(0, QPoint(998, to), window);
    };
    auto tap = [&](const QPoint &at) {
        QTest::touchEvent(window, device).press(0, at, window);
        QTest::touchEvent(window, device).release(0, at, window);
    };
    Modes modes(window);
    OneHandedMode *oneHanded = modes.oneHanded.data();
    FrogfishSettings::Values values = shrinkValues();
    modes.configure(values);
    QCOMPARE(oneHanded->diagnostics(), QStringLiteral("one-handed: off, waiting for the corner or the edge, in front application"));

    // From off, a hold and a drag down: 1000 px from the corner, then 800 px
    edgeDrag(1000, 1100, 1200);
    QTRY_COMPARE(app->scale(), 0.8);
    QCOMPARE(app->y(), 400.0);
    QCOMPARE(app->transformOrigin(), QQuickItem::TopRight);
    QCOMPARE(oneHanded->diagnostics(), QStringLiteral("one-handed: on, shrunk to 80 %, in front application"));
    QCOMPARE(oneHanded->picture(), QRectF(200, 400, 800, 1600));
    // The far side is covered too
    QQuickItem *side = oneHanded->findChild<QQuickItem *>(QStringLiteral("frogfish-onehanded-cover-side"));
    QVERIFY(side && side->isVisible());
    QCOMPARE(side->width(), 200.0);
    // The corner: back to full size, and shrunk to the size set (60 %)
    auto longPress = [&]() {
        QTest::touchEvent(window, device).press(0, Corner, window);
        QTest::qWait(700);
        QTest::touchEvent(window, device).release(0, Corner, window);
    };
    longPress();
    QTRY_COMPARE(app->scale(), 1.0);
    QCOMPARE(app->y(), 0.0);
    longPress();
    QTRY_COMPARE(app->scale(), 0.6);
    QCOMPARE(app->y(), 800.0);
    QCOMPARE(recorder.positions.count(), 0);

    // Further, from the current size, below the size set but not below the
    // fixed smallest (45 %)
    edgeDrag(1200, 1300, 1900);
    QTRY_COMPARE(app->scale(), 0.45);
    QCOMPARE(app->y(), 1100.0);
    // Another size set applies at the next corner or top menu tap
    values.size = 0.75;
    modes.configure(values);
    QCOMPARE(app->scale(), 0.45);
    // Back up: larger again
    edgeDrag(1700, 1650, 1600);
    QTRY_COMPARE(app->scale(), 0.6);
    QCOMPARE(app->y(), 800.0);

    // The keyboard (a window of category "overlay") has the shrink; one the
    // compositor announces is shrunk at once, before it ever shows
    QMetaObject::invokeMethod(window, "windowAdded", Q_ARG(QObject *, keyboard));
    QCOMPARE(keyboard->scale(), 0.6);
    QCOMPARE(keyboard->transformOrigin(), QQuickItem::BottomRight);
    // While typing the application is full height, shrunk from the bottom
    // corner with the keyboard
    window->setProperty("keyboardHeight", 500);
    QCOMPARE(app->y(), 0.0);
    QCOMPARE(app->transformOrigin(), QQuickItem::BottomRight);
    QCOMPARE(keyboard->scale(), 0.6);
    QCOMPARE(oneHanded->picture(), QRectF(400, 800, 600, 1200));
    // Closing, the keyboard keeps the shrink (no full size on its way out)
    window->setProperty("keyboardHeight", 0);
    QCOMPARE(keyboard->scale(), 0.6);
    QCOMPARE(app->y(), 800.0);
    // While typing, the edge works above the keyboard only
    window->setProperty("keyboardHeight", 500);
    QTest::touchEvent(window, device).press(0, QPoint(998, 1900), window);
    QTest::touchEvent(window, device).release(0, QPoint(998, 1900), window);
    QCOMPARE(recorder.positions.count(), 2);
    recorder.positions.clear();
    // ... and the black part above the application is the empty area: a tap
    // there ends one-handed mode
    tap(QPoint(500, 100));
    QTRY_COMPARE(app->scale(), 1.0);
    QCOMPARE(keyboard->scale(), 1.0);
    QCOMPARE(keyboard->transformOrigin(), QQuickItem::Center);
    QCOMPARE(app->transformOrigin(), QQuickItem::Center);
    QCOMPARE(recorder.positions.count(), 0);
    window->setProperty("keyboardHeight", 0);

    // Off: a hold and a drag up does nothing (nothing to enlarge)
    edgeDrag(1000, 900, 800);
    QTest::qWait(50);
    QCOMPARE(app->scale(), 1.0);
    QCOMPARE(recorder.positions.count(), 0);
    // A drag of a few millimetres snaps back to full size: off. The top
    // moves twice as far as the finger here (1000 px from the corner).
    const qreal mm = Units::pixelsPerMm(window);
    QSignalSpy inUse(oneHanded, &OneHandedMode::inUseChanged);
    edgeDrag(1000, qRound(1000 + 3.5 * mm), qRound(1000 + 4 * mm));
    QTRY_COMPARE(inUse.count(), 2);
    QCOMPARE(app->scale(), 1.0);
    QVERIFY(!oneHanded->inUse());

    // Dragged back to full size: off
    edgeDrag(1000, 1100, 1200);
    QTRY_COMPARE(app->scale(), 0.8);
    edgeDrag(1200, 1100, 0);
    QTRY_COMPARE(app->scale(), 1.0);
    QCOMPARE(app->y(), 0.0);
    QVERIFY(!oneHanded->inUse());
    // None of it reached the window
    QCOMPARE(recorder.positions.count(), 0);

    // Pull mode chosen while shrunk: off; the edge then pulls
    edgeDrag(1000, 1100, 1200);
    QTRY_COMPARE(app->scale(), 0.8);
    modes.configure(pullValues());
    QCOMPARE(app->scale(), 1.0);
    QCOMPARE(app->y(), 0.0);
    edgeDrag(1000, 1100, 1200);
    QTRY_COMPARE(app->y(), 200.0);
    QCOMPARE(app->scale(), 1.0);
    QCOMPARE(recorder.positions.count(), 0);
    window->removeEventFilter(&recorder);
}

void TestGestures::shrinkHome()
{
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    Modes modes(window);
    modes.configure(shrinkValues());
    auto edgeDrag = [&](int from, int via, int to) {
        QTest::touchEvent(window, m_device).press(0, QPoint(998, from), window);
        QTest::qWait(600);
        QTest::touchEvent(window, m_device).move(0, QPoint(998, via), window);
        QTest::touchEvent(window, m_device).move(0, QPoint(998, to), window);
        QTest::touchEvent(window, m_device).release(0, QPoint(998, to), window);
    };
    // Home is shrunk and laid out taller, so that it still ends at the bottom
    edgeDrag(1000, 1100, 1300);
    QQuickItem *cover = nullptr;
    QTRY_VERIFY((cover = modes.oneHanded->findChild<QQuickItem *>(QStringLiteral("frogfish-onehanded-cover"))));
    QVERIFY(cover->isVisible());
    QCOMPARE(cover->height(), 600.0);
    QObject *anchors = lipstick.item("home")->property("anchors").value<QObject *>();
    QCOMPARE(anchors->property("topMargin").toReal(), 600.0);
    QVERIFY(qAbs(lipstick.item("home")->mapToScene(QPointF(0, lipstick.item("home")->height())).y() - 2000) < 0.01);
    edgeDrag(1300, 1200, 0);
    QTRY_VERIFY(!cover->isVisible());
    QCOMPARE(anchors->property("topMargin").toReal(), 0.0);
}

void TestGestures::topMenuSwitch()
{
    // The top menu switch does what the corner does, and only where it does
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    QQuickItem *app = lipstick.item("app");
    lipstick.showApplication();
    Modes modes(window);
    OneHandedMode *oneHanded = modes.oneHanded.data();
    QSignalSpy active(oneHanded, &OneHandedMode::activeChanged);
    modes.configure(pullValues());
    oneHanded->setActive(true);
    QVERIFY(oneHanded->active());
    QTRY_COMPARE(app->y(), 800.0);
    QCOMPARE(active.count(), 1);
    QCOMPARE(active.last().first().toBool(), true);
    oneHanded->setActive(false);
    QTRY_COMPARE(app->y(), 0.0);
    QCOMPARE(active.count(), 2);
    QCOMPARE(active.last().first().toBool(), false);

    // Shrink: to the size set, then the edge makes it larger
    FrogfishSettings::Values values = shrinkValues();
    values.size = 0.7;
    modes.configure(values);
    oneHanded->setActive(true);
    QTRY_COMPARE(app->scale(), 0.7);
    QCOMPARE(app->y(), 600.0);
    oneHanded->setActive(false);
    QTRY_COMPARE(app->scale(), 1.0);

    // Not with mode none, on the lock screen, while typing, in landscape
    values.mode = FrogfishSettings::None;
    modes.configure(values);
    oneHanded->setActive(true);
    QVERIFY(!oneHanded->active());
    modes.configure(pullValues());
    lipstick.item("lockScreen")->setProperty("exposed", true);
    oneHanded->setActive(true);
    QVERIFY(!oneHanded->active());
    lipstick.item("lockScreen")->setProperty("exposed", false);
    window->setProperty("keyboardHeight", 500);
    oneHanded->setActive(true);
    QVERIFY(!oneHanded->active());
    window->setProperty("keyboardHeight", 0);
    window->setProperty("topmostWindowAngle", 90);
    oneHanded->setActive(true);
    QVERIFY(!oneHanded->active());
    window->setProperty("topmostWindowAngle", 0);
    QTest::qWait(20);
    QCOMPARE(app->y(), 0.0);
    QCOMPARE(active.count(), 4);

    // The corner's changes are reported too
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QTRY_COMPARE(active.count(), 5);
    QVERIFY(oneHanded->active());
    // ... and switching to none ends it
    values.mode = FrogfishSettings::None;
    modes.configure(values);
    QCOMPARE(active.count(), 6);
    QVERIFY(!oneHanded->active());
}

void TestGestures::putBack()
{
    // Everything one-handed mode and the zoom touch of lipstick's is as it
    // was once they end, and when they are removed while on
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    lipstick.showApplication();
    auto state = [&]() {
        QStringList lines;
        for (const char *name : { "layersParent", "wallpaper", "home", "statusBar", "apps", "appWrapper", "app",
                                  "lockScreen", "dialogs", "alarms", "launcher", "pager", "Launcher",
                                  "topMenuLayer", "menu", "keyboard", "notifications", "EventsView_window",
                                  "cards" }) {
            QQuickItem *item = lipstick.item(name);
            QObject *anchors = item->property("anchors").value<QObject *>();
            QObject *layer = item->property("layer").value<QObject *>();
            lines << QStringLiteral("%1: %2 transforms, %3,%4 %5x%6, scale %7, origin %8, margins %9 %10, layer %11")
                     .arg(QLatin1String(name)).arg(lipstick.transforms(name))
                     .arg(item->x()).arg(item->y()).arg(item->width()).arg(item->height())
                     .arg(item->scale()).arg(item->transformOrigin())
                     .arg(anchors->property("topMargin").toReal()).arg(anchors->property("bottomMargin").toReal())
                     .arg(layer->property("enabled").toBool());
        }
        lines << QStringLiteral("cards page %1, rows %2")
                 .arg(lipstick.item("cards")->property("_pageHeight").toReal())
                 .arg(lipstick.item("Launcher")->property("rows").toInt());
        // Frogfish's own items, shown
        QList<QQuickItem *> items { window->contentItem() };
        while (!items.isEmpty()) {
            QQuickItem *item = items.takeFirst();
            items += item->childItems();
            if (item->objectName().startsWith(QLatin1String("frogfish-")) && item->isVisible())
                lines << item->objectName();
        }
        return lines;
    };
    auto longPress = [&](const QPoint &at) {
        QTest::touchEvent(window, m_device).press(0, at, window);
        QTest::qWait(700);
        QTest::touchEvent(window, m_device).release(0, at, window);
    };
    auto edgeDrag = [&](int from, int to) {
        QTest::touchEvent(window, m_device).press(0, QPoint(998, from), window);
        QTest::qWait(600);
        QTest::touchEvent(window, m_device).move(0, QPoint(998, (from + to) / 2), window);
        QTest::touchEvent(window, m_device).move(0, QPoint(998, to), window);
        QTest::touchEvent(window, m_device).release(0, QPoint(998, to), window);
    };
    auto tap = [&](const QPoint &at) {
        QTest::touchEvent(window, m_device).press(0, at, window);
        QTest::touchEvent(window, m_device).release(0, at, window);
    };
    const QStringList before = state();

    {
        Modes modes(window, true);
        FrogfishSettings::Values values = pullValues();
        values.zoom = true;
        modes.configure(values);
        QCOMPARE(state(), before);

        // Pulled over an application, with a panel dialog and a banner
        lipstick.item("dialogs")->setProperty("exposed", true);
        lipstick.item("dialogs")->setProperty("renderDialogBackground", true);
        longPress(Corner);
        QTRY_COMPARE(lipstick.item("app")->y(), 800.0);
        QCOMPARE(lipstick.transforms("dialogs"), 2);
        QCOMPARE(lipstick.transforms("notifications"), 2);
        QVERIFY(state() != before);
        lipstick.item("dialogs")->setProperty("exposed", false);
        tap(QPoint(500, 300));
        QTRY_COMPARE(lipstick.item("app")->y(), 0.0);
        QCOMPARE(state(), before);

        // Zoomed, then not
        QTest::touchEvent(window, m_device).press(0, QPoint(500, 1998), window);
        QTest::qWait(600);
        QTest::touchEvent(window, m_device).move(0, QPoint(500, 1800), window);
        QTest::touchEvent(window, m_device).release(0, QPoint(500, 1800), window);
        QVERIFY(modes.zoom->zoomed());
        QVERIFY(state() != before);
        values.zoom = false;
        modes.configure(values);
        QVERIFY(!modes.zoom->zoomed());
        QCOMPARE(state(), before);

        // Shrunk, with the keyboard
        modes.configure(shrinkValues());
        edgeDrag(1000, 1200);
        QTRY_COMPARE(lipstick.item("app")->scale(), 0.8);
        window->setProperty("keyboardHeight", 500);
        QMetaObject::invokeMethod(window, "windowAdded", Q_ARG(QObject *, lipstick.item("keyboard")));
        QCOMPARE(lipstick.item("keyboard")->scale(), 0.8);
        window->setProperty("keyboardHeight", 0);
        edgeDrag(1200, 0);
        QTRY_COMPARE(lipstick.item("app")->scale(), 1.0);
        QCOMPARE(state(), before);

        // Removed while pulled and zoomed
        values = pullValues();
        values.zoom = true;
        modes.configure(values);
        longPress(Corner);
        QTRY_COMPARE(lipstick.item("app")->y(), 800.0);
        QTest::touchEvent(window, m_device).press(0, QPoint(500, 1998), window);
        QTest::qWait(600);
        QTest::touchEvent(window, m_device).move(0, QPoint(500, 1800), window);
        QTest::touchEvent(window, m_device).release(0, QPoint(500, 1800), window);
        QVERIFY(modes.zoom->zoomed());
    }
    QCOMPARE(state(), before);
}

void TestGestures::zoom()
{
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    QQuickItem *content = window->contentItem();
    QQuickItem *wallpaper = lipstick.item("wallpaper");
    QQuickItem *layers = lipstick.item("layersParent");
    QObject *layersLayer = layers->property("layer").value<QObject *>();
    QVERIFY(layersLayer);
    Modes modes(window, true);
    ZoomMode *zoom = modes.zoom.data();
    // Off: the strip's long press is the app's
    FrogfishSettings::Values values = pullValues();
    values.mode = FrogfishSettings::None;
    modes.configure(values);
    QCOMPARE(zoom->diagnostics(), QStringLiteral("zoom: off"));
    // The default maximum is 8x; 4x keeps the pinches below short
    values.zoom = true;
    values.zoomMaximum = 4;
    modes.configure(values);
    QCOMPARE(zoom->diagnostics(), QStringLiteral("zoom: on"));
    TouchRecorder recorder;
    window->installEventFilter(&recorder);
    QTouchDevice *device = m_device;
    // Where the scene lands: lipstick's items are scaled, not the window's
    // content item (which keeps the zoom's own items unmagnified)
    auto sceneOffset = [wallpaper]() { return wallpaper->mapToScene(QPointF(0, 0)); };
    auto sceneScale = [wallpaper]() {
        return wallpaper->mapToScene(QPointF(100, 0)).x() / 100 - wallpaper->mapToScene(QPointF(0, 0)).x() / 100;
    };
    auto near = [](const QPointF &a, const QPointF &b) {
        return qAbs(a.x() - b.x()) < 0.01 && qAbs(a.y() - b.y()) < 0.01;
    };
    const int strip = 1998;
    const int doubling = qRound(20 * zoom->mm());
    auto hold = [&](const QPoint &at) {
        QTest::touchEvent(window, device).press(0, at, window);
        QTest::qWait(600);
    };

    // On the strip, a tap is given back whole, and so is a swipe (the app
    // grid's), in order; the corners are one-handed mode's
    QTest::touchEvent(window, device).press(0, QPoint(500, strip), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip), window);
    QCOMPARE(recorder.positions.count(), 2);
    recorder.positions.clear();
    QTest::touchEvent(window, device).press(0, QPoint(500, strip), window);
    QTest::touchEvent(window, device).move(0, QPoint(500, 1800), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, 1800), window);
    QCOMPARE(recorder.positions.count(), 3);
    QCOMPARE(recorder.positions.at(1), QPointF(500, 1800));
    recorder.positions.clear();
    hold(QPoint(5, strip));
    QTest::touchEvent(window, device).release(0, QPoint(5, strip), window);
    QCOMPARE(recorder.positions.count(), 2);
    QVERIFY(!zoom->zoomed());

    // The long press alone: zoom mode at 1x, and let go there, off again
    recorder.positions.clear();
    QSignalSpy inUse(zoom, &ZoomMode::inUseChanged);
    hold(QPoint(500, strip));
    QVERIFY(zoom->zoomed());
    QCOMPARE(zoom->level(), 1.0);
    QCOMPARE(inUse.count(), 1);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip), window);
    QVERIFY(!zoom->zoomed());
    QCOMPARE(inUse.count(), 2);
    QCOMPARE(recorder.positions.count(), 0);

    // ... dragged up: twice per 20 mm, around the finger's x and the middle
    // of the screen; down again and let go at 1x: off, in one gesture
    hold(QPoint(500, strip));
    QTest::touchEvent(window, device).move(0, QPoint(500, strip - doubling), window);
    QVERIFY(qAbs(zoom->level() - zoom->levelForDrag(doubling)) < 0.001);
    QTest::touchEvent(window, device).move(0, QPoint(500, strip - 2), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip - 2), window);
    QVERIFY(!zoom->zoomed());
    hold(QPoint(500, strip));
    QTest::touchEvent(window, device).move(0, QPoint(500, strip - doubling), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip - doubling), window);
    QVERIFY(zoom->zoomed());
    const qreal level = zoom->level();
    QVERIFY(qAbs(level - 2) < 0.05);
    QCOMPARE(zoom->diagnostics(), QStringLiteral("zoom: on, zoomed %1x").arg(level, 0, 'f', 1));
    QVERIFY(qAbs(sceneScale() - level) < 0.001);
    QVERIFY(near(sceneOffset(), QPointF(500 - 500 * level, 1000 - 1000 * level)));
    QCOMPARE(content->scale(), 1.0);
    QCOMPARE(recorder.positions.count(), 0);
    // LayersParent is drawn into a layer, then magnified like the rest
    QVERIFY(layersLayer->property("enabled").toBool());
    QVERIFY(near(layers->mapToScene(QPointF(0, 0)), sceneOffset()));
    // The other top-level items are magnified too
    QVERIFY(near(lipstick.item("notifications")->mapToScene(QPointF(0, 0)), sceneOffset()));
    // The overlay (border, level) is not magnified: it covers the screen
    QQuickItem *overlay = zoom->findChild<QQuickItem *>(QStringLiteral("frogfish-zoom-overlay"));
    QQuickItem *controls = zoom->findChild<QQuickItem *>(QStringLiteral("frogfish-zoom-controls"));
    QVERIFY(overlay && controls);
    QVERIFY(overlay->isVisible());
    QCOMPARE(overlay->mapToScene(QPointF(1000, 2000)), QPointF(1000, 2000));
    // The level shows while it changes, then fades; the button shows
    QVERIFY(controls->property("showLevel").toBool());
    QTRY_VERIFY(!controls->property("showLevel").toBool());
    const QRectF button = zoom->buttonRect();
    QVERIFY(button.right() < 1000 && button.bottom() < 2000 && button.left() > 900);

    // Pan mode first (read-only): a tap reaches nothing, not on the strip
    // either
    QVERIFY(!zoom->touchMode());
    QTest::touchEvent(window, device).press(0, QPoint(500, 1000), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, 1000), window);
    QTest::touchEvent(window, device).press(0, QPoint(500, strip), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip), window);
    QCOMPARE(recorder.positions.count(), 0);
    // ... a drag pans, 1:1
    QPointF before = sceneOffset();
    QTest::touchEvent(window, device).press(0, QPoint(500, 1000), window);
    QTest::touchEvent(window, device).move(0, QPoint(560, 1080), window);
    QTest::touchEvent(window, device).release(0, QPoint(560, 1080), window);
    QVERIFY(near(sceneOffset(), before + QPointF(60, 80)));
    QCOMPARE(recorder.positions.count(), 0);
    // ... never past the screen's edges
    for (int i = 0; i < 3; ++i) {
        QTest::touchEvent(window, device).press(0, QPoint(100, 100), window);
        QTest::touchEvent(window, device).move(0, QPoint(900, 1900), window);
        QTest::touchEvent(window, device).release(0, QPoint(900, 1900), window);
    }
    QVERIFY(near(sceneOffset(), QPointF(0, 0)));

    // Two fingers pinch: the level around their middle, up to the maximum
    QTest::touchEvent(window, device).press(0, QPoint(400, 1000), window).press(1, QPoint(600, 1000), window);
    QTest::touchEvent(window, device).move(0, QPoint(300, 1000), window).move(1, QPoint(700, 1000), window);
    QVERIFY(qAbs(zoom->level() - qMin(4.0, 2 * level)) < 0.01);
    QTest::touchEvent(window, device).move(0, QPoint(100, 1000), window).move(1, QPoint(900, 1000), window);
    QTest::touchEvent(window, device).release(0, QPoint(100, 1000), window).release(1, QPoint(900, 1000), window);
    QCOMPARE(zoom->level(), 4.0);
    QCOMPARE(recorder.positions.count(), 0);
    // ... a pinch that starts as one finger, then two
    QTest::touchEvent(window, device).press(0, QPoint(300, 1000), window);
    QTest::touchEvent(window, device).move(0, QPoint(300, 1000), window).press(1, QPoint(700, 1000), window);
    QTest::touchEvent(window, device).move(0, QPoint(400, 1000), window).move(1, QPoint(600, 1000), window);
    QTest::touchEvent(window, device).release(0, QPoint(400, 1000), window).release(1, QPoint(600, 1000), window);
    QVERIFY(qAbs(zoom->level() - 2) < 0.01);
    QCOMPARE(recorder.positions.count(), 0);

    // The button: pan above, touch below; the current one again changes
    // nothing
    before = sceneOffset();
    const QPoint panHalf(qRound(button.center().x()), qRound(button.top() + button.height() / 4));
    const QPoint touchHalf(qRound(button.center().x()), qRound(button.bottom() - button.height() / 4));
    QTest::touchEvent(window, device).press(0, panHalf, window);
    QTest::touchEvent(window, device).release(0, panHalf, window);
    QVERIFY(!zoom->touchMode());
    QTest::touchEvent(window, device).press(0, touchHalf, window);
    QTest::touchEvent(window, device).release(0, touchHalf, window);
    QVERIFY(zoom->touchMode());
    QVERIFY(controls->property("edit").toBool());
    // Touch mode: input reaches the app, the pan is locked
    QTest::touchEvent(window, device).press(0, QPoint(400, 800), window);
    QTest::touchEvent(window, device).move(0, QPoint(300, 700), window);
    QTest::touchEvent(window, device).release(0, QPoint(300, 700), window);
    QCOMPARE(recorder.positions.count(), 3);
    QVERIFY(near(sceneOffset(), before));
    // ... a tap on the strip too
    recorder.positions.clear();
    QTest::touchEvent(window, device).press(0, QPoint(500, strip), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip), window);
    QCOMPARE(recorder.positions.count(), 2);
    // ... the button stays the zoom's
    recorder.positions.clear();
    QTest::touchEvent(window, device).press(0, touchHalf, window);
    QTest::touchEvent(window, device).release(0, touchHalf, window);
    QVERIFY(zoom->touchMode());
    QTest::touchEvent(window, device).press(0, panHalf, window);
    QTest::touchEvent(window, device).release(0, panHalf, window);
    QCOMPARE(recorder.positions.count(), 0);
    QVERIFY(!zoom->touchMode());

    // Pinched to 1x: the zoom ends
    QTest::touchEvent(window, device).press(0, QPoint(100, 1000), window).press(1, QPoint(900, 1000), window);
    QTest::touchEvent(window, device).move(0, QPoint(450, 1000), window).move(1, QPoint(550, 1000), window);
    QTest::touchEvent(window, device).release(0, QPoint(450, 1000), window).release(1, QPoint(550, 1000), window);
    QVERIFY(!zoom->zoomed());
    QVERIFY(qAbs(sceneScale() - 1.0) < 0.001);
    QVERIFY(!overlay->isVisible());
    QVERIFY(!layersLayer->property("enabled").toBool());
    QCOMPARE(lipstick.transforms("layersParent"), 0);

    // The long press on the strip, held: out down to 1x, where the zoom
    // ends; input again; the next zoom starts in pan mode
    hold(QPoint(500, strip));
    QTest::touchEvent(window, device).move(0, QPoint(500, strip - doubling), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip - doubling), window);
    QVERIFY(zoom->zoomed() && !zoom->touchMode());
    hold(QPoint(500, strip));
    QTRY_VERIFY(!zoom->zoomed());
    QTest::touchEvent(window, device).release(0, QPoint(500, strip), window);
    QVERIFY(!zoom->zoomed());
    QTest::touchEvent(window, device).press(0, QPoint(500, 1000), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, 1000), window);
    QCOMPARE(recorder.positions.count(), 2);

    // The keyboard ends the zoom: 1x, input as usual; closed, the zoom stays
    // off
    hold(QPoint(500, strip));
    QTest::touchEvent(window, device).move(0, QPoint(500, strip - doubling), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip - doubling), window);
    QVERIFY(zoom->level() > 1.5);
    window->setProperty("keyboardHeight", 600);
    QVERIFY(!zoom->zoomed());
    QVERIFY(qAbs(sceneScale() - 1.0) < 0.001);
    QVERIFY(!overlay->isVisible());
    recorder.positions.clear();
    QTest::touchEvent(window, device).press(0, QPoint(500, 1000), window);
    QCOMPARE(recorder.positions.count(), 1);
    QTest::touchEvent(window, device).move(0, QPoint(400, 900), window);
    QTest::touchEvent(window, device).release(0, QPoint(400, 900), window);
    QCOMPARE(recorder.positions.count(), 3);
    window->setProperty("keyboardHeight", 0);
    QTest::qWait(300);
    QVERIFY(!zoom->zoomed());

    // A dialog (an incoming call) ends it by itself, and stops it starting
    hold(QPoint(500, strip));
    QTest::touchEvent(window, device).move(0, QPoint(500, strip - doubling), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip - doubling), window);
    QVERIFY(zoom->zoomed());
    lipstick.item("dialogs")->setProperty("exposed", true);
    QTRY_VERIFY(!zoom->zoomed());
    QVERIFY(qAbs(sceneScale() - 1.0) < 0.001);
    recorder.positions.clear();
    hold(QPoint(500, strip));
    QTest::touchEvent(window, device).release(0, QPoint(500, strip), window);
    QCOMPARE(recorder.positions.count(), 2);
    lipstick.item("dialogs")->setProperty("exposed", false);

    // Rotation too
    hold(QPoint(500, strip));
    QTest::touchEvent(window, device).move(0, QPoint(500, strip - doubling), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip - doubling), window);
    QVERIFY(zoom->zoomed());
    window->setProperty("topmostWindowAngle", 90);
    QVERIFY(!zoom->zoomed());
    window->setProperty("topmostWindowAngle", 0);

    // Not with the keyboard open: its space bar row is on the strip
    window->setProperty("keyboardHeight", 600);
    recorder.positions.clear();
    hold(QPoint(500, strip));
    QTest::touchEvent(window, device).release(0, QPoint(500, strip), window);
    QCOMPARE(recorder.positions.count(), 2);
    QVERIFY(!zoom->zoomed());
    window->setProperty("keyboardHeight", 0);
    window->removeEventFilter(&recorder);
}

void TestGestures::zoomInPicture()
{
    // With one-handed mode pulled, the zoom stays inside its picture
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    lipstick.showApplication();
    Modes modes(window, true);
    FrogfishSettings::Values values = pullValues();
    values.zoom = true;
    modes.configure(values);
    ZoomMode *zoom = modes.zoom.data();
    QTouchDevice *device = m_device;
    QTest::touchEvent(window, device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, device).release(0, Corner, window);
    QTRY_COMPARE(lipstick.item("app")->y(), 800.0);
    const QRectF picture(0, 800, 1000, 1200);
    QCOMPARE(zoom->picture(), picture);
    TouchRecorder recorder;
    window->installEventFilter(&recorder);
    auto near = [](const QPointF &a, const QPointF &b) {
        return qAbs(a.x() - b.x()) < 0.01 && qAbs(a.y() - b.y()) < 0.01;
    };
    auto tap = [&](const QPoint &at) {
        QTest::touchEvent(window, device).press(0, at, window);
        QTest::touchEvent(window, device).release(0, at, window);
    };
    auto drag = [&](const QPoint &from, const QPoint &to) {
        QTest::touchEvent(window, device).press(0, from, window);
        QTest::touchEvent(window, device).move(0, to, window);
        QTest::touchEvent(window, device).release(0, to, window);
    };
    // Where the scene lands: the layers' parent has the zoom's transforms
    // only (the wallpaper in it is also moved down)
    QQuickItem *layers = lipstick.item("layersParent");
    auto zoomOffset = [layers]() { return layers->mapToScene(QPointF(0, 0)); };

    // Around the picture's middle
    const int strip = 1998;
    const int doubling = qRound(20 * zoom->mm());
    QTest::touchEvent(window, device).press(0, QPoint(500, strip), window);
    QTest::qWait(600);
    QTest::touchEvent(window, device).move(0, QPoint(500, strip - doubling), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip - doubling), window);
    QVERIFY(zoom->zoomed());
    const qreal level = zoom->level();
    QVERIFY(near(zoomOffset(), QPointF(500 - 500 * level, 1400 - 1400 * level)));
    // The black around it and the border are the overlay's, unmagnified
    QQuickItem *controls = zoom->findChild<QQuickItem *>(QStringLiteral("frogfish-zoom-controls"));
    QVERIFY(controls);
    QCOMPARE(controls->property("picture").toRectF(), picture);

    // The pan stops at the picture's edges: its top stays at its top, never
    // magnified black from above it
    for (int i = 0; i < 3; ++i)
        drag(QPoint(100, 900), QPoint(900, 1900));
    QVERIFY(near(zoomOffset(), QPointF(0, 800 - 800 * level)));
    for (int i = 0; i < 3; ++i)
        drag(QPoint(900, 1900), QPoint(100, 900));
    QVERIFY(near(zoomOffset(), QPointF(1000 - 1000 * level, 2000 - 2000 * level)));

    // Pan mode: a drag that starts on the black still pans; a tap there ends
    // the zoom, and nothing reaches the app
    QPointF before = zoomOffset();
    drag(QPoint(500, 400), QPoint(560, 480));
    QVERIFY(near(zoomOffset(), before + QPointF(60, 80)));
    recorder.positions.clear();
    tap(QPoint(500, 400));
    QVERIFY(!zoom->zoomed());
    QCOMPARE(recorder.positions.count(), 0);
    QCOMPARE(lipstick.item("app")->y(), 800.0);

    // Touch mode: a tap on the picture reaches the app; one on the black
    // ends the zoom and reaches nothing
    QTest::touchEvent(window, device).press(0, QPoint(500, strip), window);
    QTest::qWait(600);
    QTest::touchEvent(window, device).move(0, QPoint(500, strip - doubling), window);
    QTest::touchEvent(window, device).release(0, QPoint(500, strip - doubling), window);
    QVERIFY(zoom->zoomed());
    const QRectF button = zoom->buttonRect();
    QVERIFY(picture.contains(button));
    tap(QPoint(qRound(button.center().x()), qRound(button.bottom() - button.height() / 4)));
    QVERIFY(zoom->touchMode());
    recorder.positions.clear();
    tap(QPoint(500, 1200));
    QCOMPARE(recorder.positions.count(), 2);
    tap(QPoint(500, 400));
    QVERIFY(!zoom->zoomed());
    QCOMPARE(recorder.positions.count(), 2);
    QCOMPARE(lipstick.item("app")->y(), 800.0);
    // The next tap on the black ends one-handed mode
    tap(QPoint(500, 400));
    QTRY_COMPARE(lipstick.item("app")->y(), 0.0);
    QCOMPARE(zoom->picture(), QRectF(0, 0, 1000, 2000));
    window->removeEventFilter(&recorder);
}

void TestGestures::zoomOutSteadily()
{
    // Zoomed: the strip's hold zooms out, halving every second, while it
    // lasts; a drag up from the strip zooms in at once
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    Modes modes(window, true);
    FrogfishSettings::Values values = pullValues();
    values.mode = FrogfishSettings::None;
    values.zoom = true;
    modes.configure(values);
    ZoomMode *zoom = modes.zoom.data();
    TouchRecorder recorder;
    window->installEventFilter(&recorder);
    const int doubling = qRound(20 * zoom->mm());
    const QPoint strip(500, 1998);
    auto holdAndDrag = [&](int up) {
        QTest::touchEvent(window, m_device).press(0, strip, window);
        QTest::qWait(600);
        QTest::touchEvent(window, m_device).move(0, strip - QPoint(0, up), window);
        QTest::touchEvent(window, m_device).release(0, strip - QPoint(0, up), window);
    };
    // From off as before: the hold, then up
    holdAndDrag(2 * doubling);
    QVERIFY(qAbs(zoom->level() - 4) < 0.1);
    const qreal start = zoom->level();

    // The hold: out from the vibration (not before), and the level stays
    // when let go
    QTest::touchEvent(window, m_device).press(0, strip, window);
    QTest::qWait(400);
    QCOMPARE(zoom->level(), start);
    QTest::qWait(700);
    const qreal out = zoom->level();
    QVERIFY2(out < start * 0.85 && out > start * 0.5, qPrintable(QString::number(out)));
    QTest::touchEvent(window, m_device).release(0, strip, window);
    QVERIFY(zoom->zoomed());
    QCOMPARE(zoom->level(), out);

    // ... dragged up during it: from the level reached
    QTest::touchEvent(window, m_device).press(0, strip, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).move(0, strip - QPoint(0, 15), window);
    const qreal stopped = zoom->level();
    QTest::qWait(300);
    QCOMPARE(zoom->level(), stopped);
    QTest::touchEvent(window, m_device).move(0, strip - QPoint(0, 15 + doubling), window);
    QVERIFY(qAbs(zoom->level() - 2 * stopped) < 0.05);
    QTest::touchEvent(window, m_device).release(0, strip - QPoint(0, 15 + doubling), window);
    QVERIFY(zoom->zoomed());

    // Pan mode: a drag up from the strip zooms in at once, sideways it pans
    const qreal before = zoom->level();
    QTest::touchEvent(window, m_device).press(0, strip, window);
    QTest::touchEvent(window, m_device).move(0, strip - QPoint(0, doubling), window);
    QVERIFY(qAbs(zoom->level() - 2 * before) < 0.05 || zoom->level() == values.zoomMaximum);
    QTest::touchEvent(window, m_device).release(0, strip - QPoint(0, doubling), window);
    const qreal raised = zoom->level();
    QTest::touchEvent(window, m_device).press(0, strip, window);
    QTest::touchEvent(window, m_device).move(0, strip + QPoint(-200, -20), window);
    QTest::touchEvent(window, m_device).release(0, strip + QPoint(-200, -20), window);
    QCOMPARE(zoom->level(), raised);
    QCOMPARE(recorder.positions.count(), 0);

    // Touch mode: the strip's swipe up is the app's; the hold still zooms out
    const QRectF button = zoom->buttonRect();
    const QPoint touchHalf(qRound(button.center().x()), qRound(button.bottom() - button.height() / 4));
    QTest::touchEvent(window, m_device).press(0, touchHalf, window);
    QTest::touchEvent(window, m_device).release(0, touchHalf, window);
    QVERIFY(zoom->touchMode());
    QTest::touchEvent(window, m_device).press(0, strip, window);
    QTest::touchEvent(window, m_device).move(0, strip - QPoint(0, doubling), window);
    QTest::touchEvent(window, m_device).release(0, strip - QPoint(0, doubling), window);
    QCOMPARE(zoom->level(), raised);
    QCOMPARE(recorder.positions.count(), 3);

    // Held down to 1x: the zoom ends by itself, the rest of the touch is
    // swallowed
    recorder.positions.clear();
    QTest::touchEvent(window, m_device).press(0, strip, window);
    QTRY_VERIFY_WITH_TIMEOUT(!zoom->zoomed(), 6000);
    QTest::touchEvent(window, m_device).release(0, strip, window);
    QCOMPARE(recorder.positions.count(), 0);
    window->removeEventFilter(&recorder);
}

void TestGestures::back()
{
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    lipstick.showApplication();
    QQuickItem *launcher = lipstick.item("launcher");
    QQuickItem *topMenu = lipstick.item("topMenuLayer");
    TouchRecorder recorder;
    window->installEventFilter(&recorder);
    RightClicks clicks;
    window->installEventFilter(&clicks);
    // Lipstick's side strips, 20 px
    window->setProperty("peekBoundaryWidth", 20);
    Modes modes(window);
    BackMode *back = modes.back.data();
    const qreal mm = Units::pixelsPerMm(window);
    const QPoint edge(10, 1000);
    auto hold = [&](const QPoint &at, int ms) {
        QTest::touchEvent(window, m_device).press(0, at, window);
        QTest::qWait(ms);
        QTest::touchEvent(window, m_device).release(0, at, window);
        QTest::qWait(20);
    };

    // Off by default: the edge's long press reaches the window
    modes.configure(pullValues());
    QCOMPARE(back->diagnostics(), QStringLiteral("back: off"));
    hold(edge, 700);
    QCOMPARE(recorder.positions.count(), 2);
    QCOMPARE(clicks.count, 0);

    FrogfishSettings::Values values = pullValues();
    values.back = true;
    modes.configure(values);
    QCOMPARE(back->diagnostics(), QStringLiteral("back: on, left edge, last on"));
    // A tap and a swipe from the edge are lipstick's, whole and in order
    recorder.positions.clear();
    hold(edge, 0);
    QCOMPARE(recorder.positions.count(), 2);
    recorder.positions.clear();
    QTest::touchEvent(window, m_device).press(0, edge, window);
    QTest::touchEvent(window, m_device).move(0, QPoint(300, 1000), window);
    QTest::touchEvent(window, m_device).release(0, QPoint(300, 1000), window);
    QCOMPARE(recorder.positions.count(), 3);
    QCOMPARE(recorder.positions.at(0), QPointF(edge));
    QCOMPARE(recorder.positions.at(1), QPointF(300, 1000));
    QCOMPARE(clicks.count, 0);

    // The hold, lifted in time: lipstick's back; nothing reaches the window
    recorder.positions.clear();
    QSignalSpy inUse(back, &BackMode::inUseChanged);
    hold(edge, 700);
    QCOMPARE(recorder.positions.count(), 0);
    QCOMPARE(clicks.count, 1);
    QCOMPARE(inUse.count(), 1);
    QVERIFY(back->inUse());
    QTRY_VERIFY(!back->inUse());
    QCOMPARE(back->diagnostics(), QStringLiteral("back: on, left edge, last back"));
    // Held too long (a grip), or moved after the hold: nothing; the ring
    // still follows the finger while it plays
    hold(edge, 1700);
    QTest::touchEvent(window, m_device).press(0, edge, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).move(0, QPoint(10, 1100), window);
    QQuickItem *ring = playingRing(window);
    QVERIFY(ring);
    QCOMPARE(centre(ring), QPointF(10, 1100));
    QTest::touchEvent(window, m_device).release(0, QPoint(10, 1100), window);
    QTest::qWait(20);
    QCOMPARE(clicks.count, 1);
    QCOMPARE(recorder.positions.count(), 0);
    QVERIFY(!back->inUse());
    // Off the strip, or near its ends: not back's
    hold(QPoint(40, 1000), 700);
    hold(QPoint(10, qRound(5 * mm)), 700);
    QCOMPARE(recorder.positions.count(), 4);
    QCOMPARE(clicks.count, 1);

    // The keyboard open: back too (Android closes the keyboard, Silica goes
    // back a page), above the keyboard only
    recorder.positions.clear();
    window->setProperty("keyboardHeight", 500);
    hold(edge, 700);
    QCOMPARE(recorder.positions.count(), 0);
    QCOMPARE(clicks.count, 2);
    hold(QPoint(10, 1700), 700);
    QCOMPARE(recorder.positions.count(), 2);
    QCOMPARE(clicks.count, 2);
    window->setProperty("keyboardHeight", 0);
    // Lipstick's back off (its banner, its dialogs): the edge is lipstick's
    recorder.positions.clear();
    window->setProperty("synthesizeBackEvent", false);
    hold(edge, 700);
    QCOMPARE(recorder.positions.count(), 2);
    window->setProperty("synthesizeBackEvent", true);

    // The app grid and the top menu are closed, not gone back from
    launcher->setProperty("exposed", true);
    hold(edge, 700);
    QCOMPARE(launcher->property("hidden").toInt(), 1);
    topMenu->setProperty("exposed", true);
    hold(edge, 700);
    QCOMPARE(topMenu->property("hidden").toInt(), 1);
    QCOMPARE(clicks.count, 2);
    QCOMPARE(back->diagnostics(), QStringLiteral("back: on, left edge, last top menu closed"));
    // Home and the lock screen: lipstick's
    recorder.positions.clear();
    lipstick.showHome();
    hold(edge, 700);
    lipstick.showApplication();
    lipstick.item("lockScreen")->setProperty("exposed", true);
    hold(edge, 700);
    lipstick.item("lockScreen")->setProperty("exposed", false);
    QCOMPARE(recorder.positions.count(), 4);
    QCOMPARE(clicks.count, 2);

    // Left-handed: the right edge; the left one is one-handed mode's
    values.leftHanded = true;
    modes.configure(values);
    hold(QPoint(990, 1000), 700);
    QCOMPARE(clicks.count, 3);
    hold(edge, 700);
    QCOMPARE(clicks.count, 3);
}

void TestGestures::pullShrink()
{
    // The same top as Pull and Shrink, the app shrunk by the square root of
    // the size; along the edge the app's point under the finger stays under
    // it
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    QQuickItem *app = lipstick.item("app");
    lipstick.showApplication();
    auto edgeDrag = [&](int from, int via, int to) {
        QTest::touchEvent(window, m_device).press(0, QPoint(998, from), window);
        QTest::qWait(600);
        QTest::touchEvent(window, m_device).move(0, QPoint(998, via), window);
        QTest::touchEvent(window, m_device).move(0, QPoint(998, to), window);
        QTest::touchEvent(window, m_device).release(0, QPoint(998, to), window);
    };
    // Where the app's point at that height is on the screen
    auto drawnAt = [&](qreal y) { return app->mapToScene(QPointF(500, y)).y(); };
    Modes modes(window);
    OneHandedMode *oneHanded = modes.oneHanded.data();
    FrogfishSettings::Values values = pullValues();
    values.mode = FrogfishSettings::PullShrink;
    modes.configure(values);

    // The corner: the size set (60 %), top at 40 % of the screen, scale
    // sqrt(0.6), the far side covered
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QTRY_COMPARE(app->y(), 800.0);
    QVERIFY(qFuzzyCompare(app->scale(), std::sqrt(qreal(0.6))));
    QCOMPARE(app->transformOrigin(), QQuickItem::TopRight);
    QCOMPARE(oneHanded->diagnostics(), QStringLiteral("one-handed: on, pulled and shrunk to 60 %, in front application"));
    const qreal width = 1000 * std::sqrt(qreal(0.6));
    QVERIFY(qAbs(oneHanded->picture().left() - (1000 - width)) < 0.01);
    QCOMPARE(oneHanded->picture().top(), 800.0);
    QCOMPARE(oneHanded->picture().bottom(), 2000.0);
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QTRY_COMPARE(app->y(), 0.0);
    QCOMPARE(app->scale(), 1.0);

    // From off, a hold and a drag down: the app's point at 1000 follows the
    // finger to 1300 (within the offset's rounding)
    edgeDrag(1000, 1100, 1300);
    QTRY_VERIFY(app->scale() < 1);
    QVERIFY2(qAbs(drawnAt(1000) - 1300) < 1, qPrintable(QString::number(drawnAt(1000))));
    QCOMPARE(oneHanded->diagnostics(), QStringLiteral("one-handed: on, pulled and shrunk to 80 %, in front application"));
    // Further, from the current size: the point under the finger follows
    const qreal under = (1200 - app->y()) / app->scale();
    edgeDrag(1200, 1300, 1500);
    QVERIFY2(qAbs(drawnAt(under) - 1500) < 1, qPrintable(QString::number(drawnAt(under))));
    QVERIFY(qAbs(app->scale() * app->scale() - (2000 - app->y()) / 2000) < 0.001);
    // ... not below the fixed smallest (45 %)
    edgeDrag(1500, 1600, 1990);
    QTRY_COMPARE(app->y(), 1100.0);
    QVERIFY(qFuzzyCompare(app->scale(), std::sqrt(qreal(0.45))));

    // The keyboard open: the same scale (the width stays), the app and the
    // keyboard from the bottom corner; the top rises
    QQuickItem *keyboard = lipstick.item("keyboard");
    window->setProperty("keyboardHeight", 500);
    QTRY_COMPARE(app->y(), 0.0);
    QVERIFY(qFuzzyCompare(app->scale(), std::sqrt(qreal(0.45))));
    QCOMPARE(app->transformOrigin(), QQuickItem::BottomRight);
    QVERIFY(qFuzzyCompare(keyboard->scale(), std::sqrt(qreal(0.45))));
    // The edge above the keyboard keeps the point under the finger, from the
    // bottom corner
    const qreal above = 2000 - (2000 - 800) / app->scale();
    edgeDrag(800, 750, 700);
    QVERIFY2(qAbs(drawnAt(above) - 700) < 1, qPrintable(QString::number(drawnAt(above))));
    const qreal typing = app->scale();
    QVERIFY(typing > std::sqrt(qreal(0.45)));
    // Closed: the same scale, the top back where the size puts it
    window->setProperty("keyboardHeight", 0);
    QTRY_COMPARE(app->transformOrigin(), QQuickItem::TopRight);
    QCOMPARE(app->scale(), typing);
    QCOMPARE(app->y(), std::round(2000 * (1 - typing * typing)));
}

void TestGestures::routerOrder()
{
    // While zoomed, one-handed mode sees no touch: its positions wouldn't
    // match the picture
    Compositor lipstick;
    QQuickWindow *window = lipstick.window;
    lipstick.showApplication();
    QQuickItem *app = lipstick.item("app");
    Modes modes(window, true);
    FrogfishSettings::Values values = shrinkValues();
    values.zoom = true;
    modes.configure(values);
    TouchRecorder recorder;
    window->installEventFilter(&recorder);
    QTest::touchEvent(window, m_device).press(0, QPoint(500, 1998), window);
    QTest::qWait(600);
    QTest::touchEvent(window, m_device).move(0, QPoint(500, 1800), window);
    QTest::touchEvent(window, m_device).release(0, QPoint(500, 1800), window);
    QVERIFY(modes.zoom->zoomed());
    // Touch mode: the edge's hold and drag goes to the app
    const QRectF button = modes.zoom->buttonRect();
    const QPoint touchHalf(qRound(button.center().x()), qRound(button.bottom() - button.height() / 4));
    QTest::touchEvent(window, m_device).press(0, touchHalf, window);
    QTest::touchEvent(window, m_device).release(0, touchHalf, window);
    QVERIFY(modes.zoom->touchMode());
    recorder.positions.clear();
    QTest::touchEvent(window, m_device).press(0, QPoint(998, 1000), window);
    QTest::qWait(600);
    QTest::touchEvent(window, m_device).move(0, QPoint(998, 1100), window);
    QTest::touchEvent(window, m_device).move(0, QPoint(998, 1200), window);
    QTest::touchEvent(window, m_device).release(0, QPoint(998, 1200), window);
    QCOMPARE(recorder.positions.count(), 4);
    QCOMPARE(app->scale(), 1.0);
    // Not zoomed, the edge is one-handed mode's again
    values.zoom = false;
    modes.configure(values);
    QTest::touchEvent(window, m_device).press(0, QPoint(998, 1000), window);
    QTest::qWait(600);
    QTest::touchEvent(window, m_device).move(0, QPoint(998, 1100), window);
    QTest::touchEvent(window, m_device).move(0, QPoint(998, 1200), window);
    QTest::touchEvent(window, m_device).release(0, QPoint(998, 1200), window);
    QTRY_COMPARE(app->scale(), 0.8);
    QCOMPARE(recorder.positions.count(), 4);
    window->removeEventFilter(&recorder);
}

void TestGestures::controllerModes()
{
    {
        // Attached to the compositor: every mode, one-handed on by default
        Compositor lipstick;
        QTemporaryDir dir;
        FrogfishController controller("QQuickWindow", dir.path());
        QTRY_VERIFY(controller.oneHanded());
        QVERIFY(controller.zoom());
        QVERIFY(controller.back());
        QCOMPARE(controller.modeStatus(QStringLiteral("onehanded")), QStringLiteral("on"));
        // Zoom and back are off by default
        QCOMPARE(controller.modeStatus(QStringLiteral("zoom")), QStringLiteral("off"));
        QCOMPARE(controller.modeStatus(QStringLiteral("back")), QStringLiteral("off"));
        const QString diagnostics = controller.diagnostics();
        QVERIFY(diagnostics.contains(QStringLiteral("\nlipstick: missing nothing\n")));
        QVERIFY(diagnostics.contains(QStringLiteral("\none-handed: off, waiting for the corner or the edge, in front home\n")));
        QVERIFY(diagnostics.contains(QStringLiteral("\nzoom: off")));
        // The last line: no log in the diagnostics
        QVERIFY(diagnostics.endsWith(QStringLiteral("\nback: off")));
    }
    // A window without lipstick's properties: all stay off, and say why
    qputenv("FROGFISH_ZOOM", "on");
    qputenv("FROGFISH_BACK", "on");
    QQuickWindow bare;
    QTemporaryDir dir;
    FrogfishController controller("QQuickWindow", dir.path());
    QTRY_COMPARE(controller.window(), &bare);
    QCOMPARE(controller.modeStatus(QStringLiteral("onehanded")), QStringLiteral("unsupported"));
    QCOMPARE(controller.modeStatus(QStringLiteral("zoom")), QStringLiteral("unsupported"));
    QCOMPARE(controller.modeStatus(QStringLiteral("back")), QStringLiteral("unsupported"));
    QVERIFY(controller.diagnostics().contains(QStringLiteral("\nlipstick: missing alarmLayer, appLayer")));
    // Its setting off says off
    qputenv("FROGFISH_ZOOM", "0");
    QCOMPARE(controller.modeStatus(QStringLiteral("zoom")), QStringLiteral("off"));
}

void TestGestures::controllerSettingsAfterTouch()
{
    // A setting changed during a touch applies once it ends
    Compositor lipstick;
    lipstick.showApplication();
    QQuickWindow *window = lipstick.window;
    QQuickItem *app = lipstick.item("app");
    QTemporaryDir dir;
    FrogfishController controller("QQuickWindow", dir.path());
    QTRY_VERIFY(controller.oneHanded());
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QTRY_COMPARE(app->y(), 800.0);
    QTest::touchEvent(window, m_device).press(0, QPoint(500, 1500), window);
    qputenv("FROGFISH_ONEHANDED_MODE", "none");
    controller.reload();
    QTest::qWait(50);
    QCOMPARE(app->y(), 800.0);
    QTest::touchEvent(window, m_device).release(0, QPoint(500, 1500), window);
    QTRY_COMPARE(app->y(), 0.0);
    // Between touches at once
    qunsetenv("FROGFISH_ONEHANDED_MODE");
    controller.reload();
    QTest::qWait(20);
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QTRY_COMPARE(app->y(), 800.0);
    qputenv("FROGFISH_ONEHANDED_MODE", "none");
    controller.reload();
    QTRY_COMPARE(app->y(), 0.0);
}

void TestGestures::controllerInUse()
{
    // A mode is marked in use while it has changed lipstick
    Compositor lipstick;
    lipstick.showApplication();
    QQuickWindow *window = lipstick.window;
    QQuickItem *app = lipstick.item("app");
    QTemporaryDir dir;
    const QString oneHandedMark = dir.path() + QStringLiteral("/in-use-onehanded");
    const QString zoomMark = dir.path() + QStringLiteral("/in-use-zoom");
    qputenv("FROGFISH_ZOOM", "on");
    FrogfishController controller("QQuickWindow", dir.path());
    QTRY_VERIFY(controller.oneHanded());
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QTRY_COMPARE(app->y(), 800.0);
    QVERIFY(QFile::exists(oneHandedMark));
    QTest::touchEvent(window, m_device).press(0, QPoint(500, 1998), window);
    QTest::qWait(600);
    QVERIFY(QFile::exists(zoomMark));
    QTest::touchEvent(window, m_device).release(0, QPoint(500, 1998), window);
    QVERIFY(!QFile::exists(zoomMark));
    QTest::touchEvent(window, m_device).press(0, QPoint(500, 300), window);
    QTest::touchEvent(window, m_device).release(0, QPoint(500, 300), window);
    QTRY_COMPARE(app->y(), 0.0);
    QVERIFY(!QFile::exists(oneHandedMark));
    // Back: from the hold until lipstick's response is through
    const QString backMark = dir.path() + QStringLiteral("/in-use-back");
    qputenv("FROGFISH_BACK", "on");
    controller.reload();
    QTest::qWait(20);
    QTest::touchEvent(window, m_device).press(0, QPoint(10, 1000), window);
    QTest::qWait(600);
    QVERIFY(QFile::exists(backMark));
    QTest::touchEvent(window, m_device).release(0, QPoint(10, 1000), window);
    QTRY_VERIFY(!QFile::exists(backMark));
}

void TestGestures::controllerTopMenu()
{
    // The state goes to onehanded_active; the switch's write is a request
    Compositor lipstick;
    lipstick.showApplication();
    QQuickWindow *window = lipstick.window;
    QQuickItem *app = lipstick.item("app");
    QTemporaryDir dir;
    FrogfishController controller("QQuickWindow", dir.path());
    FrogfishSettings *settings = controller.settings();
    QTRY_VERIFY(controller.oneHanded());
    QVERIFY(!settings->oneHandedActive());
    settings->requestOneHandedActive(true);
    QTRY_COMPARE(app->y(), 800.0);
    QVERIFY(settings->oneHandedActive());
    // The corner ends it: the switch shows it
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QTRY_COMPARE(app->y(), 0.0);
    QVERIFY(!settings->oneHandedActive());
    // Asked for during a touch: once it ends
    QTest::touchEvent(window, m_device).press(0, QPoint(500, 1500), window);
    settings->requestOneHandedActive(true);
    QTest::qWait(50);
    QCOMPARE(app->y(), 0.0);
    QTest::touchEvent(window, m_device).release(0, QPoint(500, 1500), window);
    QTRY_COMPARE(app->y(), 800.0);
    settings->requestOneHandedActive(false);
    QTRY_COMPARE(app->y(), 0.0);
    // Refused (mode none): the switch is put right
    qputenv("FROGFISH_ONEHANDED_MODE", "none");
    controller.reload();
    QTest::qWait(20);
    settings->requestOneHandedActive(true);
    QTRY_VERIFY(!settings->oneHandedActive());
    QCOMPARE(app->y(), 0.0);
}

void TestGestures::controllerCrashGuard()
{
    // Three crashes while pulled: one-handed mode stays off, zoom works
    QTemporaryDir dir;
    for (int i = 0; i < 3; ++i) {
        UseGuard(dir.path()).begin(QStringLiteral("onehanded"));
        UseGuard(dir.path()).recover();
    }
    Compositor lipstick;
    lipstick.showApplication();
    QQuickWindow *window = lipstick.window;
    QQuickItem *app = lipstick.item("app");
    qputenv("FROGFISH_ZOOM", "on");
    FrogfishController controller("QQuickWindow", dir.path());
    QTRY_VERIFY(controller.oneHanded());
    QCOMPARE(controller.modeStatus(QStringLiteral("onehanded")), QStringLiteral("crash-guard"));
    QCOMPARE(controller.modeStatus(QStringLiteral("zoom")), QStringLiteral("on"));
    TouchRecorder recorder;
    window->installEventFilter(&recorder);
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QCOMPARE(recorder.positions.count(), 2);
    QCOMPARE(app->y(), 0.0);
    // Try again
    controller.resetCrashGuard();
    QCOMPARE(controller.modeStatus(QStringLiteral("onehanded")), QStringLiteral("on"));
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QTRY_COMPARE(app->y(), 800.0);
    window->removeEventFilter(&recorder);
}

void TestGestures::controllerDetachOnQuit()
{
    // Before lipstick quits, everything is put back while its scene is there
    Compositor lipstick;
    lipstick.showApplication();
    QQuickWindow *window = lipstick.window;
    QQuickItem *app = lipstick.item("app");
    QTemporaryDir dir;
    FrogfishController controller("QQuickWindow", dir.path());
    QTRY_VERIFY(controller.oneHanded());
    QTest::touchEvent(window, m_device).press(0, Corner, window);
    QTest::qWait(700);
    QTest::touchEvent(window, m_device).release(0, Corner, window);
    QTRY_COMPARE(app->y(), 800.0);
    // Back's hold armed when lipstick quits
    qputenv("FROGFISH_BACK", "on");
    controller.reload();
    QTest::qWait(20);
    QTest::touchEvent(window, m_device).press(0, QPoint(10, 1000), window);
    QTest::qWait(600);
    QVERIFY(controller.back()->inUse());
    QVERIFY(QMetaObject::invokeMethod(qApp, "aboutToQuit"));
    QTest::touchEvent(window, m_device).release(0, QPoint(10, 1000), window);
    QCOMPARE(app->y(), 0.0);
    QVERIFY(!controller.oneHanded());
    QVERIFY(!controller.zoom());
    QVERIFY(!controller.back());
    QVERIFY(!QFile::exists(dir.path() + QStringLiteral("/in-use-onehanded")));
    QVERIFY(!QFile::exists(dir.path() + QStringLiteral("/in-use-back")));
    QCOMPARE(lipstick.transforms("wallpaper"), 0);
}

QTEST_MAIN(TestGestures)

#include "tst_gestures.moc"
