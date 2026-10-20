// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_CRASHGUARD_H
#define FROGFISH_CRASHGUARD_H

#include <QString>
#include <QStringList>

// Protects the home screen against a plugin that crashes it on startup.
//
// Every start that has not yet been confirmed healthy is counted in a small
// file. After maxUnhealthyStarts such starts in a row the guard trips and the
// plugin stays inactive until the file is removed.
class CrashGuard
{
public:
    explicit CrashGuard(const QString &filePath, int maxUnhealthyStarts = 3);

    // Returns false if the guard has tripped; otherwise records this start
    bool begin();
    // This run has been healthy long enough; reset the count
    void markHealthy();

    int unhealthyStarts() const;
    QString filePath() const { return m_filePath; }

private:
    QString m_filePath;
    int m_maxUnhealthyStarts;
};

// Protects the home screen against a mode that crashes it while in use,
// which the startup guard can't see.
//
// A mode is marked in use (a small file with the time) from the moment it
// changes the home screen until it has put everything back. A start that
// finds a mark counts a crash in that mode, unless the mark is older than
// the system's boot (the phone was switched off meanwhile). After maxCrashes
// in a row the mode stays off until it is reset; a clean end of the mode
// resets the count.
class UseGuard
{
public:
    explicit UseGuard(const QString &directory, int maxCrashes = 3);

    // At start: counts the marks the crashed run left, and removes them.
    // Returns the modes that crashed. bootTime in seconds since the epoch.
    QStringList recover(qint64 bootTime = systemBootTime());
    // /proc/stat's btime; 0 if unknown (every mark counts then)
    static qint64 systemBootTime();
    bool tripped(const QString &mode) const;
    int crashes(const QString &mode) const;
    void begin(const QString &mode);
    void end(const QString &mode);
    void reset(const QString &mode);

private:
    QString markPath(const QString &mode) const;
    QString countPath(const QString &mode) const;

    QString m_directory;
    int m_maxCrashes;
};

#endif
