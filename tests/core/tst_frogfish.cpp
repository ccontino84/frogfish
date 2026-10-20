// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "controller.h"
#include "crashguard.h"
#include "log.h"
#include "settings.h"
#include "statusservice.h"
#include <frogfish_version.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QGenericPluginFactory>
#include <QProcess>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// Unset every setting, so each test starts from the defaults
void clearSettings()
{
    for (const char *key : { "FROGFISH_ONEHANDED_MODE", "FROGFISH_ONEHANDED_SIZE", "FROGFISH_ZOOM",
                             "FROGFISH_ZOOM_MAX", "FROGFISH_BACK", "FROGFISH_HAND" })
        qunsetenv(key);
}

void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}

class TestFrogfish : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void settingsDefaults();
    void settingsMatchPage();
    void settingsParsing();
    void settingsReload();

    void crashGuard();
    void useGuard();
    void useGuardAfterShutdown();
    void log();

    void controllerAttaches();
    void controllerModeStatus();
    void controllerCrashGuard();
    void controllerRecoversUse();
    void statusService();

    void pluginOnlyInCompositor();
    void loaderStartsAddons();

    void updateEnv_data();
    void updateEnv();
    void updateEnvKeepsOwnFileOutOfTheCheck();

private:
    QTemporaryDir m_cache;
};

void TestFrogfish::initTestCase()
{
    QVERIFY(m_cache.isValid());
    // The plugin's own copy of the code writes its crash guard and log
    // under the cache: never the real one
    qputenv("XDG_CACHE_HOME", m_cache.path().toUtf8());
    FrogfishLog::setFilePath(m_cache.path() + QStringLiteral("/test-log.txt"));
    QCoreApplication::addLibraryPath(QStringLiteral(FROGFISH_PLUGINS_DIR));
}

void TestFrogfish::init()
{
    clearSettings();
}

void TestFrogfish::settingsDefaults()
{
    FrogfishSettings settings;
    QCOMPARE(settings.oneHanded(), true);
    QCOMPARE(settings.mode(), FrogfishSettings::Pull);
    QCOMPARE(settings.size(), qreal(0.6));
    QCOMPARE(settings.zoom(), false);
    QCOMPARE(settings.zoomMaximum(), qreal(8));
    QCOMPARE(settings.back(), false);
    QCOMPARE(settings.leftHanded(), false);
}

void TestFrogfish::settingsMatchPage()
{
    // defaults.js is the page's copy of the defaults
    const QString js = QString::fromUtf8(readFile(QStringLiteral(FROGFISH_DEFAULTS_JS)));
    QVERIFY(!js.isEmpty());
    QMap<QString, QString> page;
    QRegularExpression entry(QStringLiteral("^\\s*(\\w+):\\s*\"?([^\",]*)\"?,?\\s*$"),
                             QRegularExpression::MultilineOption);
    for (QRegularExpressionMatchIterator it = entry.globalMatch(js); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        page.insert(match.captured(1), match.captured(2));
    }
    FrogfishSettings settings;
    QCOMPARE(page.value("onehanded_mode"), QString(settings.mode() == FrogfishSettings::Shrink ? "shrink" : "pull"));
    QCOMPARE(page.value("onehanded_size").toInt(), qRound(settings.size() * 100));
    QCOMPARE(page.value("zoom"), QString(settings.zoom() ? "true" : "false"));
    QCOMPARE(page.value("back"), QString(settings.back() ? "true" : "false"));
    QCOMPARE(page.value("hand"), QString(settings.leftHanded() ? "left" : "right"));
    QCOMPARE(page.count(), 5);
}

