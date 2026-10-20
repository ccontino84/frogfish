# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

# One folder per part; the include paths keep #include "name.h" working
# from anywhere
#   core       the controller, settings, crash guards, D-Bus status, log
#   lipstick   every read of lipstick's QML (LipstickScene)
#   input      the event filter, what a hold is, the ring
#   onehanded  one-handed mode, its rules, the items it moves
#   zoom       the zoom and its overlay
#   back       the back gesture
FROGFISH_DIRS = core lipstick input onehanded zoom back
INCLUDEPATH += $$PWD
for(dir, FROGFISH_DIRS): INCLUDEPATH += $$PWD/$$dir

# The status for the settings page; ngfd for the haptics
QT += dbus

HEADERS += \
    $$PWD/back/back.h \
    $$PWD/core/controller.h \
    $$PWD/core/crashguard.h \
    $$PWD/core/haptics.h \
    $$PWD/core/log.h \
    $$PWD/core/qml.h \
    $$PWD/core/settings.h \
    $$PWD/core/statusservice.h \
    $$PWD/core/units.h \
    $$PWD/lipstick/scene.h \
    $$PWD/input/hold.h \
    $$PWD/input/holdcue.h \
    $$PWD/input/router.h \
    $$PWD/onehanded/moveditem.h \
    $$PWD/onehanded/onehanded.h \
    $$PWD/onehanded/policy.h \
    $$PWD/zoom/zoom.h

SOURCES += \
    $$PWD/back/back.cpp \
    $$PWD/core/controller.cpp \
    $$PWD/core/crashguard.cpp \
    $$PWD/core/haptics.cpp \
    $$PWD/core/log.cpp \
    $$PWD/core/qml.cpp \
    $$PWD/core/settings.cpp \
    $$PWD/core/statusservice.cpp \
    $$PWD/core/units.cpp \
    $$PWD/lipstick/scene.cpp \
    $$PWD/input/hold.cpp \
    $$PWD/input/holdcue.cpp \
    $$PWD/input/router.cpp \
    $$PWD/onehanded/moveditem.cpp \
    $$PWD/onehanded/onehanded.cpp \
    $$PWD/onehanded/policy.cpp \
    $$PWD/zoom/zoom.cpp

# Frogfish's own QML (the cue, the cover, the zoom's overlay), loaded into
# the compositor's engine from qrc:/frogfish/
RESOURCES += $$PWD/frogfish.qrc

# The package version, for the settings page; the spec is the one source.
# It goes into a generated header, rewritten only when it changes, so that
# the files using it are rebuilt; the spec is a dependency of the Makefile, so
# qmake runs again when Version is raised. Where the spec is not next to the
# sources (OBS's tar_git moves rpm/ away), the spec passes the version as
# FROGFISH_VERSION; those builds always start clean.
SPEC_FILE = $$PWD/../rpm/frogfish.spec
exists($$SPEC_FILE) {
    SPEC_LINES = $$cat($$SPEC_FILE, lines)
    for(line, SPEC_LINES) {
        contains(line, "^Version:.*"): FROGFISH_VERSION = $$section(line, " ", -1)
    }
    QMAKE_INTERNAL_INCLUDED_FILES += $$SPEC_FILE
}
isEmpty(FROGFISH_VERSION): error("No Version in rpm/frogfish.spec, and no FROGFISH_VERSION given")
# In a subdirectory of the build directory: qmake searches the sources
# first, where an in-source build's copy would shadow an out-of-source one
VERSION_HEADER = $$OUT_PWD/generated/frogfish_version.h
VERSION_LINE = "$${LITERAL_HASH}define FROGFISH_VERSION \"$$FROGFISH_VERSION\""
VERSION_EXISTING = $$cat($$VERSION_HEADER, lines)
!equals(VERSION_EXISTING, $$VERSION_LINE) {
    !write_file($$VERSION_HEADER, VERSION_LINE): error("Cannot write $$VERSION_HEADER")
}
INCLUDEPATH += $$OUT_PWD/generated
QMAKE_CLEAN += $$VERSION_HEADER

# On Sailfish OS settings come from dconf via mlite5; host builds fall back
# to environment variables (see settings.cpp)
packagesExist(mlite5) {
    CONFIG += link_pkgconfig
    PKGCONFIG += mlite5
    DEFINES += HAVE_MLITE
}
