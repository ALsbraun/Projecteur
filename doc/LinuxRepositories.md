# Projecteur GNOME packages

The supported target for this fork is **Fedora 45, GNOME 51, Wayland, x86-64**.
Download RPMs from [this repository's Releases](https://github.com/ALsbraun/Projecteur/releases)
once its GitHub workflows have run. Each release contains the application RPM,
source RPM, debug packages and SHA-256 checksums.

The application RPM bundles the GNOME Shell extension, device access rules and
autostart entry. Install that one RPM on the desktop with `sudo dnf install`
and its downloaded filename. Debug packages are optional. See
[installation and hardware tests](FEDORA-GNOME.md).

Main-branch and scheduled builds publish development prereleases; version tags
publish stable releases after validation. See the repository maintenance guide
linked from [CONTRIBUTING](../CONTRIBUTING.md) for bot setup and release rules.
Release assets are standalone downloads, not a DNF repository. Installing one
does not configure automatic desktop package updates.

Packages called Projecteur from other distributions and the retained Arch/AUR
recipes may belong to the [upstream Plasma edition](https://github.com/gbin/Projecteur).
They do not establish GNOME support or compatibility with this fork. Historical
upstream release and issue references are retained for attribution.
