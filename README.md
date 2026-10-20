# Frogfish

Gestures for Sailfish OS that bring the whole screen within reach of one
hand:

- **One-handed mode**, in one of three ways. Hold a bottom corner to switch
  it on, and again to switch it off; or hold the side edge and drag to set
  it gradually:
  - **Pull**: pulls the screen down within reach of your thumb.
  - **Shrink**: shrinks the screen towards your thumb.
  - **Pull and shrink**: pulls it down to the same place and shrinks it a
    little, so more of it stays in view than with Pull.

  A tap on the empty area goes back. Calls and alarms always show full
  screen. A switch in the top menu does the same as the corner and shows
  whether one-handed mode is on.
- **Back** (off at first): hold the side edge away from your preferred hand
  (the left one for right-handed), then lift, to go back. It also closes
  the app grid and the top menu, and the keyboard in Android apps.
  Sailfish's own side swipes stay as they are.
- **Zoom** (off at first): hold the bottom edge and drag up to magnify the
  screen, up to 8×. While zoomed, drag up from the edge to zoom in further,
  or hold it to zoom out until zoom turns off. Pinch to zoom and move; a
  button in the corner switches between moving the view and touching the
  apps.

Frogfish works alongside other home screen add-ons such as
[Owlfish](https://github.com/ccontino84/owlfish).

**Status:** released; what changed in each version is in
[CHANGELOG.md](CHANGELOG.md).

Frogfish was written with an AI coding assistant (Claude). It was designed,
reviewed and tested on real devices by the author.

## Install

1. Download the RPM for your device from the
   [latest release](https://github.com/ccontino84/frogfish/releases/latest):
   `aarch64` for most current devices (Jolla Phone, Xperia 10 II/III/IV and
   later), `armv7hl` for 32-bit installations.
2. In **Settings > Untrusted software**, turn on *Allow untrusted software*.
3. Open the downloaded file (from the browser's downloads or the Files app)
   and install it.
4. **Restart the phone.** The home screen only loads Frogfish when it starts.
5. Open **Settings > Frogfish gestures** (under Look and feel). One-handed mode
   (pull) is on, zoom and back are off. To add the top menu switch, go to Settings > Top
   menu and turn on "One-handed" under *Switches*.

**Upgrading** works the same way: install the new RPM over the old one and
restart the phone. Your settings are kept.

With Developer mode, the same from a terminal:

```sh
devel-su pkcon install-local frogfish-*.rpm
systemctl --user restart lipstick     # restarts the home screen; closes open apps
```

## Using it

**Settings > Frogfish gestures:**

- **Preferred hand**: right (default) or left: the corner, the edge and the
  zoom button are on that side; back is on the other edge.
- **One-handed**: *None*, *Pull* (default), *Shrink* or *Pull and shrink*.
  - **Size**: 45 to 75 % of the screen's height, default 60 %: how far the
    corner and the top menu switch bring the screen down. The edge drag
    goes from 45 % to full size.
- **Zoom**: on or off (default off).
- **Back**: on or off (default off). With the keyboard open, Android apps
  close the keyboard first; Sailfish apps go back a page, as their page
  swipe does (a swipe down closes only the keyboard). Sailfish's own
  browser doesn't react to back.

The top menu switch (Settings > Top menu) pulls or shrinks the screen and
puts it back, like the corner, and shows whether one-handed mode is on; a
long press opens the page. With *None* it does nothing.

The pull-down menu has **Reset to defaults** and **Report a bug** (see
below).

## If something goes wrong

The settings page shows a warning at the top when Frogfish is not running.
To report a problem, pull down the settings page and tap **Report a bug**:
the browser opens GitHub's bug report form with your Sailfish OS version and
Frogfish's diagnostics filled in; describe the problem and submit it (needs
a GitHub account). Without an account, double-tap the title of the settings
page, tap **Copy** and paste the text into your report.

- **"Not supported on this device."** Your device's own configuration
  loads Qt plugins through the same setting as Frogfish
  (`QT_QPA_GENERIC_PLUGINS`). Frogfish gives way rather than switch off
  your device's plugins, so it does nothing. Please report it in the
  [issues](https://github.com/ccontino84/frogfish/issues) with your
  device's name.
- **"Not running. Restart the phone to start it."** It was installed or
  updated since the last restart.
- **"Turned off after the home screen failed to start."** The startup crash
  guard (see below) has turned Frogfish off. Tap **Reset** and restart the
  phone to try again.
- **"Turned off after repeated crashes."** under a gesture:
  the home screen stopped three times in a row while that gesture was in
  use, so it was turned off. Tap **Try again**, and please report it.
- **"Not supported on this Sailfish OS version."** under a gesture:
  the home screen lacks something that gesture needs, typically after
  a system update, so it stays off rather than half work. Please report it
  with the diagnostics.

**Touch, mouse or keyboard stop working after installing or a system
update.** Unlikely, as the installer checks for the conflict above. To
recover over SSH (USB or Wi-Fi, with Developer mode and a password set):

```sh
devel-su rm /var/lib/environment/compositor/01-frogfish.conf   # or: devel-su rpm -e frogfish
devel-su reboot
```

## Uninstall

Frogfish has no app icon, so it is removed from a terminal (Developer mode):

```sh
devel-su pkcon remove frogfish
systemctl --user restart lipstick
```

Settings stay in dconf; remove them with `dconf reset -f /apps/frogfish/`.

## Safety and privacy

Frogfish is a plugin that the home screen (lipstick) loads at startup. It
modifies no system file: it only adds its own, including an environment
file that tells lipstick to load it.

- **Startup crash guard:** each home screen start counts as unhealthy until
  Frogfish has run for 30 s. After 3 unhealthy starts in a row Frogfish
  stays off until reset on its settings page.
- **In-use crash guard:** if the home screen stops while a gesture is in use
  (one-handed mode on, zoomed, or during a back hold), that counts against the gesture; switching
  the phone off does not. After 3 in a row it stays off until you try again
  on the settings page.
- **Only in the home screen:** the plugin does nothing in any other process.
- **Privacy:** the gestures look at touches only to recognise their own
  holds. What they decide is logged in `~/.cache/frogfish/log.txt`, for
  bug reports. Nothing leaves the phone.

## Development

Building, testing, how it works inside: [DEVELOPMENT.md](DEVELOPMENT.md).

## License

GNU Lesser General Public License, version 2.1 only (LGPL-2.1-only, the same
as lipstick), see [LICENSE](LICENSE).
Copyright (c) 2026 [ccontino84](https://github.com/ccontino84).
