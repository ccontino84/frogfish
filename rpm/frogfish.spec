# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

Name:       frogfish
Version:    1.0.0~alpha24
Release:    1
Summary:    One-handed mode, zoom and back for the home screen
License:    LGPL-2.1-only
URL:        https://github.com/ccontino84/frogfish
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(mlite5)
Requires:   lipstick-qt5
# Settings page
Requires:   jolla-settings
Requires:   nemo-qml-plugin-dbus-qt5
Requires:   nemo-qml-plugin-configuration-qt5

%description
Gestures for using the phone with one hand: pull the screen down, shrink it
or both, within reach of your thumb; zoom into any part of the screen; go
back with a hold on the edge.
Loaded into the home screen compositor (lipstick) as a Qt generic plugin;
no system files are modified.

Set up in Settings > Frogfish gestures; one-handed mode also has a top menu switch.

%if 0%{?_chum}
Title: Frogfish
Type: addon
DeveloperName: ccontino84
Categories:
 - Utility
 - Accessibility
Custom:
  Repo: https://github.com/ccontino84/frogfish
PackageIcon: https://raw.githubusercontent.com/ccontino84/frogfish/main/icons/icon-m-frogfish.svg
Links:
  Homepage: https://github.com/ccontino84/frogfish
  Bugtracker: https://github.com/ccontino84/frogfish/issues
%endif

%prep
%autosetup -n %{name}-%{version}

%build
%qmake5 CONFIG+=notests FROGFISH_VERSION=%{version}
make %{?_smp_mflags}

%install
%qmake5_install
# Written by %post, unless the device already sets QT_QPA_GENERIC_PLUGINS
mkdir -p %{buildroot}%{_sharedstatedir}/environment/compositor
touch %{buildroot}%{_sharedstatedir}/environment/compositor/01-frogfish.conf

%post
# On every install and upgrade; never fails the installation
if /usr/libexec/frogfish/update-env; then
    echo "frogfish: restart the home screen to load it: systemctl --user restart lipstick"
fi
exit 0

%postun
if [ $1 -eq 0 ]; then
    echo "frogfish: restart the home screen to unload it: systemctl --user restart lipstick"
fi

%files
%license LICENSE
%{_libdir}/qt5/plugins/generic/libfrogfish.so
%{_libdir}/qt5/plugins/generic/liblipstickaddons-frogfish.so
%dir /usr/libexec/frogfish
%attr(755,root,root) /usr/libexec/frogfish/update-env
%ghost %attr(644,root,root) %{_sharedstatedir}/environment/compositor/01-frogfish.conf
%{_datadir}/jolla-settings/entries/frogfish.json
%{_datadir}/jolla-settings/pages/frogfish
%{_datadir}/themes/sailfish-default/silica/*/icons-monochrome/icon-m-frogfish.png
%{_datadir}/themes/sailfish-default/silica/*/icons-monochrome/icon-m-frogfish-off.png
