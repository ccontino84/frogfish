// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_SETTINGS_H
#define FROGFISH_SETTINGS_H

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariant>

class MDConfItem;

// The settings under /apps/frogfish/ (dconf), with their defaults; the
// settings page's are in settings/defaults.js and must match (a test checks):
//   onehanded_mode        "none", "pull" (default), "shrink" or "pullshrink"
//   onehanded_size        percent of the screen's height the picture keeps
//                         when the corner or the top menu switch it on; 45
//                         to 75, default 60
//   zoom                  bool, default false
//   back                  bool, default false: the back gesture
//   hand                  "right" (default) or "left": the side of the
//                         corner, the edge and the zoom button; back is on
//                         the other edge
// Hidden, for tuning:
//   zoom_max              the highest zoom level, 1.5 to 15, default 8
// Written by the plugin, not a setting:
//   onehanded_active      bool: one-handed mode is on right now. The top
//                         menu switch shows it, and asks for the other state
//                         by writing it.
//
// Host builds without mlite read FROGFISH_<KEY> environment variables
// instead (FROGFISH_ONEHANDED_MODE=shrink), at every read.
class FrogfishSettings : public QObject
{
    Q_OBJECT

public:
    enum Mode { None, Pull, Shrink, PullShrink };
    // The value of onehanded_mode
    static QString modeName(Mode mode);

    static const char *const Path;
    // Also the smallest the edge drag goes, a fixed size still usable
    static const int MinimumSize = 45;
    static const int MaximumSize = 75;
    static const int DefaultSize = 60;

    // Every setting at once, as the modes keep them between changes
    struct Values
    {
        Mode mode = Pull;
        // 0.45 to 0.75
        qreal size = DefaultSize / 100.0;
        bool zoom = false;
        qreal zoomMaximum = 8;
        bool back = false;
        bool leftHanded = false;
    };

    explicit FrogfishSettings(QObject *parent = nullptr);

    Values values() const;
    // Not None
    bool oneHanded() const { return mode() != None; }
    Mode mode() const;
    // 0.45 to 0.75
    qreal size() const;
    bool zoom() const;
    qreal zoomMaximum() const;
    bool back() const;
    bool leftHanded() const;

    // After "Reset to defaults": new items for every key, whatever
    // notifications the reset of the whole path sent
    void reload();
    // One line for the diagnostics
    QString summary() const;

    // onehanded_active as the key has it now
    bool oneHandedActive() const;
    // The state, for the top menu switch: written when the key differs
    void publishOneHandedActive(bool active);
    // Writes the key as the top menu switch does (for tests); host builds
    // keep it in memory
    void requestOneHandedActive(bool active);

    // Every key is read the same way. A missing value is the default;
    // "false", "0", "off", "no" and the empty string are false.
    static bool parseBool(const QVariant &value, bool fallback);
    // A number in range, or the default if it isn't a number
    static int parseInt(const QVariant &value, int fallback, int minimum, int maximum);
    static qreal parseReal(const QVariant &value, qreal fallback, qreal minimum, qreal maximum);

signals:
    // Once for several keys changed together
    void changed();
    // onehanded_active was set to something else than published: the top
    // menu switch asks for that state
    void oneHandedActiveRequested();

private:
    QVariant value(const char *key) const;
    void watch();
    void activeKeyChanged();

#ifdef HAVE_MLITE
    QHash<QString, MDConfItem *> m_items;
    MDConfItem *m_activeItem = nullptr;
#else
    bool m_activeValue = false;
#endif
    bool m_publishedActive = false;
    QTimer m_changed;
};

#endif
