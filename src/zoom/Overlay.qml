// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0

// While zoomed, unmagnified, in screen coordinates: the black around
// one-handed mode's picture, the border, the level and the mode button. The
// properties are set from C++ (ZoomMode::updateOverlay()).
Item {
    // A millimetre in pixels
    property real unit: 10
    property real level: 1
    // Touch mode (the button's lower half)
    property bool edit
    property bool showLevel
    property bool rightHand: true
    property rect button
    property rect picture
    // Colors.qml where Silica is there
    property QtObject colors
    readonly property color accent: colors ? colors.accent : "#80c0ff"
    readonly property color primary: colors ? colors.primary : "white"

    function dim(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

    // Black around one-handed mode's picture: the magnified scene spills
    // over it
    Rectangle { color: "black"; width: parent.width; height: picture.y }
    Rectangle {
        color: "black"; y: picture.y + picture.height
        width: parent.width; height: parent.height - y
    }
    Rectangle { color: "black"; y: picture.y; width: picture.x; height: picture.height }
    Rectangle {
        color: "black"; x: picture.x + picture.width; y: picture.y
        width: parent.width - x; height: picture.height
    }

    // The picture is zoomed: a thin border in the highlight colour
    Rectangle {
        x: picture.x; y: picture.y; width: picture.width; height: picture.height
        color: "transparent"
        border.width: Math.max(2, 0.6 * unit); border.color: accent
    }

    // The level, only while it changes: in the picture's top corner on the
    // hand's side, clear of the camera hole, just inside the rounded corner
    Rectangle {
        width: levelText.width + 4 * unit; height: levelText.height + unit; radius: height / 2
        x: rightHand ? picture.x + picture.width - 3.5 * unit - width : picture.x + 3.5 * unit
        y: picture.y + 3 * unit
        color: "#c0000000"
        opacity: showLevel ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 250 } }

        Text {
            id: levelText
            anchors.centerIn: parent; color: accent; font.pixelSize: 4.5 * unit
            text: level.toFixed(1) + "×"
        }
    }

    // The floating mode choice: pan above, touch below; the current one
    // highlighted, as Silica marks a selected item
    Rectangle {
        x: button.x; y: button.y; width: button.width; height: button.height
        radius: 1.5 * unit; color: "#e0000000"

        Rectangle {
            x: 0.5 * unit; width: parent.width - unit; height: (parent.height - unit) / 2
            y: edit ? parent.height - height - 0.5 * unit : 0.5 * unit
            radius: unit; color: dim(accent, 0.3)
            Behavior on y { NumberAnimation { duration: 120 } }
        }

        // Pan: four-way arrows
        Canvas {
            width: 6 * unit; height: width
            x: (parent.width - width) / 2; y: (parent.height / 2 - height) / 2 + 0.25 * unit
            property color ink: edit ? dim(primary, 0.7) : accent
            onInkChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var c = width / 2, a = width * 0.42, h = width * 0.15
                ctx.strokeStyle = ink
                ctx.lineWidth = Math.max(2, width * 0.1)
                ctx.lineCap = "round"; ctx.lineJoin = "round"
                ctx.beginPath()
                ctx.moveTo(c - a, c); ctx.lineTo(c + a, c)
                ctx.moveTo(c, c - a); ctx.lineTo(c, c + a)
                ctx.moveTo(c + a - h, c - h); ctx.lineTo(c + a, c); ctx.lineTo(c + a - h, c + h)
                ctx.moveTo(c - a + h, c - h); ctx.lineTo(c - a, c); ctx.lineTo(c - a + h, c + h)
                ctx.moveTo(c - h, c - a + h); ctx.lineTo(c, c - a); ctx.lineTo(c + h, c - a + h)
                ctx.moveTo(c - h, c + a - h); ctx.lineTo(c, c + a); ctx.lineTo(c + h, c + a - h)
                ctx.stroke()
            }
        }

        // Touch: a hand, index finger up, on a 100-unit grid: the finger, the
        // three folded fingers' knuckles beside it (middle, ring, little),
        // the fist; gaps between them
        Canvas {
            width: 6 * unit; height: width
            x: (parent.width - width) / 2; y: parent.height / 2 + (parent.height / 2 - height) / 2 - 0.25 * unit
            property color ink: edit ? accent : dim(primary, 0.7)
            onInkChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var u = width / 100
                ctx.fillStyle = ink
                ctx.beginPath()
                ctx.roundedRect(18 * u, 6 * u, 22 * u, 76 * u, 11 * u, 11 * u)
                ctx.roundedRect(40 * u, 32 * u, 16 * u, 44 * u, 8 * u, 8 * u)
                ctx.roundedRect(54 * u, 36 * u, 16 * u, 40 * u, 8 * u, 8 * u)
                ctx.roundedRect(68 * u, 42 * u, 14 * u, 34 * u, 7 * u, 7 * u)
                ctx.roundedRect(18 * u, 50 * u, 64 * u, 46 * u, 16 * u, 16 * u)
                ctx.fill()
                ctx.beginPath()
                ctx.strokeStyle = "black"
                ctx.lineWidth = Math.max(1, 3 * u)
                ctx.lineCap = "round"
                ctx.moveTo(40 * u, 36 * u); ctx.lineTo(40 * u, 60 * u)
                ctx.moveTo(55 * u, 40 * u); ctx.lineTo(55 * u, 62 * u)
                ctx.moveTo(69 * u, 46 * u); ctx.lineTo(69 * u, 64 * u)
                ctx.stroke()
            }
        }
    }
}
