# Contributing to Projecteur

The packaged targets are Fedora 45, Ubuntu/Kubuntu 26.10 and openSUSE
Tumbleweed on Wayland with GNOME Shell 51 or KDE Plasma.
Please keep changes focused and describe their effect on a presentation or
presenter device. For larger work, discuss the approach in an
[issue](https://github.com/ALsbraun/Projecteur/issues) first.

## Build requirements

Build dependencies are listed in the package definitions for
[Fedora](packaging/fedora/projecteur-gnome.spec),
[Ubuntu](packaging/ubuntu/debian/control) and
[openSUSE](packaging/opensuse/projecteur.spec).
Both editions use C++17 and CMake. GNOME builds with GLib, GIO and
nlohmann/json. Its preferences use GJS, GTK 4.10 or later and libadwaita 1.6
or later at runtime. The Plasma edition builds with Qt 6.10 or later, KDE
Frameworks, Plasma, KPipeWire and LayerShellQt.

On Fedora 45 or in a Fedora 45 Toolbox:

```sh
git clone https://github.com/ALsbraun/Projecteur.git
cd Projecteur
./packaging/fedora/build-rpm.sh --install-deps
```

This creates binary and source RPMs under `dist/`; `build-rpm/` holds temporary
build files. Subsequent runs can omit `--install-deps`. Set
`PROJECTEUR_BUILD_JOBS=2` on machines with less memory. Use `--desktop gnome` or
`--desktop kde` to select one edition. Use the RPM on the target desktop for
physical-device tests. See the [Ubuntu and openSUSE guide](doc/LINUX-PACKAGES.md)
for their build commands.

For faster implementation iterations after installing dependencies:

```sh
cmake -S . -B build/gnome -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DPROJECTEUR_WITH_KDE=OFF \
  -DPACKAGE_TARGETS=OFF \
  -DBUILD_TESTING=ON
cmake --build build/gnome --parallel 2
ctest --test-dir build/gnome --output-on-failure
```

The GNOME extension and udev rules must be installed for the complete desktop
workflow. Do not run the device application as Root. For a Plasma-only build, configure with
`-DPROJECTEUR_WITH_KDE=ON -DPROJECTEUR_WITH_GNOME=OFF`. All desktop packages run the common device tests. Fedora CI also runs the
GNOME 51 compositor smoke test.

## Validation

The RPM build runs the input/HID++, geometry and private D-Bus tests. Run the
real compositor smoke test on a GNOME 51 system as a normal user:

```sh
./gnome/tests/shell-smoke.sh
```

It starts a headless compositor with its own D-Bus session and configuration.
It does not enable extensions in your running desktop. CI also runs this test
and retains screenshots and logs. Device behavior still needs the
[hardware acceptance procedure](doc/FEDORA-GNOME.md).

Before submitting a change:

```sh
shellcheck packaging/fedora/build-rpm.sh .github/scripts/*.sh
git diff --check
./packaging/fedora/build-rpm.sh
```

Use `actionlint .github/workflows/*.yml` when changing GitHub workflows. Report
Fedora/GNOME versions, presenter model, USB/Bluetooth connection and the checks
performed. Include screenshots for visible changes. Do not claim hardware
coverage from synthetic input or headless tests alone.

## Source and artifact handling

Build output, RPMs, local editor state and signing keys are excluded by
`.gitignore`. Review `git status` and staged files before committing. An ignore
rule does not remove files that were already tracked. Binary packages belong
in release assets or CI artifacts, not in the Git history.

The RPM script archives tracked files plus non-ignored untracked source edits,
so it also works before the first commit. It excludes the Git database and local
build output. Set `SOURCE_DATE_EPOCH` when an explicit release timestamp is
needed; otherwise a Git commit timestamp is used where available.

## Project map

| Path | Purpose |
| --- | --- |
| `src/` | Shared Linux device handling and the Plasma application |
| `src/native/` | GLib/GIO device service and D-Bus objects for GNOME |
| `gnome/` | GTK preferences, GNOME Shell renderer and real-compositor smoke test |
| `tests/` | Input, geometry registration and application/bridge regression checks |
| `packaging/fedora/` | Native Fedora RPM and repeatable package build script |
| `.github/` | Build, release and Dependabot automation |
| `renovate.json` | Fedora build-image update policy |
| `qml/`, `plasma/`, `protocols/` | Retained upstream Plasma backend |
| `doc/` | User, testing and maintenance documentation |

## Device changes

Additional static device definitions belong in `devices.conf`:

```text
0x0abc, 0x1234, usb, Example Presenter
```

CMake generates the corresponding definitions and access rules. Device-specific
protocol changes must also validate capabilities, malformed reports, disconnects
and failure recovery. `--additional-device VENDOR:PRODUCT` is available for
local experiments. Preserve native event forwarding when grabbing a device
fails or virtual input cannot be created.

## Releases and bots

See [Maintenance](doc/MAINTENANCE.md) for the stable/snapshot version scheme,
first-push repository settings and the division between Dependabot and Renovate.
Do not add another updater for the same dependencies. Major Fedora or GNOME
changes need explicit compatibility work and validation.

Contributions are provided under the existing [MIT License](LICENSE.md).
Preserve upstream attribution and distinguish this fork's behavior from the
original Plasma implementation.
