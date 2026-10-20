// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcFrogfish, "frogfish", QtInfoMsg)

namespace {

const qint64 MaximumBytes = 2 * 1024 * 1024;

QString &customPath()
{
    static QString path;
    return path;
}

}

namespace FrogfishLog {

QString filePath()
{
    if (!customPath().isEmpty())
        return customPath();
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
            + QStringLiteral("/frogfish/log.txt");
}

void setFilePath(const QString &path)
{
    customPath() = path;
}

void trace(const char *area, const QString &message)
{
    qCInfo(lcFrogfish, "%s: %s", area, qPrintable(message));

    QFile file(filePath());
    if (file.size() > MaximumBytes) {
        QFile::remove(file.fileName() + QStringLiteral(".old"));
        file.rename(file.fileName() + QStringLiteral(".old"));
        file.setFileName(filePath());
    }
    QDir().mkpath(QFileInfo(file).path());
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        const QString line = QDateTime::currentDateTime().toString(Qt::ISODate)
                + QLatin1Char(' ') + QLatin1String(area) + QLatin1Char(' ') + message;
        file.write(line.toUtf8() + '\n');
    }
}

}
