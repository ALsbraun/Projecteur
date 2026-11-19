# Projecteur

**Logitech Spotlight on Wayland, with editions for GNOME 51 and KDE Plasma.**

[![Fedora 45 RPM](https://github.com/ALsbraun/Projecteur/actions/workflows/fedora-rpm.yml/badge.svg?branch=main)](https://github.com/ALsbraun/Projecteur/actions/workflows/fedora-rpm.yml)
[![MIT license](https://img.shields.io/badge/license-MIT-2ea44f)](LICENSE.md)

Both editions use the same Logitech device support. The GNOME edition draws
effects in the GNOME Shell compositor and has GTK 4/libadwaita preferences.
The KDE Plasma edition includes a Qt preferences dialog, a KWin Wayland overlay
and a Plasma panel widget.

## Features

- Circular spotlight with adjustable size, shade, color and border.
- Colored pointer, solid or diffuse, with an optional trail.
- Live circular magnification with smooth scaling.
- Next/back buttons, configurable button actions, presets and presentation timers.
- USB receiver and Bluetooth support for Logitech Spotlight. Logitech Spotlight 2
  and the original project's other device definitions remain available.
- GNOME panel mode selector and connected-presenter battery status.
- A ten-second effect preview from the Overlay settings.
- Monitor-aware positioning and optional shading of all monitors.
- Automatic cleanup on extension disable, application disconnect and expired
  state updates.

The GNOME device service uses GLib and GIO. The GNOME packages have no Qt or KDE
Frameworks dependencies. Both editions read and write the existing
`projecteurrc` file, so presets and button mappings carry over between desktops.
On GNOME, desktop notifications go directly through the session notification
service.

## Install Projecteur

| Distribution | Packages |
| --- | --- |
| Fedora 45 | [Fedora RPMs](#install-on-fedora) |
| Ubuntu and Kubuntu 26.10 | [Ubuntu DEBs](doc/LINUX-PACKAGES.md) |
| openSUSE Tumbleweed | [Tumbleweed RPMs](doc/LINUX-PACKAGES.md) |

## Install on Fedora

Choose the binary RPM for your desktop from
[Releases](https://github.com/ALsbraun/Projecteur/releases). The packages
share the executable and configuration file. Install one edition at a time.
Packages marked **Pre-release** are automated development snapshots.

This repository starts at version 1.0.0. If you installed a higher-version RPM
from the previous repository, run `sudo dnf downgrade ./PACKAGE.rpm` with
the downloaded RPM for your desktop to switch to this release.

| Desktop | Package |
| --- | --- |
| GNOME 51 | `projecteur-gnome` |
| KDE Plasma 6 on Wayland | `projecteur-kde` |

### GNOME

For the current local release build:

```sh
sudo dnf install ./projecteur-gnome-1.1.0-1.fc45.x86_64.rpm
```

Log out and back in, then enable the extension:

```sh
gnome-extensions enable projecteur@projecteur.github.io
```

Reconnect the USB receiver or Bluetooth presenter. Projecteur starts automatically
at GNOME login. Select **Spotlight**, **Pointer** or **Magnifier** in the panel
menu. Open **Projecteur** from the application menu to configure settings and
buttons.

The UUID is deliberately kept stable across updates. No Root access is needed
when using the application. These project-built RPMs are unsigned; install them
as local files with DNF and check the supplied `SHA256SUMS` when transferring them.

See the **[installation and hardware test guide](doc/FEDORA-GNOME.md)**
for detailed setup, diagnosis and removal.

### KDE Plasma

```sh
sudo dnf install ./projecteur-kde-1.1.0-1.fc45.x86_64.rpm
```

On a Plasma Wayland session, sign out and back in for autostart or open
**Projecteur** from the application menu. Add **Projecteur** through Plasma's
**Add Widgets** to get panel controls. Reconnect the presenter after
installation so the device access rules take effect.

## Build Fedora RPMs

```sh
git clone https://github.com/ALsbraun/Projecteur.git
cd Projecteur
./packaging/fedora/build-rpm.sh --install-deps
```

Use Fedora 45 or a Fedora 45 Toolbox. The optional `--install-deps` installs
build dependencies. By default the script builds both editions. Use
`--desktop gnome` or `--desktop kde` to build just one. Binary RPMs, source RPMs
and checksums go to `dist/`. Install the chosen RPM on the desktop for hardware
testing.

See the [Ubuntu and openSUSE packaging guide](doc/LINUX-PACKAGES.md) for
their build commands and install instructions.

## Verification and current limits

CI builds both editions on Fedora 45, Ubuntu 26.10 and openSUSE Tumbleweed.
The package builds test input and HID++ handling. GNOME builds also test
monitor geometry, preferences and D-Bus calls. A real headless GNOME 51
compositor on Fedora renders all three effects and checks cleanup.

The user's device test of 1.0.1 confirmed normal operation except for the upper
button's double-click mode switch. The device reports were captured; 1.0.2
corrects the HID++ setup response handling. Acceptance of this fix on the
physical presenter is still pending. Mixed-DPI monitors, suspend/reconnect and
screen sharing require their own device and desktop tests. Hold/release and
hardware mode switching use HID++ only after the device advertises and confirms
the controls. Other firmware uses the existing motion timeout. See the
[protocol and hardware acceptance notes](doc/HIDPP-POINTER-NOTES.txt).

GNOME supports a circular lens with smooth scaling. The inherited Plasma
backend's extra shapes and text/pixel zoom filters are not part of the GNOME
renderer. Full parity with all Logitech Windows software features is not claimed.

## Automatic maintenance

Pull requests and main-branch updates build and test packages for all three
distributions. Successful main updates and weekly builds publish versioned
prerelease packages. A matching `vX.Y.Z` tag publishes a stable release once the
same checks pass.

Dependabot owns GitHub Actions updates; Renovate owns the pinned Fedora 45
container image. Eligible action patch/digest and Fedora image digest updates
can merge automatically after the required checks. Major versions remain a
review decision. The repository settings and Renovate installation needed to
activate this are documented in [Maintenance](doc/MAINTENANCE.md).

## Documentation and support

- [Fedora GNOME setup and hardware test](doc/FEDORA-GNOME.md)
- [User guide](doc/USER-GUIDE.md) and [troubleshooting](doc/TROUBLESHOOTING.md)
- [Build and contribution guide](CONTRIBUTING.md)
- [CI, release and dependency maintenance](doc/MAINTENANCE.md)
- [GNOME renderer and protocol](gnome/README.md)
- [Changelog](doc/CHANGELOG.md)
- [Report an issue](https://github.com/ALsbraun/Projecteur/issues)

Include `projecteur --fullversion`, `projecteur --device-scan`, the connection
method and GNOME version when reporting a problem.

## Origin and license

This is a fork of [Projecteur](https://github.com/gbin/Projecteur), originally
created by Jahn Fuchs and maintained upstream by Guillaume Binet. Their device
support, application and Plasma implementation form the foundation of this work.
The original attribution and [MIT license](LICENSE.md) are preserved. Historical
upstream links and authorship comments are intentionally retained where they
identify that work; issues for this Fedora/GNOME fork belong in this repository.