void TestFrogfish::settingsParsing()
{
    // One parser for every key
    for (const char *text : { "false", "0", "off", "no", "", " OFF " })
        QVERIFY2(!FrogfishSettings::parseBool(QString::fromLatin1(text), true), text);
    for (const char *text : { "true", "1", "on", "yes" })
        QVERIFY2(FrogfishSettings::parseBool(QString::fromLatin1(text), false), text);
    QCOMPARE(FrogfishSettings::parseBool(QVariant(), true), true);
    QCOMPARE(FrogfishSettings::parseBool(QVariant(false), true), false);

    QCOMPARE(FrogfishSettings::parseInt(QStringLiteral("30"), 60, 45, 75), 45);
    QCOMPARE(FrogfishSettings::parseInt(QStringLiteral("90"), 60, 45, 75), 75);
    QCOMPARE(FrogfishSettings::parseInt(QStringLiteral("x"), 60, 45, 75), 60);
    QCOMPARE(FrogfishSettings::parseReal(QStringLiteral("20"), 8, 1.5, 15), qreal(15));

    qputenv("FROGFISH_ZOOM", "on");
    qputenv("FROGFISH_ONEHANDED_MODE", "Shrink");
    qputenv("FROGFISH_ONEHANDED_SIZE", "50");
    qputenv("FROGFISH_HAND", "left");
    qputenv("FROGFISH_BACK", "yes");
    FrogfishSettings settings;
    QCOMPARE(settings.oneHanded(), true);
    QCOMPARE(settings.zoom(), true);
    QCOMPARE(settings.back(), true);
    QVERIFY(settings.values().back);
    QCOMPARE(settings.mode(), FrogfishSettings::Shrink);
    QCOMPARE(settings.size(), qreal(0.5));
    qputenv("FROGFISH_ONEHANDED_SIZE", "95");
    QCOMPARE(settings.size(), qreal(0.75));
    qputenv("FROGFISH_ONEHANDED_SIZE", "30");
    QCOMPARE(settings.size(), qreal(0.45));
    QCOMPARE(settings.leftHanded(), true);
    QVERIFY(settings.summary().contains(QStringLiteral("one-handed shrink")));
    qputenv("FROGFISH_ONEHANDED_MODE", " None ");
    QCOMPARE(settings.mode(), FrogfishSettings::None);
    QCOMPARE(settings.oneHanded(), false);
    qputenv("FROGFISH_ONEHANDED_MODE", "PullShrink");
    QCOMPARE(settings.mode(), FrogfishSettings::PullShrink);
    QVERIFY(settings.summary().contains(QStringLiteral("one-handed pullshrink")));

    // Unknown values are the defaults
    qputenv("FROGFISH_ONEHANDED_MODE", "both");
    qputenv("FROGFISH_HAND", "middle");
    QCOMPARE(settings.mode(), FrogfishSettings::Pull);
    QCOMPARE(settings.leftHanded(), false);
}

void TestFrogfish::settingsReload()
{
    FrogfishSettings settings;
    QSignalSpy changed(&settings, &FrogfishSettings::changed);
    settings.reload();
    settings.reload();
    // Coalesced
    QTRY_COMPARE(changed.count(), 1);
}

void TestFrogfish::crashGuard()
{
    QTemporaryDir dir;
    CrashGuard guard(dir.path() + QStringLiteral("/unhealthy-starts"));
    QVERIFY(guard.begin());
    QVERIFY(guard.begin());
    QVERIFY(guard.begin());
    QCOMPARE(guard.unhealthyStarts(), 3);
    QVERIFY(!guard.begin());
    guard.markHealthy();
    QCOMPARE(guard.unhealthyStarts(), 0);
    QVERIFY(guard.begin());
}

void TestFrogfish::useGuard()
{
    QTemporaryDir dir;
    const QString mode = QStringLiteral("zoom");
    {
        UseGuard guard(dir.path());
        QVERIFY(guard.recover().isEmpty());
        // A clean use leaves nothing
        guard.begin(mode);
        guard.end(mode);
        QVERIFY(guard.recover().isEmpty());
    }
    // Three runs that ended while zoomed
    for (int run = 1; run <= 3; ++run) {
        UseGuard guard(dir.path());
        if (run > 1)
            QCOMPARE(guard.recover(), QStringList { mode });
        QCOMPARE(guard.crashes(mode), run - 1);
        QVERIFY(!guard.tripped(mode));
        guard.begin(mode);
        guard.begin(mode);
    }
    UseGuard guard(dir.path());
    QCOMPARE(guard.recover(), QStringList { mode });
    QCOMPARE(guard.crashes(mode), 3);
    QVERIFY(guard.tripped(mode));
    // The other mode is unaffected
    QVERIFY(!guard.tripped(QStringLiteral("onehanded")));
    guard.reset(mode);
    QVERIFY(!guard.tripped(mode));

    // A clean end resets the count of crashes in a row
    guard.begin(mode);
    UseGuard(dir.path()).recover();
    QCOMPARE(guard.crashes(mode), 1);
    guard.begin(mode);
    guard.end(mode);
    QCOMPARE(guard.crashes(mode), 0);
}

