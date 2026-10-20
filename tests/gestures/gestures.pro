# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

TEMPLATE = app
TARGET = tst_gestures
CONFIG += testcase c++11
QT += testlib gui quick

include(../../src/frogfish.pri)

SOURCES += tst_gestures.cpp
OTHER_FILES += qml/*.qml

# A stand-in for lipstick's compositor.qml
DEFINES += FROGFISH_TEST_QML=\\\"$$PWD/qml\\\"
