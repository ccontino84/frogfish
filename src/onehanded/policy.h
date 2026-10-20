// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef FROGFISH_POLICY_H
#define FROGFISH_POLICY_H

// What one-handed mode does with what is in front (OneHandedMode): plain
// values in, decisions out. Lipstick is read in LipstickScene; the rules are
// here, in one place, and tested on the host.
namespace OneHandedPolicy {

// What lipstick shows, from compositor.qml
struct Scene
{
    // The lock screen layer is shown
    bool lockScreen = false;
    // The device lock is on: no gestures, also with the camera over it
    bool deviceLocked = false;
    // The alarm layer is above the dialog layer. A panel is a window with
    // lipstick's dialog background (Sailfish.Lipstick's SystemDialog: package
    // install, USB mode); without it, a full-screen call or alarm.
    bool alarm = false;
    bool alarmPanel = false;
    bool dialog = false;
    bool dialogPanel = false;
    // The app grid
    bool launcher = false;
    bool topMenu = false;
    bool homeActive = false;
    // Lipstick's topmost window is an application's (not lipstick's own, nor
    // a call, an alarm or a dialog)
    bool application = false;
};

enum class Front {
    LockScreen,
    // A full-screen call or alarm
    Alert,
    // A panel dialog, in the dialog or the alarm layer
    Dialog,
    Launcher,
    Application,
    Home,
    // The top menu over none of these
    Other
};

// In lipstick's order: what is shown on top wins
Front front(const Scene &scene);
// For the traces
const char *name(Front front);

// One-handed mode's own state
struct State
{
    // On, in portrait
    bool lowered = false;
    // The keyboard is open
    bool keyboard = false;
    // The application in front is moved down
    bool moved = false;
    bool launcherLowered = false;
    bool homeLowered = false;
    // A hold and drag on the edge switches one-handed mode on (it is off)
    bool edgeSwitchesOn = false;
};

// Where the picture in front starts
enum class Placement {
    // Nothing lowered: no cover
    FullScreen,
    // At the offset everything shares
    Lowered,
    // An application shrunk from the bottom corner with the keyboard
    AboveKeyboard
};

Placement placement(const Scene &scene, const State &state);
// The dialog or alarm layer in front is moved and scaled with everything
bool dialogFollows(const Scene &scene, const State &state);
// The notification banners go where the picture is
bool bannersFollow(const Scene &scene, const State &state);
// The keyboard has the application's shrink
bool keyboardShrunk(const Scene &scene, const State &state);
// The black area above the picture is one-handed mode's: swipes down from it,
// and a tap
bool ownsEmptyArea(const Scene &scene, const State &state);

enum class Tap { SwitchOff, DismissDialog };
// A tap on the black area
Tap emptyAreaTap(const Scene &scene);

// The corner's hold
bool triggerAllowed(const Scene &scene, const State &state);

enum class Edge {
    None,
    // Along the side, below the picture's top
    Side,
    // Along the side, above the keyboard
    AboveKeyboard
};
// The edge's hold and drag
Edge edge(const Scene &scene, const State &state);

}

#endif
