// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import QtQuick.Window 2.0
import Sailfish.Lipstick 1.0

// A stand-in for lipstick's compositor.qml: the root's properties, the
// layers and the items Frogfish reads or moves, with the same names and
// shapes; nothing else. Sizes are explicit: the window is never shown.
Window {
    id: root

    width: 1000
    height: 2000

    // The window in front's wrapper; its "window" is the surface's item
    property QtObject topmostWindow: null
    property int topmostWindowAngle: 0
    property real keyboardHeight: 0
    property bool deviceIsLocked: false
    property bool homeActive: false
    property Item wallpaper: wallpaperItem
    property Item homeLayer: homeItem
    property Item appLayer: appLayerItem
    property Item lockScreenLayer: lockScreenItem
    property Item dialogLayer: dialogItem
    property Item alarmLayer: alarmItem
    property Item launcherLayer: launcherItem
    property Item topMenuLayer: topMenuItem
    // Lipstick turns a right click into Key_Back while this is on
    property bool synthesizeBackEvent: true
    // Lipstick's side strips, set as in compositor.qml (0: not set)
    property real peekBoundaryWidth: 0
    PeekFilter.boundaryWidth: peekBoundaryWidth

    signal windowAdded(QtObject window)

    LayersParent {
        objectName: "layersParent"
        width: root.width
        height: root.height

        Item {
            id: wallpaperItem
            objectName: "wallpaper"
            width: root.width
            height: root.height
        }

        Item {
            id: homeItem
            objectName: "home"
            anchors.fill: parent

            property Item statusBar: statusBarItem

            Item {
                id: statusBarItem
                objectName: "statusBar"
                width: parent.width
                height: 100
            }

            Item {
                objectName: "Switcher_wrapper"
                anchors.fill: parent

                Item {
                    objectName: "cards"
                    property int largeRows: 3
                    property real _pageHeight: root.height
                    property int columnsUpdated: 0
                    function updateColumns() { columnsUpdated++ }
                }
            }

            Item {
                objectName: "EventsView_window"
                anchors.fill: parent
            }
        }

        Item {
            id: appLayerItem
            objectName: "apps"
            width: root.width
            height: root.height

            property bool _closeDismissedWindow: false
            property int closed: 0
            function close() { closed++ }

            Item {
                objectName: "appWrapper"
                width: root.width
                height: root.height

                property Item window: appItem

                Item {
                    id: appItem
                    objectName: "app"
                    width: root.width
                    height: root.height

                    property bool isInProcess: false
                    property string category: ""
                    property string title: "App"
                }
            }
        }

        Item {
            id: lockScreenItem
            objectName: "lockScreen"
            width: root.width
            height: root.height
            property bool exposed: false
        }

        Item {
            id: dialogItem
            objectName: "dialogs"
            width: root.width
            height: root.height
            property bool exposed: false
            property bool renderDialogBackground: false
            // The panel, at the window's top
            property rect backgroundRect: Qt.rect(0, 0, root.width, 600)
        }

        Item {
            id: alarmItem
            objectName: "alarms"
            width: root.width
            height: root.height
            property bool exposed: false
            property bool renderDialogBackground: false
        }

        Item {
            id: launcherItem
            objectName: "launcher"
            width: root.width
            height: root.height
            property bool exposed: false
            property int hidden: 0
            function hide() { hidden++; exposed = false }

            // The pager (a SilicaListView) and its icon grid
            Item {
                id: pager
                objectName: "pager"
                width: parent.width
                height: parent.height
                property QtObject openedChildFolder: null
                property int snapMode: 0

                Item {
                    objectName: "Launcher"
                    property bool rootFolder: true
                    property real pageHeight: pager.height
                    property real minimumCellHeight: 250
                    // Silica's rule: at least 6 in portrait
                    property int rows: Math.max(6, Math.floor(pageHeight / minimumCellHeight))
                }
            }
        }

        Item {
            id: topMenuItem
            objectName: "topMenuLayer"
            width: root.width
            height: root.height
            property bool exposed: false
            property int shown: 0
            function show() { shown++ }
            property int hidden: 0
            function hide() { hidden++; exposed = false }
            property Item topMenu: menu

            Item {
                width: parent.width
                height: parent.height

                Item {
                    id: menu
                    objectName: "menu"
                    width: parent.width
                    implicitHeight: 1200
                    height: Math.min(implicitHeight, parent.height)
                }
            }
        }

        Item {
            objectName: "LayersParent_cameraArea"
            width: root.width
            height: root.height
        }
    }

    // Maliit's window
    Item {
        objectName: "keyboard"
        y: root.height - 500
        width: root.width
        height: 500
        property int windowId: 7
        property string category: "overlay"
    }

    // The notification banners' layer
    Item {
        objectName: "notifications"
        width: root.width
        height: root.height
        property bool __compositor_is_layer: true

        Item {
            objectName: "NotificationPreview_popupArea"
            width: root.width
            height: 300
            opacity: 0
        }
    }
}