void TestFrogfish::useGuardAfterShutdown()
{
    // A mark from before this boot: the phone was switched off while the
    // mode was on, which is no crash
    QTemporaryDir dir;
    const QString mode = QStringLiteral("onehanded");
    UseGuard guard(dir.path());
    guard.begin(mode);
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    QVERIFY(guard.recover(now + 60).isEmpty());
    QCOMPARE(guard.crashes(mode), 0);
    QVERIFY(!QFile::exists(dir.path() + QStringLiteral("/in-use-onehanded")));
    // Since this boot: a crash
    guard.begin(mode);
    QCOMPARE(guard.recover(now - 60), QStringList { mode });
    QCOMPARE(guard.crashes(mode), 1);
    // A mark without its time counts
    writeFile(dir.path() + QStringLiteral("/in-use-onehanded"), "x\n");
    QCOMPARE(guard.recover(now + 60), QStringList { mode });
    QCOMPARE(guard.crashes(mode), 2);
    // The system's own boot time is before now
    QVERIFY(UseGuard::systemBootTime() > 0);
    QVERIFY(UseGuard::systemBootTime() <= now);
}

void TestFrogfish::log()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/sub/log.txt");
    FrogfishLog::setFilePath(path);
    for (int i = 0; i < 30; ++i)
        FrogfishLog::trace("test", QStringLiteral("line %1").arg(i));
    // Its folder made, one line each, in order
    QStringList lines = QString::fromUtf8(readFile(path)).split(QLatin1Char('\n'));
    lines.removeAll(QString());
    QCOMPARE(lines.count(), 30);
    QVERIFY(lines.first().endsWith(QStringLiteral("test line 0")));
    QVERIFY(lines.last().endsWith(QStringLiteral("test line 29")));
    FrogfishLog::setFilePath(m_cache.path() + QStringLiteral("/test-log.txt"));
}

void TestFrogfish::controllerAttaches()
{
    QTemporaryDir dir;
    FrogfishController controller("QQuickWindow", dir.path());
    QVERIFY(controller.isActive());
    QSignalSpy attached(&controller, &FrogfishController::attached);
    QQuickWindow window;
    QTRY_COMPARE(attached.count(), 1);
    QCOMPARE(controller.window(), &window);
    QCOMPARE(controller.status(), QStringLiteral("active"));
    const QString diagnostics = controller.diagnostics();
    QVERIFY(diagnostics.startsWith(QStringLiteral("version " FROGFISH_VERSION)));
    QVERIFY(diagnostics.contains(QStringLiteral("settings: one-handed pull, size 60 %, zoom off")));
    QVERIFY(diagnostics.contains(QStringLiteral("guards: unhealthy starts 1")));
}

void TestFrogfish::controllerModeStatus()
{
    QTemporaryDir dir;
    FrogfishController controller("NoSuchWindow", dir.path());
    QCOMPARE(controller.status(), QStringLiteral("starting"));
    QCOMPARE(controller.modeStatus(QStringLiteral("onehanded")), QStringLiteral("on"));
    // Zoom is off by default
    QCOMPARE(controller.modeStatus(QStringLiteral("zoom")), QStringLiteral("off"));
    QCOMPARE(controller.modeStatus(QStringLiteral("other")), QStringLiteral("off"));
    qputenv("FROGFISH_ZOOM", "true");
    QCOMPARE(controller.modeStatus(QStringLiteral("zoom")), QStringLiteral("on"));
    qputenv("FROGFISH_ONEHANDED_MODE", "none");
    QCOMPARE(controller.modeStatus(QStringLiteral("onehanded")), QStringLiteral("off"));
    qunsetenv("FROGFISH_ONEHANDED_MODE");
    for (int i = 0; i < 3; ++i) {
        controller.useGuard().begin(QStringLiteral("onehanded"));
        UseGuard(dir.path()).recover();
    }
    QCOMPARE(controller.modeStatus(QStringLiteral("onehanded")), QStringLiteral("crash-guard"));
    controller.resetCrashGuard();
    QCOMPARE(controller.modeStatus(QStringLiteral("onehanded")), QStringLiteral("on"));
}

