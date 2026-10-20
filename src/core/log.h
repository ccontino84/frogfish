// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_LOG_H
#define FROGFISH_LOG_H

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(lcFrogfish)

// What the gestures decide, in the journal and in a file of its own: the
// journal keeps only 1 MB in RAM, too little for days of touches. The file
// is capped and keeps one older copy (.old).
namespace FrogfishLog {

// "<area>: <message>" to the journal; with the time to the file
void trace(const char *area, const QString &message);
// ~/.cache/frogfish/log.txt
QString filePath();
// For tests
void setFilePath(const QString &path);

}

#endif
