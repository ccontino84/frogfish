// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import Sailfish.Silica 1.0

// The overlay's colours from the ambience; without Silica (host tests) this
// fails to load and the overlay keeps its plain ones
QtObject {
    readonly property color accent: Theme.highlightColor
    readonly property color primary: Theme.primaryColor
}
