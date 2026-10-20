// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "settings.h"

#ifdef HAVE_MLITE
#include <MDConfItem>
#endif

const char *const FrogfishSettings::Path = "/apps/frogfish";

namespace {

const char *const Keys[] = {
    "onehanded_mode", "onehanded_size", "zoom", "zoom_max", "back", "hand"
};
const char *const ActiveKey = "onehanded_active";
// In Mode's order
const char *const ModeNames[] = { "none", "pull", "shrink", "pullshrink" };

const qreal DefaultZoomMaximum = 8;
const qreal MinimumZoomMaximum = 1.5;
const qreal MaximumZoomMaximum = 15;

}

FrogfishSettings::FrogfishSettings(QObject *parent)
    : QObject(parent)
{
    m_changed.setSingleShot(true);
    m_changed.setInterval(0);
    connect(&m_changed, &QTimer::timeout, this, &FrogfishSettings::changed);
    watch();
#ifdef HAVE_MLITE
    // Not one of the settings: kept through a reload
    m_activeItem = new MDConfItem(QLatin1String(Path) + QLatin1Char('/') + QLatin1String(ActiveKey), this);
    connect(m_activeItem, &MDConfItem::valueChanged, this, &FrogfishSettings::activeKeyChanged);
#endif
}

bool FrogfishSettings::oneHandedActive() const
{
#ifdef HAVE_MLITE
    return parseBool(m_activeItem->value(), false);
#else
    return m_activeValue;
#endif
}

void FrogfishSettings::publishOneHandedActive(bool active)
{
    m_publishedActive = active;
    if (oneHandedActive() == active)
        return;
#ifdef HAVE_MLITE
    m_activeItem->set(active);
#else
    m_activeValue = active;
#endif
}

void FrogfishSettings::requestOneHandedActive(bool active)
{
#ifdef HAVE_MLITE
    m_activeItem->set(active);
#else
    // dconf tells of a change later, from the event loop
    m_activeValue = active;
    QTimer::singleShot(0, this, &FrogfishSettings::activeKeyChanged);
#endif
}

void FrogfishSettings::activeKeyChanged()
{
    // Its own write comes back too
    if (oneHandedActive() != m_publishedActive)
        emit oneHandedActiveRequested();
}

void FrogfishSettings::watch()
{
#ifdef HAVE_MLITE
    for (const char *key : Keys) {
        MDConfItem *item = new MDConfItem(QLatin1String(Path) + QLatin1Char('/') + QLatin1String(key), this);
        connect(item, &MDConfItem::valueChanged, &m_changed, static_cast<void (QTimer::*)()>(&QTimer::start));
        m_items.insert(QLatin1String(key), item);
    }
#endif
}

void FrogfishSettings::reload()
{
#ifdef HAVE_MLITE
    qDeleteAll(m_items);
    m_items.clear();
#endif
    watch();
    m_changed.start();
}

QVariant FrogfishSettings::value(const char *key) const
{
#ifdef HAVE_MLITE
    MDConfItem *item = m_items.value(QLatin1String(key));
    return item ? item->value() : QVariant();
#else
    const QByteArray name = "FROGFISH_" + QByteArray(key).toUpper();
    return qEnvironmentVariableIsSet(name.constData())
            ? QVariant(QString::fromLocal8Bit(qgetenv(name.constData())))
            : QVariant();
#endif
}

bool FrogfishSettings::parseBool(const QVariant &value, bool fallback)
{
    if (!value.isValid())
        return fallback;
    const QString text = value.toString().trimmed().toLower();
    return !(text.isEmpty() || text == QLatin1String("false") || text == QLatin1String("0")
             || text == QLatin1String("off") || text == QLatin1String("no"));
}

int FrogfishSettings::parseInt(const QVariant &value, int fallback, int minimum, int maximum)
{
    bool ok = false;
    const int number = value.toString().trimmed().toInt(&ok);
    return ok ? qBound(minimum, number, maximum) : fallback;
}

qreal FrogfishSettings::parseReal(const QVariant &value, qreal fallback, qreal minimum, qreal maximum)
{
    bool ok = false;
    const double number = value.toString().trimmed().toDouble(&ok);
    // double: qreal is float on 32-bit ARM
    return ok ? qreal(qBound(double(minimum), number, double(maximum))) : fallback;
}

FrogfishSettings::Values FrogfishSettings::values() const
{
    Values values;
    values.mode = mode();
    values.size = size();
    values.zoom = zoom();
    values.zoomMaximum = zoomMaximum();
    values.back = back();
    values.leftHanded = leftHanded();
    return values;
}

QString FrogfishSettings::modeName(Mode mode)
{
    return QLatin1String(ModeNames[mode]);
}

FrogfishSettings::Mode FrogfishSettings::mode() const
{
    const QString name = value("onehanded_mode").toString().trimmed().toLower();
    for (const Mode mode : { None, Pull, Shrink, PullShrink }) {
        if (name == modeName(mode))
            return mode;
    }
    return Pull;
}

qreal FrogfishSettings::size() const
{
    return parseInt(value("onehanded_size"), DefaultSize, MinimumSize, MaximumSize) / 100.0;
}

bool FrogfishSettings::zoom() const
{
    return parseBool(value("zoom"), false);
}

qreal FrogfishSettings::zoomMaximum() const
{
    return parseReal(value("zoom_max"), DefaultZoomMaximum, MinimumZoomMaximum, MaximumZoomMaximum);
}

bool FrogfishSettings::back() const
{
    return parseBool(value("back"), false);
}

bool FrogfishSettings::leftHanded() const
{
    // Anything but "left" is right
    return value("hand").toString().trimmed().toLower() == QLatin1String("left");
}

QString FrogfishSettings::summary() const
{
    return QStringLiteral("settings: one-handed %1, size %2 %, zoom %3, max %4x, back %5, hand %6")
            .arg(modeName(mode()))
            .arg(qRound(size() * 100))
            .arg(zoom() ? QStringLiteral("on") : QStringLiteral("off"))
            .arg(zoomMaximum())
            .arg(back() ? QStringLiteral("on") : QStringLiteral("off"))
            .arg(leftHanded() ? QStringLiteral("left") : QStringLiteral("right"));
}
