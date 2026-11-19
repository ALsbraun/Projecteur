# Packages for Ubuntu and openSUSE

Projecteur has separate GNOME and KDE Plasma packages. Choose the edition for
the Wayland session you use. Both editions use the same configuration file and
install the same device service executable, so install one edition at a time.

| Distribution | GNOME package | KDE Plasma package |
| --- | --- | --- |
| Ubuntu and Kubuntu 26.10 | `projecteur-gnome` (.deb) | `projecteur-kde` (.deb) |
| openSUSE Tumbleweed with GNOME 51 or Plasma 6.7+ | `projecteur-gnome` (.tw RPM) | `projecteur-kde` (.tw RPM) |

Install the downloaded package with your distribution's package manager. For
example, from the corresponding build output directory:

```sh
sudo apt install ./projecteur-gnome_1.1.0-1~ubuntu26.10_amd64.deb
sudo zypper install ./projecteur-gnome-1.1.0-1.tw.x86_64.rpm
```

On GNOME, log out and back in, then enable the Shell extension:

```sh
gnome-extensions enable projecteur@projecteur.github.io
```

On Plasma, start Projecteur from the application menu or add the Projecteur
widget to a panel. Reconnect the presenter after installing either edition.

## Build packages

Run the matching command on Ubuntu 26.10 or openSUSE Tumbleweed, including in a
container of that distribution:

```sh
./packaging/ubuntu/build-deb.sh --install-deps
./packaging/opensuse/build-rpm.sh --install-deps
```

Both commands build the two editions by default. Use `--desktop gnome` or
`--desktop kde` for one edition. Omit `--install-deps` when the build
dependencies are already present. Outputs go into `dist/ubuntu26.10/` and
`dist/opensuse/`, respectively.

The GNOME packages use GLib/GIO and require GNOME Shell 51. The Plasma
packages use Qt 6.10 or newer and require Plasma 6.7 or newer at build time.
Build and install packages on the intended distribution so the desktop
plugins use the distribution's Qt and Plasma libraries.