void TestFrogfish::controllerCrashGuard()
{
    QTemporaryDir dir;
    for (int start = 0; start < 3; ++start)
        QVERIFY(FrogfishController("NoSuchWindow", dir.path()).isActive());
    FrogfishController tripped("QQuickWindow", dir.path());
    QVERIFY(!tripped.isActive());
    QCOMPARE(tripped.status(), QStringLiteral("crash-guard"));
    QQuickWindow window;
    QTest::qWait(50);
    QVERIFY(!tripped.window());
    tripped.resetCrashGuard();
    QVERIFY(FrogfishController("NoSuchWindow", dir.path()).isActive());
}

void TestFrogfish::controllerRecoversUse()
{
    QTemporaryDir dir;
    UseGuard(dir.path()).begin(QStringLiteral("zoom"));
    FrogfishController controller("NoSuchWindow", dir.path());
    QCOMPARE(controller.useGuard().crashes(QStringLiteral("zoom")), 1);
    QVERIFY(controller.diagnostics().contains(QStringLiteral("zoom crashes 1")));
}

void TestFrogfish::statusService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("No session bus");
    QTemporaryDir dir;
    FrogfishController *controller = new FrogfishController("NoSuchWindow", dir.path());
    FrogfishStatusService *service = new FrogfishStatusService(controller);
    QVERIFY(service->registerOn(bus));
    QDBusInterface remote(QLatin1String(FrogfishStatusService::ServiceName),
                          QLatin1String(FrogfishStatusService::ObjectPath),
                          QStringLiteral("io.github.ccontino84.frogfish"), bus);
    QDBusReply<QString> status = remote.call(QStringLiteral("status"));
    QCOMPARE(status.value(), QStringLiteral("starting"));
    QDBusReply<QString> version = remote.call(QStringLiteral("version"));
    QCOMPARE(version.value(), QStringLiteral(FROGFISH_VERSION));
    QDBusReply<QString> mode = remote.call(QStringLiteral("modeStatus"), QStringLiteral("onehanded"));
    QCOMPARE(mode.value(), QStringLiteral("on"));
    bus.unregisterService(QLatin1String(FrogfishStatusService::ServiceName));
    bus.unregisterObject(QLatin1String(FrogfishStatusService::ObjectPath));
    delete controller;
}

void TestFrogfish::pluginOnlyInCompositor()
{
    QVERIFY(QGenericPluginFactory::keys().contains(QStringLiteral("frogfish")));
    // Not lipstick: nothing starts, and no "No such plugin"
    QScopedPointer<QObject> frogfish(QGenericPluginFactory::create(QStringLiteral("frogfish"), QString()));
    QVERIFY(frogfish);
    QCOMPARE(frogfish->metaObject()->className(), "QObject");
    QScopedPointer<QObject> loader(QGenericPluginFactory::create(QStringLiteral("lipstickaddons"), QString()));
    QVERIFY(loader);
    QVERIFY(loader->children().isEmpty());
}

void TestFrogfish::loaderStartsAddons()
{
    QQuickWindow window;
    // Through the loader, found by its key prefix
    QScopedPointer<QObject> loader(QGenericPluginFactory::create(QStringLiteral("lipstickaddons"),
                                                                 QStringLiteral("process=*:class=QQuickWindow")));
    QVERIFY(loader);
    QCOMPARE(loader->children().count(), 1);
    QObject *controller = loader->children().first();
    QCOMPARE(controller->metaObject()->className(), "FrogfishController");
    QVERIFY(QMetaObject::invokeMethod(controller, "findWindow"));

    // Once per process: by its own key too, and by the loader again
    QScopedPointer<QObject> again(QGenericPluginFactory::create(QStringLiteral("frogfish"),
                                                                QStringLiteral("process=*")));
    QCOMPARE(again->metaObject()->className(), "QObject");
    QScopedPointer<QObject> loaderAgain(QGenericPluginFactory::create(QStringLiteral("lipstickaddons"),
                                                                      QStringLiteral("process=*")));
    QVERIFY(loaderAgain->children().isEmpty());
    // Its D-Bus service registers once the event loop runs
    QTest::qWait(10);
    QDBusConnection::sessionBus().unregisterService(QStringLiteral("io.github.ccontino84.frogfish"));
    QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/frogfish"));
}

