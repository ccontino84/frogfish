// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_MOVEDITEM_H
#define FROGFISH_MOVEDITEM_H

#include <QList>
#include <QPointer>
#include <QQuickItem>
#include <QVariant>

// A lipstick item scaled from its top corner on the hand's side, then moved
// down: a Scale and a Translate after its own transforms (the layers' x and
// y belong to their own states). At full size and in place, they are
// removed again, and the item's transform list is what it was.
class MovedItem
{
public:
    MovedItem() = default;
    ~MovedItem() { reset(); }
    MovedItem(const MovedItem &) = delete;
    MovedItem &operator=(const MovedItem &) = delete;

    void set(QQuickItem *item, qreal factor, qreal y, bool leftHanded);
    void reset();
    bool used() const { return !m_scale.isNull(); }

private:
    QPointer<QQuickItem> m_item;
    QPointer<QObject> m_scale;
    QPointer<QObject> m_translate;
};

// A lipstick item moved by its own properties: the application windows and
// the keyboard, which no QML positions. What is changed is put back to what
// it was before the first change.
class PlacedItem
{
public:
    explicit PlacedItem(QQuickItem *item = nullptr) : m_item(item) {}

    void setY(QQuickItem *item, qreal y);
    void setScale(QQuickItem *item, qreal scale, QQuickItem::TransformOrigin origin);
    void reset();
    QQuickItem *item() const { return m_item; }

private:
    void take(QQuickItem *item);

    QPointer<QQuickItem> m_item;
    bool m_movedY = false;
    bool m_scaled = false;
    qreal m_y = 0;
    qreal m_scale = 1;
    QQuickItem::TransformOrigin m_origin = QQuickItem::Center;
};

// Properties of lipstick's objects set here (anchors' margins), each put back
// to its value before the first change
class SavedProperties
{
public:
    void set(QObject *object, const char *name, const QVariant &value);
    void restore();
    bool isEmpty() const { return m_saved.isEmpty(); }

private:
    struct Saved
    {
        QPointer<QObject> object;
        QByteArray name;
        QVariant value;
    };
    QList<Saved> m_saved;
};

#endif
