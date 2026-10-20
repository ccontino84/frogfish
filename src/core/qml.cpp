// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "qml.h"
#include "log.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlListReference>
#include <QQuickItem>

QObject *Qml::create(QQmlEngine *engine, const char *file, bool quiet)
{
    if (!engine)
        return nullptr;
    // Local, so it is ready at once
    QQmlComponent component(engine, QUrl(QStringLiteral("qrc:/frogfish/") + QLatin1String(file)));
    QObject *object = component.isReady() ? component.create() : nullptr;
    if (!object && !quiet)
        qCWarning(lcFrogfish) << "Cannot create" << file << component.errorString();
    return object;
}

QQuickItem *Qml::createItem(QQmlEngine *engine, const char *file)
{
    QObject *object = create(engine, file);
    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (object && !item)
        delete object;
    return item;
}

QObject *Qml::appendTransform(QQmlEngine *engine, QQuickItem *item, const char *type, QObject *owner)
{
    if (!engine || !item)
        return nullptr;
    QQmlComponent component(engine);
    component.setData(QByteArray("import QtQuick 2.0\n") + type + " {}", QUrl());
    QObject *transform = component.create();
    if (!transform) {
        qCWarning(lcFrogfish) << "Cannot create" << type << component.errorString();
        return nullptr;
    }
    transform->setParent(owner);
    QQmlListReference(item, "transform", engine).append(transform);
    return transform;
}

void Qml::removeTransforms(QQmlEngine *engine, QQuickItem *item, const QList<QObject *> &transforms)
{
    if (engine && item) {
        QQmlListReference list(item, "transform", engine);
        QList<QObject *> others;
        for (int i = 0; i < list.count(); ++i) {
            if (!transforms.contains(list.at(i)))
                others << list.at(i);
        }
        if (others.count() != list.count()) {
            list.clear();
            for (QObject *transform : others)
                list.append(transform);
        }
    }
    qDeleteAll(transforms);
}
