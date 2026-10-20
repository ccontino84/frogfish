# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

TEMPLATE = subdirs
SUBDIRS = src loader

# Host-side tests; the RPM build passes CONFIG+=notests
!notests {
    SUBDIRS += tests
    tests.depends = src loader
}

# Writes the compositor environment file that loads the add-ons; run by the
# RPM's %post (see config/update-env)
update_env.files = config/update-env
update_env.path = /usr/libexec/frogfish
update_env.CONFIG += executable
INSTALLS += update_env

# Settings app page and the one-handed top menu switch (jolla-settings)
settings_entries.files = settings/frogfish.json
settings_entries.path = /usr/share/jolla-settings/entries
settings_pages.files = settings/FrogfishPage.qml settings/DiagnosticsPage.qml \
    settings/OneHandedSwitch.qml settings/defaults.js
settings_pages.path = /usr/share/jolla-settings/pages/frogfish
INSTALLS += settings_entries settings_pages

# Monochrome icons for every Silica theme scale, so image://theme/icon-m-frogfish
# resolves (and gets tinted) like the stock icons: filled (on) and outlined
# (off). Regenerate with icons/render.sh.
for(scale, $$list(z1.0 z1.25 z1.5 z1.5-large z1.75 z2.0 z2.5)) {
    target = icon_$$replace(scale, [.-], _)
    $${target}.files = icons/$${scale}/icon-m-frogfish.png icons/$${scale}/icon-m-frogfish-off.png
    $${target}.path = /usr/share/themes/sailfish-default/silica/$${scale}/icons-monochrome
    INSTALLS += $${target}
}

OTHER_FILES += \
    rpm/frogfish.spec \
    config/update-env \
    settings/*.json \
    settings/*.qml \
    settings/*.js \
    icons/*.svg \
    icons/render.sh \
    README.md \
    CHANGELOG.md \
    DEVELOPMENT.md \
    lipstick-addons.md \
    LICENSE