void TestFrogfish::updateEnv_data()
{
    QTest::addColumn<QStringList>("files");
    QTest::addColumn<int>("exitCode");
    QTest::addColumn<bool>("written");

    QTest::newRow("nothing") << QStringList() << 0 << true;
    QTest::newRow("owlfish 1.5") << QStringList { "00-owlfish.conf=QT_QPA_GENERIC_PLUGINS=owlfish" } << 0 << true;
    QTest::newRow("quoted, with a specification")
            << QStringList { "00-owlfish.conf=QT_QPA_GENERIC_PLUGINS=\"owlfish:class=X\"" } << 0 << true;
    QTest::newRow("other settings") << QStringList { "10-other.conf=QT_QPA_FONTDIR=/x" } << 0 << true;
    QTest::newRow("a port's plugins")
            << QStringList { "50-port.conf=QT_QPA_GENERIC_PLUGINS=evdevtouch:/dev/input/event2" } << 1 << false;
    QTest::newRow("a port's plugins before ours")
            << QStringList { "00-port.conf=QT_QPA_GENERIC_PLUGINS=evdevkeyboard" } << 1 << false;
    QTest::newRow("a port's plugins with owlfish")
            << QStringList { "zz-merged.conf=QT_QPA_GENERIC_PLUGINS=evdevtouch,owlfish" } << 1 << false;
    QTest::newRow("the last one wins")
            << QStringList { "00-port.conf=QT_QPA_GENERIC_PLUGINS=evdevtouch",
                             "zz-fix.conf=QT_QPA_GENERIC_PLUGINS=owlfish,lipstickaddons" } << 0 << true;
}

void TestFrogfish::updateEnv()
{
    QFETCH(QStringList, files);
    QFETCH(int, exitCode);
    QFETCH(bool, written);

    QTemporaryDir dir;
    for (const QString &file : files) {
        const int separator = file.indexOf(QLatin1Char('='));
        writeFile(dir.path() + QLatin1Char('/') + file.left(separator), file.mid(separator + 1).toUtf8() + '\n');
    }
    // A file from an earlier install, to be replaced or removed
    writeFile(dir.path() + QStringLiteral("/01-frogfish.conf"), "old\n");
    QProcess process;
    process.start(QStringLiteral("sh"), { QStringLiteral(FROGFISH_UPDATE_ENV), dir.path() });
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), exitCode);
    const QString own = dir.path() + QStringLiteral("/01-frogfish.conf");
    QCOMPARE(QFile::exists(own), written);
    if (written)
        QCOMPARE(readFile(own), QByteArray("QT_QPA_GENERIC_PLUGINS=owlfish,lipstickaddons\n"));
    else
        QVERIFY(process.readAllStandardOutput().contains("installed but inactive"));
}

void TestFrogfish::updateEnvKeepsOwnFileOutOfTheCheck()
{
    // Its own file, even with a foreign value, is not the device's
    QTemporaryDir dir;
    writeFile(dir.path() + QStringLiteral("/01-frogfish.conf"), "QT_QPA_GENERIC_PLUGINS=evdevtouch\n");
    QProcess process;
    process.start(QStringLiteral("sh"), { QStringLiteral(FROGFISH_UPDATE_ENV), dir.path() + QStringLiteral("/new") });
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 0);
    process.start(QStringLiteral("sh"), { QStringLiteral(FROGFISH_UPDATE_ENV), dir.path() });
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 0);
    QCOMPARE(readFile(dir.path() + QStringLiteral("/new/01-frogfish.conf")),
             QByteArray("QT_QPA_GENERIC_PLUGINS=owlfish,lipstickaddons\n"));
}

QTEST_MAIN(TestFrogfish)

#include "tst_frogfish.moc"
