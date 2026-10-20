// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_UNITS_H
#define FROGFISH_UNITS_H

#include <QtGlobal>

class QWindow;

namespace Units {

// The screen's physical density; without one, a phone about 70 mm wide.
// Every size of the gestures is given in millimetres, the same on every
// phone.
qreal pixelsPerMm(const QWindow *window);

}

#endif
