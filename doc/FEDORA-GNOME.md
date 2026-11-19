# Projecteur on Fedora 45 / GNOME 51 / Wayland

This edition combines the existing Logitech device support with a GNOME Shell
extension. The extension draws the spotlight, pointer and magnifier directly
in the GNOME compositor. The original KDE overlay relies on KWin protocols.

## Install the RPM

Packages are available on [GitHub Releases](https://github.com/ALsbraun/Projecteur/releases).
Stable releases use `v…` tags. Automated development builds are marked as
pre-releases.

The RPM contains the application, extension, device access rules and a GNOME
autostart entry. Run the installation on the Fedora desktop outside Toolbox.

```sh
sudo dnf install ./dist/projecteur-gnome-1.1.0-1.fc45.x86_64.rpm
```

**Log out and back in** so GNOME discovers the system-installed extension.
Enable it once.

```sh
gnome-extensions enable projecteur@projecteur.github.io
```

Replug the USB receiver or reconnect the Bluetooth presenter to apply the
device access rules. Projecteur starts automatically when you sign in to GNOME.
Open **Projecteur** from the application menu for the GTK4/libadwaita
preferences, which include **Overlay**, **Devices** and **Timer**. The
preferences and the GNOME panel menu follow your session language. They use
German in a German session and English by default. The service saves changes
immediately and continues running when you close the window. Run the
application as your regular user.

If you have another edition of Projecteur installed, remove it before
installing this RPM. The editions use the same executable and can have
conflicting desktop integration files. Your preferences are retained when
switching packages.

## Test with the Logitech Spotlight

Hardware testing on 1.0.1 confirmed normal use, including mode selection in
the GNOME panel and a reported battery level of 100%, with one outstanding issue
in the top-button double-click. The captured button reports led to a fix in
1.0.2 for the HID++ setup response handling. That fix still needs confirmation
on the physical remote. Use the same steps to check other device setups.

1. Confirm that your presenter appears in the Projecteur panel menu or under
   **Devices** in preferences.
2. With the presenter disconnected, choose **Start Preview** under **Overlay**
   and try Spotlight, Pointer and Magnifier. All three effects should follow
   the cursor. The magnifier should update as the content underneath moves.
3. Open a presentation in LibreOffice Impress or in a browser. Tap Next and Back
   once each. Each tap should move exactly one slide.
4. Hold the top button and move the presenter. The selected effect should stay
   visible. Hold the presenter still for a few seconds, then release the
   button. The effect should end reliably.
5. Switch between Spotlight, Pointer and Magnifier. Adjust the size, color and
   magnification factor and check each result. Double-click the top button to
   verify the 1.0.2 fix. The mode should change once.
6. On a multimonitor setup, move across monitor boundaries and edges, including
   monitors with different scale factors. Test fullscreen presentations and
   screen sharing separately.
7. Disconnect and reconnect the presenter, then lock and unlock the session.
   Confirm that the overlay clears and the presenter buttons still work.
8. If you use both USB and Bluetooth, test each connection separately.

The GNOME panel menu offers the three modes and displays the presenter's battery
level. **Start Preview** shows the selected effect for ten seconds. Spotlight
and Magnifier are circular, and the magnifier scales live content smoothly.
The extra KDE shapes and text filter are specific to the Plasma renderer.

Firmware that reports and confirms the relevant HID++ controls lets Projecteur
detect holds and releases directly. A mode-change report cycles through
Spotlight, Magnifier and Pointer. On firmware without confirmed controls, the
overlay still turns off 600 ms after the last movement. Pay particular
attention to holding the remote still and double-clicking the top button.
The full checklist is in [HID++ pointer notes](HIDPP-POINTER-NOTES.txt).

Hardware testing on a visible GNOME desktop is needed to verify visual quality,
firmware-specific button reports, screen sharing and multimonitor behavior.

## Diagnose problems

```sh
projecteur --fullversion
projecteur --device-scan
gnome-extensions info projecteur@projecteur.github.io
gdbus call --session --dest org.projecteur.Gnome \
  --object-path /org/projecteur/Gnome \
  --method org.projecteur.Gnome.Ping
journalctl --user -b --no-pager | grep -Ei 'projecteur|gnome-shell' | tail -100
```

`Ping` returns protocol version `1` when the extension is active. If the D-Bus
name is missing, check whether the extension is enabled. A detected device
that Projecteur cannot read may indicate that its access rules have yet to
take effect. Use the packaged device access rules for `/dev/input` and
`/dev/hidraw*`.

## Build an RPM

On Fedora 45 or in a Fedora 45 Toolbox, run the local build script.

```sh
./packaging/fedora/build-rpm.sh --desktop gnome --install-deps
```

The optional `--install-deps` flag uses `sudo dnf` to install the build
dependencies listed in the RPM specification. If those dependencies are
already installed, run the script without the flag.

```sh
./packaging/fedora/build-rpm.sh --desktop gnome
```

Binary and source RPMs and their checksums go to `dist/`. Build files go to
`build-rpm/`. Install the newly built package on the Fedora desktop for each
test. Set `PROJECTEUR_BUILD_JOBS=2` to reduce peak memory use. Set
`SOURCE_DATE_EPOCH` to use a reproducible release timestamp. For a standalone
CMake build, configure with `-DPROJECTEUR_WITH_KDE=OFF`.

The package targets GNOME 51. New major releases need testing because the
renderer uses GNOME Shell compositor APIs.

## Uninstall

```sh
gnome-extensions disable projecteur@projecteur.github.io
sudo dnf remove projecteur-gnome
```

Log out to end any Projecteur service still running in your session.
