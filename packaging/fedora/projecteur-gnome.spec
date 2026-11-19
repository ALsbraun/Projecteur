%{!?projecteur_desktop:%global projecteur_desktop gnome}
%if "%{projecteur_desktop}" == "gnome"
%global projecteur_other_desktop kde
%global projecteur_with_gnome ON
%global projecteur_with_kde OFF
%global projecteur_session GNOME
%global projecteur_launcher org.projecteur.Projecteur.Preferences
%else
%global projecteur_other_desktop gnome
%global projecteur_with_gnome OFF
%global projecteur_with_kde ON
%global projecteur_session KDE
%global projecteur_launcher org.projecteur.Projecteur
%endif
%global extension_uuid projecteur@projecteur.github.io
%{!?projecteur_version:%global projecteur_version 1.0.0}
%{!?projecteur_release:%global projecteur_release 1}

Name:           projecteur-%{projecteur_desktop}
Version:        %{projecteur_version}
Release:        %{projecteur_release}%{?dist}
Summary:        Logitech Spotlight pointer, spotlight and magnifier for %{projecteur_session} Wayland
License:        MIT
URL:            https://github.com/ALsbraun/Projecteur
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.20
BuildRequires:  ninja-build
BuildRequires:  gettext
BuildRequires:  systemd-rpm-macros
BuildRequires:  pkgconfig(udev)
BuildRequires:  desktop-file-utils
%if "%{projecteur_desktop}" == "gnome"
BuildRequires:  pkgconfig(gio-2.0)
BuildRequires:  json-devel >= 3.10
BuildRequires:  nodejs
BuildRequires:  gjs
BuildRequires:  dbus-daemon
BuildRequires:  (glibc-langpack-de or glibc-all-langpacks)
BuildRequires:  (glibc-langpack-en or glibc-all-langpacks)

%else
BuildRequires:  qt6-qtbase-devel >= 6.10
BuildRequires:  extra-cmake-modules >= 6.7
BuildRequires:  qt6-qtdeclarative-devel >= 6.10
BuildRequires:  qt6-qtshadertools-devel >= 6.10
BuildRequires:  qt6-qtwayland-devel >= 6.10
BuildRequires:  kf6-kconfig-devel >= 6.7
BuildRequires:  kf6-kconfigwidgets-devel >= 6.7
BuildRequires:  kf6-kcoreaddons-devel >= 6.7
BuildRequires:  kf6-kdbusaddons-devel >= 6.7
BuildRequires:  kf6-kglobalaccel-devel >= 6.7
BuildRequires:  kf6-ki18n-devel >= 6.7
BuildRequires:  kf6-kirigami-devel >= 6.7
BuildRequires:  kf6-knotifications-devel >= 6.7
BuildRequires:  kf6-kpackage-devel >= 6.7
BuildRequires:  kf6-kwidgetsaddons-devel >= 6.7
BuildRequires:  kf6-kwindowsystem-devel >= 6.7
BuildRequires:  kf6-kxmlgui-devel >= 6.7
BuildRequires:  kpipewire-devel >= 6.7
BuildRequires:  layer-shell-qt-devel >= 6.7
BuildRequires:  libplasma-devel >= 6.7
%endif

%if "%{projecteur_desktop}" == "gnome"
Requires:       gnome-shell >= 51
Requires:       gnome-shell < 52
Requires:       gjs
Requires:       gtk4 >= 4.10
Requires:       libadwaita >= 1.6
%else
Requires:       plasma-workspace >= 6.7
Requires:       layer-shell-qt >= 6.7
Requires:       qt6-qtwayland >= 6.10
%endif
Requires:       systemd-udev
Requires:       hicolor-icon-theme
Requires(post): kmod
Provides:       projecteur = %{version}-%{release}
Conflicts:      projecteur
Conflicts:      projecteur-%{projecteur_other_desktop}

%description
Projecteur supports Logitech Spotlight presentation remotes over USB and
Bluetooth.
%if "%{projecteur_desktop}" == "gnome"
The GNOME edition includes a GNOME Shell extension providing a
compositor-native pointer, spotlight and live magnifier on Wayland, plus
a GTK 4 and libadwaita configuration application for the presenter and overlay.
Its device service uses GLib and GIO.

