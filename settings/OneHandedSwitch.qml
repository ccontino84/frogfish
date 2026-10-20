// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

import QtQuick 2.0
import Sailfish.Silica 1.0
import com.jolla.settings 1.0
import Nemo.Configuration 1.0
import "defaults.js" as Defaults

// Top menu switch: whether one-handed mode is on right now, and a tap does
// what the corner does. The plugin writes the state to onehanded_active; a
// tap writes the other one, which the plugin follows, or writes back where
// the corner wouldn't work either.
SettingsToggle {
    id: root

    name: "One-handed"
    icon.source: checked ? "image://theme/icon-m-frogfish" : "image://theme/icon-m-frogfish-off"
    checked: !!activeConfig.value

    onToggled: {
        if (modeConfig.value !== "none")
            activeConfig.value = !checked
    }

    menu: ContextMenu {
        SettingsMenuItem {
            onClicked: root.goToSettings()
        }
    }

    ConfigurationValue {
        id: activeConfig

        key: "/apps/frogfish/onehanded_active"
        defaultValue: false
    }

    ConfigurationValue {
        id: modeConfig

        key: "/apps/frogfish/onehanded_mode"
        defaultValue: Defaults.values.onehanded_mode
    }
}
