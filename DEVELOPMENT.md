# Frogfish development

## How it works

Frogfish is a Qt generic plugin that the home screen compositor (lipstick)
loads at startup. It finds the compositor's window and works on its scene
and its touches from inside the process. Library paths are for aarch64;
armv7hl uses `/usr/lib`.

| File | Purpose |
|---|---|
| `/usr/lib64/qt5/plugins/generic/libfrogfish.so` | the plugin, keys `frogfish` and `lipstickaddon-frogfish` |
| `/usr/lib64/qt5/plugins/generic/liblipstickaddons-frogfish.so` | the add-on loader, key `lipstickaddons` ([lipstick-addons.md](lipstick-addons.md)) |
| `/var/lib/environment/compositor/01-frogfish.conf` | `QT_QPA_GENERIC_PLUGINS=owlfish,lipstickaddons`, read by `lipstick.service`; not shipped (`%ghost`), written by `update-env` |
| `/usr/libexec/frogfish/update-env` | run by `%post`: writes the file above, unless the device's own files set the variable (then it warns and exits 1) |
| `/usr/share/jolla-settings/entries/frogfish.json`, `pages/frogfish/` | the settings page and the one-handed top menu switch |
| `~/.cache/frogfish/` | the crash guards' files and `log.txt` |

### Loading

`01-frogfish.conf` sorts after Owlfish's `00-owlfish.conf` and wins, and
names both Owlfish (up to 1.5 it loads only by its own key) and the loader.
The loader starts every installed plugin with a `lipstickaddon-` key, so
Frogfish and later add-ons share one value. Without Owlfish installed, Qt
only logs "No such plugin for spec owlfish". Owlfish 1.5's own `update-env`
accepts this file (its value names `owlfish`), so both install in any
order. The contract is in [lipstick-addons.md](lipstick-addons.md).

### Source layout

| Folder | What |
|---|---|
| `src/core/` | the controller (finds the window, owns the modes), settings, crash guards, D-Bus status, log, units (mm), haptics, QML helpers |
| `src/lipstick/` | `LipstickScene`: every read of lipstick's QML (the compositor window's properties, items by objectName, class or marker), the hooks found or missing |
| `src/input/` | `InputRouter`, the only event filter: a touch goes to zoom, then to back, then (unless zoomed) to one-handed mode; `hold.*`: what a hold is (`HoldGesture`: 0.5 s, 3 mm, 1.5 s at most, the corner and strip sizes), the same for every gesture, and held touches with their replay; `holdcue.*` and `Cue.qml`: the ring once a hold is recognised, two-tone so it shows on any background, kept under its finger by the router while it plays |
| `src/onehanded/` | one-handed mode (pull, shrink, or both); the rules for what is in front (`policy.*`, plain values, tested on the host); `moveditem.*`, which puts every changed lipstick item back as it was |
| `src/back/` | the back gesture: a hold on lipstick's side strip away from the hand; lipstick's own back (a right click becomes `Key_Back` for the window in front), or the app grid or top menu closed |
| `src/zoom/` | the zoom; `Overlay.qml` (border, level, mode button) and `Colors.qml` (Silica's theme, optional) |
| `src/frogfish.qrc` | Frogfish's own QML, loaded from `qrc:/frogfish/` into lipstick's engine |
| `src/plugin.cpp` | the plugin: process check, once per process |
| `loader/` | the lipstickaddons loader; keep it identical to the other add-ons' copies |
| `settings/` | the settings page (QML), its defaults (`defaults.js`) and the top menu switch |
| `config/update-env` | the environment file check (POSIX sh: RPM scriptlets may run in BusyBox) |
| `icons/` | `icon-m-frogfish` (filled, on) and `icon-m-frogfish-off` (outlined, off), one contour; PNGs for every Silica scale by `render.sh` |

### Crash guards

- **Startup:** every compositor start counts as unhealthy in
  `~/.cache/frogfish/unhealthy-starts` until the plugin has run on the window
  for 30 s. After 3 in a row the plugin stays inactive (status
  `crash-guard`) until reset from the page or D-Bus.
- **In use:** a mode writes `~/.cache/frogfish/in-use-<mode>` (the time)
  while it has changed the scene, and removes it once everything is put
  back. A start that finds a mark counts a crash in `crashes-<mode>`,
  unless the mark is older than the system's boot (`/proc/stat` `btime`:
  the phone was switched off). A clean end resets the count. After 3 in a
  row the mode stays off (`modeStatus` = `crash-guard`), its setting
  unchanged, until reset.

