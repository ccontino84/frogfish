// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0

// Once a hold is recognised: a ring grows out of the finger to the item's
// edge and fades, dark outside and light inside, so one of the two shows on
// any background. Sized and placed from C++, played by start().
Item {
    id: cue

    // Where the ring starts
    property real fingerSize: 0
    property int duration: 450
    // 0 to 1 while it plays
    property real phase: 0
    readonly property real lineWidth: Math.max(2, fingerSize / 16)
    readonly property real ringSize: fingerSize + (width - fingerSize) * phase

    function start() {
        visible = true
        anim.restart()
    }

    NumberAnimation {
        id: anim
        target: cue
        property: "phase"
        from: 0
        to: 1
        duration: cue.duration
        onRunningChanged: if (!running) cue.visible = false
    }

    Rectangle {
        anchors.centerIn: parent
        width: cue.ringSize
        height: width
        radius: width / 2
        color: "transparent"
        border.width: cue.lineWidth
        border.color: Qt.rgba(0, 0, 0, 0.6 * (1 - cue.phase))
    }

    Rectangle {
        anchors.centerIn: parent
        width: cue.ringSize - 2 * cue.lineWidth
        height: width
        radius: width / 2
        color: "transparent"
        border.width: cue.lineWidth
        border.color: Qt.rgba(1, 1, 1, 0.9 * (1 - cue.phase))
    }
}
