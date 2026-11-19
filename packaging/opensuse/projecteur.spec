%{!?projecteur_desktop:%global projecteur_desktop gnome}
%{!?projecteur_release:%global projecteur_release 1.tw}
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

Name:           projecteur-%{projecteur_desktop}
Version:        %{projecteur_version}
Release:        %{projecteur_release}%{?dist}
Summary:        Logitech Spotlight presenter controls for %{projecteur_session} Wayland
License:        MIT
URL:            https://github.com/ALsbraun/Projecteur
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.20
BuildRequires:  ninja
BuildRequires:  gettext-tools
BuildRequires:  desktop-file-utils
BuildRequires:  pkgconf-pkg-config
%if "%{projecteur_desktop}" == "gnome"
BuildRequires:  pkgconfig(gio-2.0)
BuildRequires:  nlohmann_json-devel >= 3.10
BuildRequires:  nodejs24
BuildRequires:  gjs
BuildRequires:  dbus-1-daemon
BuildRequires:  glibc-locale
Requires:       gnome-shell >= 51
Requires:       gnome-shell < 52
Requires:       gjs
Requires:       typelib-1_0-Gtk-4_0 >= 4.10
Requires:       typelib-1_0-Adw-1 >= 1.6
%else
BuildRequires:  kf6-extra-cmake-modules >= 6.7
BuildRequires:  cmake(Qt6Core) >= 6.10
BuildRequires:  cmake(Qt6Quick) >= 6.10
BuildRequires:  cmake(Qt6ShaderTools) >= 6.10
BuildRequires:  cmake(Qt6WaylandClient) >= 6.10
BuildRequires:  cmake(KF6Config) >= 6.7
BuildRequires:  cmake(KF6ConfigWidgets) >= 6.7
BuildRequires:  cmake(KF6CoreAddons) >= 6.7
BuildRequires:  cmake(KF6DBusAddons) >= 6.7
BuildRequires:  cmake(KF6GlobalAccel) >= 6.7
BuildRequires:  cmake(KF6I18n) >= 6.7
BuildRequires:  cmake(KF6Notifications) >= 6.7
BuildRequires:  cmake(KF6Package) >= 6.7
BuildRequires:  cmake(KF6WidgetsAddons) >= 6.7
BuildRequires:  cmake(KF6WindowSystem) >= 6.7
BuildRequires:  cmake(KF6XmlGui) >= 6.7
BuildRequires:  kf6-kirigami-devel >= 6.7
BuildRequires:  cmake(KPipeWire) >= 6.7
BuildRequires:  cmake(LayerShellQt) >= 6.7
BuildRequires:  cmake(Plasma) >= 6.7
Requires:       plasma6-workspace >= 6.7
Requires:       layer-shell-qt6 >= 6.7
Requires:       qt6-wayland >= 6.10
Requires:       qt6-declarative-imports
Requires:       kf6-kirigami-imports
Requires:       kpipewire6-imports
%endif
Requires:       hicolor-icon-theme
Requires:       udev
Requires(post): kmod
Provides:       projecteur = %{version}-%{release}
Conflicts:      projecteur
Conflicts:      projecteur-%{projecteur_other_desktop}

%description
Projecteur supports Logitech Spotlight presentation remotes over USB and
Bluetooth with a pointer, spotlight and magnifier on Wayland.
%if "%{projecteur_desktop}" == "gnome"
The GNOME edition uses GTK 4 preferences, a GNOME Shell extension and a
GLib/GIO device service.
%else
The KDE Plasma edition includes Qt preferences, the KWin overlay and a Plasma
panel widget.
%endif

%prep
%autosetup
rm -f cmake/modules/ArchiveExportInfo.cmake

%build
export GIT_CEILING_DIRECTORIES="%{_builddir}"
export CFLAGS="%{optflags}" CXXFLAGS="%{optflags}"
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_INSTALL_LIBDIR=%{_lib} -DCMAKE_INSTALL_LIBEXECDIR=%{_libexecdir} \
    -DCMAKE_INSTALL_UDEVRULESDIR=%{_prefix}/lib/udev/rules.d \
    -DPROJECTEUR_WITH_GNOME=%{projecteur_with_gnome} \
    -DPROJECTEUR_WITH_KDE=%{projecteur_with_kde} \
    -DPROJECTEUR_PACKAGE_VERSION=%{version}-%{release} \
    -DCOMPRESS_MAN_PAGE=OFF -DPACKAGE_TARGETS=OFF -DBUILD_TESTING=ON
cmake --build build --parallel %{_smp_build_ncpus}

%install
DESTDIR=%{buildroot} cmake --install build
install -d %{buildroot}%{_sysconfdir}/xdg/autostart
sed 's/^OnlyShowIn=.*/OnlyShowIn=%{projecteur_session};/' packaging/fedora/projecteur-autostart.desktop \
    > %{buildroot}%{_sysconfdir}/xdg/autostart/org.projecteur.Projecteur.desktop
install -Dpm 0644 packaging/fedora/projecteur-uinput.conf \
    %{buildroot}%{_prefix}/lib/modules-load.d/projecteur-uinput.conf
%find_lang projecteur

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{projecteur_launcher}.desktop
desktop-file-validate %{buildroot}%{_sysconfdir}/xdg/autostart/org.projecteur.Projecteur.desktop
ctest --test-dir build --output-on-failure

%post
%{_sbindir}/modprobe uinput >/dev/null 2>&1 || :
if %{_bindir}/udevadm control --reload-rules >/dev/null 2>&1; then
    %{_bindir}/udevadm trigger --action=change --subsystem-match=misc --sysname-match=uinput >/dev/null 2>&1 || :
    %{_bindir}/udevadm settle --timeout=10 >/dev/null 2>&1 || :
fi

%postun
%{_bindir}/udevadm control --reload-rules >/dev/null 2>&1 || :

%files -f projecteur.lang
%license LICENSE.md
%doc README.md doc/LINUX-PACKAGES.md
%{_bindir}/projecteur
%{_prefix}/lib/udev/rules.d/55-projecteur.rules
%{_prefix}/lib/modules-load.d/projecteur-uinput.conf
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
%{_datadir}/gnome-shell/extensions/projecteur@projecteur.github.io/
%{_datadir}/icons/hicolor/scalable/apps/org.projecteur.Projecteur.Preferences.svg
%else
%{_datadir}/knotifications6/projecteur.notifyrc
%{_libdir}/qt6/plugins/plasma/applets/org.projecteur.Projecteur.so
%endif

%changelog
* Thu Nov 19 2026 Projecteur contributors <stefan.braun@ergon.ch>
- Add separate Projecteur packages for GNOME and KDE Plasma on Tumbleweed.
