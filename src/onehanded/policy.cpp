// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "policy.h"

namespace OneHandedPolicy {

Front front(const Scene &scene)
{
    if (scene.lockScreen)
        return Front::LockScreen;
    // The alarm layer above the dialog layer
    if (scene.alarm)
        return scene.alarmPanel ? Front::Dialog : Front::Alert;
    if (scene.dialog)
        return scene.dialogPanel ? Front::Dialog : Front::Alert;
    if (scene.launcher)
        return Front::Launcher;
    // The top menu is over the application or home, which stays topmost
    if (scene.application)
        return Front::Application;
    // homeActive is not always set right after a restart
    if (scene.homeActive || !scene.topMenu)
        return Front::Home;
    return Front::Other;
}

const char *name(Front front)
{
    switch (front) {
    case Front::LockScreen:
        return "lock screen";
    case Front::Alert:
        return "call or alarm";
    case Front::Dialog:
        return "dialog";
    case Front::Launcher:
        return "app grid";
    case Front::Application:
        return "application";
    case Front::Home:
        return "home";
    case Front::Other:
        return "other";
    }
    return "?";
}

Placement placement(const Scene &scene, const State &state)
{
    if (!state.lowered)
        return Placement::FullScreen;
    switch (front(scene)) {
    case Front::LockScreen:
    case Front::Alert:
        return Placement::FullScreen;
    case Front::Dialog:
        // Back to full screen while typing, like an application
        return state.keyboard ? Placement::FullScreen : Placement::Lowered;
    case Front::Launcher:
        return state.launcherLowered ? Placement::Lowered : Placement::FullScreen;
    case Front::Application:
        if (!state.moved)
            return Placement::FullScreen;
        return state.keyboard ? Placement::AboveKeyboard : Placement::Lowered;
    case Front::Home:
    case Front::Other:
        return Placement::Lowered;
    }
    return Placement::FullScreen;
}

bool dialogFollows(const Scene &scene, const State &state)
{
    return front(scene) == Front::Dialog && placement(scene, state) == Placement::Lowered;
}

bool bannersFollow(const Scene &scene, const State &state)
{
    // Not while typing: the banner places itself above the keyboard
    return !state.keyboard && placement(scene, state) == Placement::Lowered;
}

bool keyboardShrunk(const Scene &scene, const State &state)
{
    return front(scene) == Front::Application && state.moved && state.lowered
            && !scene.deviceLocked;
}

bool ownsEmptyArea(const Scene &scene, const State &state)
{
    if (placement(scene, state) == Placement::FullScreen)
        return false;
    switch (front(scene)) {
    case Front::Dialog:
    case Front::Launcher:
    case Front::Application:
        return true;
    case Front::Home:
        // Once home is laid out below the same top
        return state.homeLowered;
    default:
        return false;
    }
}

Tap emptyAreaTap(const Scene &scene)
{
    // Above a dialog, like a tap outside its panel
    return front(scene) == Front::Dialog ? Tap::DismissDialog : Tap::SwitchOff;
}

bool triggerAllowed(const Scene &scene, const State &state)
{
    // Not on the lock screen (already usable with one hand), not over a call
    // or an alarm, and not while typing: it would take long presses on the
    // keyboard's corner key
    const Front shown = front(scene);
    return shown != Front::LockScreen && shown != Front::Alert && !scene.deviceLocked
            && !state.keyboard;
}

Edge edge(const Scene &scene, const State &state)
{
    const Front shown = front(scene);
    if (shown == Front::LockScreen || shown == Front::Alert || scene.deviceLocked)
        return Edge::None;
    if (state.keyboard) {
        // While typing only over an application, whose size the keyboard
        // shares
        const bool on = state.moved && state.lowered;
        return shown == Front::Application && (on || state.edgeSwitchesOn)
                ? Edge::AboveKeyboard : Edge::None;
    }
    if (state.edgeSwitchesOn) {
        // Off: over what can be lowered
        return shown == Front::Application || shown == Front::Home || shown == Front::Launcher
                || shown == Front::Dialog ? Edge::Side : Edge::None;
    }
    // On: over what is lowered
    if (!state.lowered)
        return Edge::None;
    switch (shown) {
    case Front::Application:
        return state.moved ? Edge::Side : Edge::None;
    case Front::Home:
        return state.homeLowered ? Edge::Side : Edge::None;
    case Front::Launcher:
        return state.launcherLowered ? Edge::Side : Edge::None;
    case Front::Dialog:
        return Edge::Side;
    default:
        return Edge::None;
    }
}

}
