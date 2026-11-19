# Projecteur packages

[GitHub Releases](https://github.com/ALsbraun/Projecteur/releases) lists
separate binary packages for GNOME 51 and KDE Plasma on Wayland.

- [Fedora 45 RPMs](FEDORA-GNOME.md) install with `dnf`.
- [Ubuntu and Kubuntu 26.10 DEBs](LINUX-PACKAGES.md) install with `apt`.
- [openSUSE Tumbleweed RPMs](LINUX-PACKAGES.md) install with `zypper`.

The GNOME editions bundle the GNOME Shell extension, device access rules and
autostart entry. The KDE Plasma editions bundle the Qt application and panel
widget. Install the edition for your desktop. The debug packages are optional.

Main-branch and scheduled builds publish development prereleases. Version tags
publish stable releases after validation. Release assets are standalone
downloads. See the [maintenance guide](MAINTENANCE.md) for release procedures.

Packages named Projecteur from other distributions and the retained Arch/AUR
recipes may belong to the [upstream Plasma edition](https://github.com/gbin/Projecteur).
Check the platform and desktop listed for each package.