### Hooks (lipstick's side)

At start `LipstickScene` checks the compositor window's properties. A mode
whose required ones are missing stays off (`modeStatus` = `unsupported`);
the optional ones only lose their own part. The diagnostics line
`lipstick: missing ...` lists the missing properties and the items not
found when they were first needed.

| Property | Needed by |
|---|---|
| `homeLayer`, `topMenuLayer` | one-handed |
| `topmostWindow` | one-handed, back |
| `topmostWindowAngle`, `keyboardHeight`, `lockScreenLayer` | one-handed, zoom, back |
| `synthesizeBackEvent` | back |
| `deviceIsLocked`, `homeActive`, `dialogLayer`, `alarmLayer`, `launcherLayer`, `appLayer`, `wallpaper` | optional |

Items found by name or class, all optional: `LayersParent_cameraArea` (the
cover's parent), `Launcher` (the app grid's pager), `Switcher_wrapper`,
`EventsView_window`, `NotificationPreview_popupArea`, the item marked
`__compositor_is_layer` (banners), `BlurSource_QML*`, `LayersParent*` (drawn into a layer while zoomed).
The side strips' width is the root's attached `PeekFilter.boundaryWidth`
(set in compositor.qml), read through the root's QML context; without it
5 % of the width, and the diagnostics list `PeekFilter`.
Nothing of Frogfish is left on lipstick's objects: transforms are appended
and removed again, changed properties are put back to their earlier
values.

## Settings (dconf)

```sh
dconf write /apps/frogfish/onehanded_mode "'shrink'"  # none, pull (default), shrink or pullshrink
dconf write /apps/frogfish/onehanded_size 50          # percent of the height kept by the corner and the top menu; 45-75, default 60
dconf write /apps/frogfish/zoom true                  # default false
dconf write /apps/frogfish/back true                  # default false
dconf write /apps/frogfish/hand "'left'"              # right (default) or left
dconf write /apps/frogfish/zoom_max 4                 # hidden: highest zoom level, 1.5-15, default 8
dconf read /apps/frogfish/onehanded_active            # written by the plugin: on right now
dconf write /apps/frogfish/onehanded_active true      # what the top menu switch does: asks for on (or off)
dconf reset -f /apps/frogfish/                        # back to defaults, as the page's Reset does; then
dbus-send --session --dest=io.github.ccontino84.frogfish /frogfish io.github.ccontino84.frogfish.reload
```

`onehanded_active` is no setting: the plugin writes it (false at every
start) whenever one-handed mode goes on or off, and follows a different
value written by someone else, as the corner would; where the corner
wouldn't work (mode none, lock screen, a call, typing, landscape) it writes
the state back. The edge drag goes from a fixed 45 % (`MinimumSize`) to full size, in every
mode.

Booleans: a missing value is the default; `false`, `0`, `off`, `no` and the
empty string are false. The page's defaults (`settings/defaults.js`) must
match `src/core/settings.h`; a test checks.

## Build

Use the Sailfish SDK with the 5.1.0.11 targets. They have the same Qt (5.6.3)
and mlite as 5.2.

```sh
sfdk config target=SailfishOS-5.1.0.11-aarch64   # see: sfdk tools list
sfdk build                                        # RPM lands in RPMS/
```

For a local test build, set `Version:` in `rpm/frogfish.spec` to
`<version>~alphaN` and build with `sfdk -c no-fix-version build`; otherwise
sfdk names the package from the latest git tag.

## Tests

Host build and tests (desktop Qt 5 with the QtQuick.Window module, needs
Xvfb):

```sh
mkdir build && cd build && qmake ../frogfish.pro && make
for test in core/tst_frogfish gestures/tst_gestures; do
    QT_QPA_PLATFORM=xcb QSG_RENDER_LOOP=basic dbus-run-session -- \
        xvfb-run -a -s "-screen 0 640x480x24" ./tests/$test > ${test#*/}.log 2>&1; tail -1 ${test#*/}.log
done
```

