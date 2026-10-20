// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "crashguard.h"
#include "log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace {

qint64 readNumber(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    return qMax(qint64(0), file.readAll().trimmed().toLongLong());
}

void writeNumber(const QString &path, qint64 number)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcFrogfish) << "Cannot write" << path;
        return;
    }
    file.write(QByteArray::number(number) + '\n');
    // Must hit the disk before a possible crash later in this start
    if (!file.commit())
        qCWarning(lcFrogfish) << "Cannot commit" << path;
}

void removeFile(const QString &path)
{
    if (QFile::exists(path) && !QFile::remove(path))
        qCWarning(lcFrogfish) << "Cannot remove" << path;
}

}

CrashGuard::CrashGuard(const QString &filePath, int maxUnhealthyStarts)
    : m_filePath(filePath)
    , m_maxUnhealthyStarts(maxUnhealthyStarts)
{
}

int CrashGuard::unhealthyStarts() const
{
    return int(readNumber(m_filePath));
}

bool CrashGuard::begin()
{
    const int count = unhealthyStarts();
    if (count >= m_maxUnhealthyStarts)
        return false;
    writeNumber(m_filePath, count + 1);
    return true;
}

void CrashGuard::markHealthy()
{
    removeFile(m_filePath);
}

UseGuard::UseGuard(const QString &directory, int maxCrashes)
    : m_directory(directory)
    , m_maxCrashes(maxCrashes)
{
}

QString UseGuard::markPath(const QString &mode) const
{
    return m_directory + QStringLiteral("/in-use-") + mode;
}

QString UseGuard::countPath(const QString &mode) const
{
    return m_directory + QStringLiteral("/crashes-") + mode;
}

QStringList UseGuard::recover(qint64 bootTime)
{
    QStringList crashed;
    const QString prefix = QStringLiteral("in-use-");
    for (const QString &name : QDir(m_directory).entryList({ prefix + QLatin1Char('*') }, QDir::Files)) {
        const QString mode = name.mid(prefix.size());
        // Marked before this boot: the home screen ended with the system
        const qint64 marked = readNumber(markPath(mode));
        if (marked <= 0 || marked >= bootTime) {
            writeNumber(countPath(mode), crashes(mode) + 1);
            crashed << mode;
        }
        removeFile(markPath(mode));
    }
    return crashed;
}

qint64 UseGuard::systemBootTime()
{
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    // Its size is 0, so atEnd() is true at once: read to the end
    for (const QByteArray &line : file.readAll().split('\n')) {
        if (line.startsWith("btime "))
            return line.mid(6).trimmed().toLongLong();
    }
    return 0;
}

bool UseGuard::tripped(const QString &mode) const
{
    return crashes(mode) >= m_maxCrashes;
}

int UseGuard::crashes(const QString &mode) const
{
    return int(readNumber(countPath(mode)));
}

void UseGuard::begin(const QString &mode)
{
    // currentSecsSinceEpoch() is Qt 5.8
    if (!QFile::exists(markPath(mode)))
        writeNumber(markPath(mode), QDateTime::currentMSecsSinceEpoch() / 1000);
}

void UseGuard::end(const QString &mode)
{
    removeFile(markPath(mode));
    removeFile(countPath(mode));
}

void UseGuard::reset(const QString &mode)
{
    removeFile(countPath(mode));
}
