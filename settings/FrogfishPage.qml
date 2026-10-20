// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import Nemo.DBus 2.0
import "defaults.js" as Defaults

// Options that only matter after a choice on this page are hidden until it
// is made (the size until a one-handed mode is chosen).
Page {
    id: page

    // Keys and defaults must match the lipstick plugin (settings.h); the
    // defaults are in defaults.js
    ConfigurationGroup {
        id: config

        path: "/apps/frogfish"

        // "none", "pull", "shrink" or "pullshrink"
        property string onehanded_mode: Defaults.values.onehanded_mode
        // Percent of the screen's height the picture keeps
        property int onehanded_size: Defaults.values.onehanded_size
        property bool zoom: Defaults.values.zoom
        property bool back: Defaults.values.back
        // "right" or "left"
        property string hand: Defaults.values.hand
    }

    // Silica's Slider and ComboBox replace a plain binding when the user
    // moves or picks; these keep following the settings (Reset to defaults,
    // or the top menu switch)
    Binding { target: modeCombo; property: "currentIndex"; value: page.noMode ? 0 : page.shrinkMode ? 2 : page.pullShrinkMode ? 3 : 1 }
    Binding { target: sizeSlider; property: "value"; value: config.onehanded_size }
    Binding { target: handCombo; property: "currentIndex"; value: page.leftHanded ? 1 : 0 }

    // Anything but "none", "shrink" and "pullshrink" is pull, anything but
    // "left" is right, as in the plugin
    readonly property bool noMode: config.onehanded_mode === "none"
    readonly property bool shrinkMode: config.onehanded_mode === "shrink"
    readonly property bool pullShrinkMode: config.onehanded_mode === "pullshrink"
    readonly property bool leftHanded: config.hand === "left"
    readonly property string handSide: leftHanded ? "left" : "right"
    // Back is on the other edge
    readonly property string backSide: leftHanded ? "right" : "left"

    // Whether the plugin runs in the home screen, checked once when the page
    // opens: FrogfishController::status() ("active", "starting", "no-window",
    // "crash-guard"), or without a reply "not-running" or "unsupported"; empty
    // until known
    property string pluginStatus: ""
    // Of the running plugin; empty when it does not reply
    property string pluginVersion: ""
    // FrogfishController::modeStatus(): "on", "off", "crash-guard" or
    // "unsupported"
    property string oneHandedStatus: ""
    property string zoomStatus: ""
    property string backStatus: ""
    readonly property string pluginProblem: {
        switch (pluginStatus) {
        case "unsupported":
            return "Not supported on this device."
        case "not-running":
            return "Not running. Restart the phone to start it."
        case "crash-guard":
            return "Turned off after the home screen failed to start."
        case "no-window":
            return "Couldn't attach to the home screen."
        }
        return ""
    }

    // No reply: the package only writes the environment file that loads the
    // add-ons when the device does not load plugins of its own the same way
    // (config/update-env)
    function checkEnvironmentFile() {
        var request = new XMLHttpRequest()
        request.onreadystatechange = function() {
            if (request.readyState === XMLHttpRequest.DONE) {
                page.pluginStatus = request.responseText.indexOf("lipstickaddons") >= 0
                        ? "not-running" : "unsupported"
            }
        }
        request.open("GET", "file:///var/lib/environment/compositor/01-frogfish.conf")
        request.send()
    }

    function queryStatus() {
        plugin.typedCall("status", [],
                         function(status) {
                             page.pluginStatus = status
                             plugin.typedCall("version", [],
                                              function(version) { page.pluginVersion = version })
                             queryModes()
                         },
                         function() { page.checkEnvironmentFile() })
    }

    function queryModes() {
        plugin.typedCall("modeStatus", [{ type: "s", value: "onehanded" }],
                         function(status) { page.oneHandedStatus = status })
        plugin.typedCall("modeStatus", [{ type: "s", value: "zoom" }],
                         function(status) { page.zoomStatus = status })
        plugin.typedCall("modeStatus", [{ type: "s", value: "back" }],
                         function(status) { page.backStatus = status })
    }

    function resetCrashGuard() {
        plugin.typedCall("resetCrashGuard", [], function() { page.queryStatus() })
    }

    // The plugin's diagnostics, or fallbackDiagnostics()
    function collectDiagnostics(done) {
        plugin.typedCall("diagnostics", [],
                         function(text) { done(text) },
                         function() {
                             var request = new XMLHttpRequest()
                             request.onreadystatechange = function() {
                                 if (request.readyState === XMLHttpRequest.DONE)
                                     done(fallbackDiagnostics(request.responseText || ""))
                             }
                             request.open("GET", "file:///etc/os-release")
                             request.send()
                         })
    }

    // What the page knows when the plugin doesn't reply
    function fallbackDiagnostics(osRelease) {
        var lines = []
        var system = /^PRETTY_NAME="?([^"\n]*)/m.exec(osRelease)
        lines.push("system " + (system && system[1] !== "" ? system[1] : "unknown"))
        switch (page.pluginStatus) {
        case "unsupported":
            lines.push("status unsupported (01-frogfish.conf missing)")
            break
        case "not-running":
            lines.push("status not-running (01-frogfish.conf present)")
            break
        default:
            lines.push("status " + (page.pluginStatus || "unknown"))
        }
        return lines.join("\n")
    }

    function setMode(mode) {
        config.onehanded_mode = mode
        page.queryModes()
    }

    // Every key back to its default: the page's own values first (an unset
    // key doesn't change them), then everything under the path, hidden keys
    // too. The plugin then reads its settings again.
    function resetSettings() {
        for (var key in Defaults.values)
            config[key] = Defaults.values[key]
        config.clear()
        config.sync()
        plugin.typedCall("reload", [])
    }

    function openDiagnostics() {
        collectDiagnostics(function(text) {
            pageStack.push(Qt.resolvedUrl("DiagnosticsPage.qml"), { text: text })
        })
    }

    // A new GitHub issue in the browser, from the bug report form
    // (.github/ISSUE_TEMPLATE/bug_report.yml) with the diagnostics filled in
    function reportBug() {
        collectDiagnostics(function(diagnostics) {
            Qt.openUrlExternally("https://github.com/ccontino84/frogfish/issues/new"
                                 + "?template=bug_report.yml"
                                 + "&diagnostics=" + encodeURIComponent(diagnostics))
        })
    }

    Component.onCompleted: queryStatus()

    DBusInterface {
        id: plugin

        // FrogfishStatusService, on the session bus
        service: "io.github.ccontino84.frogfish"
        path: "/frogfish"
        iface: "io.github.ccontino84.frogfish"
    }

    // A mode that stays off: turned off by the in-use crash guard (with a
    // way to try again), or lipstick lacks what it needs. The Loader's
    // status tells which.
    Component {
        id: modeProblem

        Column {
            readonly property bool crashGuard: parent && parent.status === "crash-guard"

            width: page.width
            spacing: Theme.paddingMedium

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: crashGuard ? "Turned off after repeated crashes."
                                 : "Not supported on this Sailfish OS version."
            }

            Button {
                visible: crashGuard
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Try again"
                onClicked: page.resetCrashGuard()
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        VerticalScrollDecorator {}

        PullDownMenu {
            MenuItem {
                text: "Reset to defaults"
                onClicked: Remorse.popupAction(page, "Reset to defaults", page.resetSettings)
            }
            MenuItem {
                text: "Report a bug"
                onClicked: page.reportBug()
            }
        }

        Column {
            id: column

            width: parent.width

            PageHeader {
                title: "Frogfish gestures"
                description: page.pluginVersion !== "" ? "Version " + page.pluginVersion : ""

                // Hidden, for support
                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: page.openDiagnostics()
                }
            }

            // Only when there is a problem; the settings stay editable
            Column {
                visible: page.pluginProblem !== ""
                width: parent.width
                spacing: Theme.paddingMedium

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    color: Theme.highlightColor
                    text: page.pluginProblem
                }

                Button {
                    visible: page.pluginStatus === "crash-guard"
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Reset"
                    onClicked: plugin.typedCall("resetCrashGuard", [],
                                                function() { page.pluginStatus = "not-running" })
                }

                Item {
                    width: 1
                    height: Theme.paddingMedium
                }
            }

            ComboBox {
                id: handCombo
                label: "Preferred hand"
                description: "The side on which the gestures are triggered."

                menu: ContextMenu {
                    MenuItem {
                        text: "Right"
                        onClicked: config.hand = "right"
                    }
                    MenuItem {
                        text: "Left"
                        onClicked: config.hand = "left"
                    }
                }
            }

            ComboBox {
                id: modeCombo
                label: "One-handed"
                description: page.noMode ? ""
                             : "Hold the bottom " + page.handSide + " corner to "
                               + (page.shrinkMode ? "shrink the screen"
                                  : page.pullShrinkMode ? "pull the screen down and shrink it"
                                  : "pull the screen down")
                               + ", or hold the " + page.handSide + " edge and drag to adjust manually."

                menu: ContextMenu {
                    MenuItem {
                        text: "None"
                        onClicked: page.setMode("none")
                    }
                    MenuItem {
                        text: "Pull"
                        onClicked: page.setMode("pull")
                    }
                    MenuItem {
                        text: "Shrink"
                        onClicked: page.setMode("shrink")
                    }
                    MenuItem {
                        text: "Pull and shrink"
                        onClicked: page.setMode("pullshrink")
                    }
                }
            }

            Loader {
                readonly property string status: page.oneHandedStatus
                active: !page.noMode && (status === "crash-guard" || status === "unsupported")
                sourceComponent: modeProblem
            }

            Slider {
                id: sizeSlider
                visible: !page.noMode
                width: parent.width
                label: "Size"
                // FrogfishSettings::MinimumSize..MaximumSize
                minimumValue: 45
                maximumValue: 75
                stepSize: 5
                valueText: Math.round(sliderValue) + " %"
                onSliderValueChanged: {
                    // Only a value in range: while the slider is created, the
                    // minimum clamps the initial 0 before the Binding applies,
                    // and that would be written
                    if (value !== sliderValue)
                        return
                    var size = Math.round(sliderValue)
                    if (size !== config.onehanded_size)
                        config.onehanded_size = size
                }
            }

            TextSwitch {
                text: "Zoom"
                description: "Hold the bottom edge and drag up to zoom in; hold it again to zoom out."
                checked: config.zoom
                automaticCheck: false
                onClicked: {
                    config.zoom = !checked
                    page.queryModes()
                }
            }

            Loader {
                readonly property string status: page.zoomStatus
                active: config.zoom && (status === "crash-guard" || status === "unsupported")
                sourceComponent: modeProblem
            }

            TextSwitch {
                text: "Back"
                description: "Hold the " + page.backSide + " edge, then lift, to go back."
                checked: config.back
                automaticCheck: false
                onClicked: {
                    config.back = !checked
                    page.queryModes()
                }
            }

            Loader {
                readonly property string status: page.backStatus
                active: config.back && (status === "crash-guard" || status === "unsupported")
                sourceComponent: modeProblem
            }

        }
    }
}