Enable the bundled extension after logging out and in:
gnome-extensions enable %{extension_uuid}
%else
The KDE Plasma edition includes the Qt settings dialog, the Wayland overlay
and the Projecteur panel widget.
%endif

%prep
%autosetup
# This source snapshot includes local GNOME changes. Do not report the commit
# identity embedded in the original upstream archive as this RPM's revision.
rm -f cmake/modules/ArchiveExportInfo.cmake

%build
export GIT_CEILING_DIRECTORIES="%{_builddir}"
%cmake -G Ninja \
    -DPROJECTEUR_WITH_KDE=%{projecteur_with_kde} \
    -DPROJECTEUR_WITH_GNOME=%{projecteur_with_gnome} \
    -DCMAKE_INSTALL_LIBEXECDIR=%{_libexecdir} \
    -DPROJECTEUR_PACKAGE_VERSION=%{version}-%{release} \
    -DPACKAGE_TARGETS=OFF \
    -DCMAKE_INSTALL_UDEVRULESDIR=%{_udevrulesdir} \
    -DCOMPRESS_MAN_PAGE=OFF \
    -DBUILD_TESTING=ON
%cmake_build

%install
%cmake_install
install -d %{buildroot}%{_sysconfdir}/xdg/autostart
sed 's/^OnlyShowIn=.*/OnlyShowIn=%{projecteur_session};/' packaging/fedora/projecteur-autostart.desktop \
    > %{buildroot}%{_sysconfdir}/xdg/autostart/org.projecteur.Projecteur.desktop
install -Dpm 0644 packaging/fedora/projecteur-uinput.conf \
    %{buildroot}%{_modulesloaddir}/projecteur-uinput.conf
%find_lang projecteur

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{projecteur_launcher}.desktop
desktop-file-validate %{buildroot}%{_sysconfdir}/xdg/autostart/org.projecteur.Projecteur.desktop
%ctest

%post
/usr/sbin/modprobe uinput >/dev/null 2>&1 || :
# The static uinput node may predate the package and have no active-seat ACL.
# Reload this rule, then re-evaluate only uinput, not every input device.
/usr/bin/udevadm control --reload-rules >/dev/null 2>&1 || :
/usr/bin/udevadm trigger --action=change --subsystem-match=misc --sysname-match=uinput >/dev/null 2>&1 || :
/usr/bin/udevadm settle --timeout=10 >/dev/null 2>&1 || :

%postun
%udev_rules_update

%files -f projecteur.lang
%license LICENSE.md
%doc README.md doc/FEDORA-GNOME.md doc/HIDPP-POINTER-NOTES.txt
%{_bindir}/projecteur
%{_udevrulesdir}/55-projecteur.rules
%{_modulesloaddir}/projecteur-uinput.conf
%config(noreplace) %{_sysconfdir}/xdg/autostart/org.projecteur.Projecteur.desktop
%{_datadir}/applications/%{projecteur_launcher}.desktop
%{_datadir}/bash-completion/completions/projecteur
%{_datadir}/dbus-1/services/org.projecteur.Projecteur.service
%{_datadir}/dbus-1/interfaces/org.projecteur.Projecteur.xml
%{_datadir}/icons/hicolor/*/apps/projecteur.svg
%{_datadir}/metainfo/org.projecteur.Projecteur.metainfo.xml
%{_mandir}/man1/projecteur.1*
%if "%{projecteur_desktop}" == "gnome"
%{_libexecdir}/projecteur/
%{_datadir}/gnome-shell/extensions/%{extension_uuid}/
%{_datadir}/icons/hicolor/scalable/apps/org.projecteur.Projecteur.Preferences.svg
%else
%{_datadir}/knotifications6/projecteur.notifyrc
%{_qt6_plugindir}/plasma/applets/org.projecteur.Projecteur.so
%endif

%changelog
* Thu Nov 19 2026 Projecteur contributors - 1.0.0-1
- Initial release from the Projecteur repository for Fedora 45.
- Ship separate RPMs for GNOME 51 and KDE Plasma on Wayland.
- Use a Qt-free GLib/GIO service, GTK 4 preferences and a Shell extension on GNOME.
- Keep the Qt device service and the native overlay and widget on KDE Plasma.
- Preserve existing presets and presenter button mappings across editions.
