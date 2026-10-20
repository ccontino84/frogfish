// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "units.h"

#include <QScreen>
#include <QWindow>

namespace {

const qreal FallbackWidthMm = 70;

}

qreal Units::pixelsPerMm(const QWindow *window)
{
    if (!window)
        return 1;
    const QScreen *screen = window->screen();
    if (screen && screen->physicalDotsPerInch() > 0)
        return screen->physicalDotsPerInch() / 25.4;
    return window->width() > 0 ? window->width() / FallbackWidthMm : 1;
}