`QT_QPA_PLATFORM=xcb` matters in a Wayland session. Write the output to a
file rather than a pipe: services started on the test bus keep a pipe open
after the tests end. In host builds the settings come from `FROGFISH_<KEY>`
environment variables (e.g. `FROGFISH_ONEHANDED_MODE=shrink`) instead of
dconf.

`tests/core` covers the settings (defaults, the page's copy of them,
parsing, coalesced change notifications), both crash guards, the log, the
controller (finding the window, status, mode status, guards in the
diagnostics), the D-Bus service, loading through `QGenericPluginFactory`
(only in the compositor, once per process, through the loader by key
prefix) and `update-env` against sample environment files.

`tests/gestures` runs the gestures on a stand-in for lipstick's
compositor.qml (`tests/gestures/qml/Compositor.qml`: the root properties,
the layers, an app window, the keyboard, the items found by name), with
touches from `QTest`: the rules, the scene's hooks, pull, shrink, zoom
(alone and inside one-handed mode's picture), back, the router's order, that
every touched item is put back as it was (after each mode, and when removed
while on), and the controller (modes on attach, unsupported, settings
applied after a touch, in-use marks, the in-use guard, putting back before
lipstick quits).

Plugin options, for testing outside lipstick:
`QT_QPA_GENERIC_PLUGINS=lipstickaddons:class=<QQuickWindow subclass>:process=<exe or *>`
(the loader passes its specification on), or `frogfish:...` directly. The
defaults are `class=LipstickCompositor:process=lipstick`.

## Try it on a phone

Enable Developer mode and check that SSH works (USB `192.168.2.15`, or Wi-Fi).

```sh
scp RPMS/frogfish-[0-9]*.aarch64.rpm defaultuser@192.168.2.15:
ssh defaultuser@192.168.2.15
devel-su pkcon install-local frogfish-[0-9]*.aarch64.rpm
systemctl --user restart lipstick          # closes open apps
```

Close the Settings app if it was open, so it picks up the new page. Check:

```sh
devel-su sh -c 'grep -l libfrogfish /proc/[0-9]*/maps'   # must list only lipstick's pid
dbus-send --session --print-reply --dest=io.github.ccontino84.frogfish \
    /frogfish io.github.ccontino84.frogfish.status       # string "active"
dbus-send --session --print-reply --dest=io.github.ccontino84.frogfish \
    /frogfish io.github.ccontino84.frogfish.diagnostics  # also on the settings page: double-tap the header
tail ~/.cache/frogfish/log.txt
```

Sailfish OS keeps the journal in RAM and only 1 MB of it, so follow it live:

```sh
devel-su journalctl -f -n 0 --no-pager _COMM=lipstick   # e.g. "lipstickaddons: Started lipstickaddon-frogfish"
```

## Making a release

The release is built from its branch (`dev/<major.minor>`), and `main`
only moves once that has worked.

1. On the release branch: one commit per feature, each with its
   `CHANGELOG.md` bullet under `## <version> (unreleased)`. Then a release
   commit: `Version:` in `rpm/frogfish.spec` (local test builds carry a
   `~alphaN`, `~betaN` or `~rcN` suffix; the release has none), and the
   entry's heading dated, `## <version> (date)`. Push the branch.
2. On GitHub: **Actions > Release > Run workflow**, from the release branch.
   It checks that `v<Version>` does not exist yet, that `CHANGELOG.md` has
   its entry and that `main` can fast-forward to the commit. It then runs
   the tests, builds the RPMs for aarch64 and armv7hl against the Sailfish
   OS 5.1.0.11 SDK targets, and creates the release `v<Version>` (a draft by
   default) with them attached and the changelog entry as its text.
3. If it fails: fix it on the branch, delete the draft if one was created,
   push and run it again.
4. If it works: fast-forward `main` to the same commit
   (`git push origin dev/<major.minor>:main`), try the draft's RPMs on a
   phone, then publish the draft.

`ci/build-rpm.sh aarch64` (or `armv7hl`) runs the same build locally with
Docker; the RPM lands in `out/`.
