# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

TEMPLATE = app
TARGET = tst_frogfish
CONFIG += testcase c++11
QT += testlib gui quick

include(../../src/frogfish.pri)

SOURCES += tst_frogfish.cpp

DEFINES += FROGFISH_PLUGINS_DIR=\\\"$$OUT_PWD/../../plugins\\\"
DEFINES += FROGFISH_UPDATE_ENV=\\\"$$PWD/../../config/update-env\\\"
DEFINES += FROGFISH_DEFAULTS_JS=\\\"$$PWD/../../settings/defaults.js\\\"
