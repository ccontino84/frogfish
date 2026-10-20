// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_QML_H
#define FROGFISH_QML_H

#include <QList>

class QObject;
class QQmlEngine;
class QQuickItem;

// Objects made in the compositor's QML engine
namespace Qml {

// One of Frogfish's own QML files (frogfish.qrc), e.g. "Cue.qml"; null and
// a warning if it fails, or quietly when quiet (an optional file)
QObject *create(QQmlEngine *engine, const char *file, bool quiet = false);
QQuickItem *createItem(QQmlEngine *engine, const char *file);
// A QtQuick transform ("Scale", "Translate") made and appended to the
// item's transform list, owned by owner; null if it can't be made
QObject *appendTransform(QQmlEngine *engine, QQuickItem *item, const char *type, QObject *owner);
// Taken out of the item's transform list, the others kept in their order,
// and deleted; null entries are skipped
void removeTransforms(QQmlEngine *engine, QQuickItem *item, const QList<QObject *> &transforms);

}

#endif
