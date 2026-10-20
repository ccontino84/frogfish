// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "moveditem.h"
#include "qml.h"

#include <QQmlEngine>
#include <QVector3D>

void MovedItem::set(QQuickItem *item, qreal factor, qreal y, bool leftHanded)
{
    const bool identity = factor == 1 && y == 0;
    if (item != m_item || identity)
        reset();
    if (!item || identity)
        return;
    if (!m_scale || !m_translate) {
        QQmlEngine *engine = qmlEngine(item);
        m_item = item;
        m_scale = Qml::appendTransform(engine, item, "Scale", item);
        m_translate = Qml::appendTransform(engine, item, "Translate", item);
        if (!m_scale || !m_translate) {
            reset();
            return;
        }
    }
    m_scale->setProperty("origin", QVector3D(leftHanded ? 0 : item->width(), 0, 0));
    m_scale->setProperty("xScale", factor);
    m_scale->setProperty("yScale", factor);
    m_translate->setProperty("y", y);
}

void MovedItem::reset()
{
    Qml::removeTransforms(m_item ? qmlEngine(m_item) : nullptr, m_item, { m_scale, m_translate });
    m_item = nullptr;
}

void PlacedItem::take(QQuickItem *item)
{
    if (item == m_item)
        return;
    reset();
    m_item = item;
}

void PlacedItem::setY(QQuickItem *item, qreal y)
{
    take(item);
    if (!item)
        return;
    if (!m_movedY) {
        m_movedY = true;
        m_y = item->y();
    }
    item->setY(y);
}

void PlacedItem::setScale(QQuickItem *item, qreal scale, QQuickItem::TransformOrigin origin)
{
    take(item);
    if (!item)
        return;
    if (!m_scaled) {
        m_scaled = true;
        m_scale = item->scale();
        m_origin = item->transformOrigin();
    }
    item->setTransformOrigin(origin);
    item->setScale(scale);
}

void PlacedItem::reset()
{
    if (m_item) {
        if (m_movedY)
            m_item->setY(m_y);
        if (m_scaled) {
            m_item->setScale(m_scale);
            m_item->setTransformOrigin(m_origin);
        }
    }
    m_item = nullptr;
    m_movedY = false;
    m_scaled = false;
}

void SavedProperties::set(QObject *object, const char *name, const QVariant &value)
{
    if (!object)
        return;
    bool saved = false;
    for (const Saved &entry : m_saved)
        saved = saved || (entry.object == object && entry.name == name);
    if (!saved)
        m_saved << Saved { object, QByteArray(name), object->property(name) };
    object->setProperty(name, value);
}

void SavedProperties::restore()
{
    // The last change first
    for (int i = m_saved.count() - 1; i >= 0; --i) {
        const Saved &entry = m_saved.at(i);
        if (entry.object)
            entry.object->setProperty(entry.name.constData(), entry.value);
    }
    m_saved.clear();
}
