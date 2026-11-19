# Troubleshooting Projecteur GNOME

For Fedora 45 / GNOME 51 / Wayland, start with the
[installation guide](FEDORA-GNOME.md). Report problems to the
[Projecteur issue tracker](https://github.com/ALsbraun/Projecteur/issues).
Include the presenter model, USB or Bluetooth connection, GNOME version, and:

```sh
projecteur --fullversion
projecteur --device-scan
gnome-extensions info projecteur@projecteur.github.io
```

## The menu or effects are missing

After installing or updating the RPM, log out and back in so GNOME loads the
installed extension version. Enable it with:

```sh
gnome-extensions enable projecteur@projecteur.github.io
```

Use **Start Projecteur** in the menu to start the background service. Once the
service starts, the menu shows **Projecteur Is Running**. Use **Preferences…**
to open the settings or **Quit** to stop the service. The GNOME extension stays
enabled after you quit the service.

Check the rendering connection:

```sh
gdbus call --session --dest org.projecteur.Gnome \
  --object-path /org/projecteur/Gnome --method org.projecteur.Gnome.Ping
```

The result must be `('1',)`. The preferences window also reports when the GNOME
overlay is unavailable. The extension is intentionally inactive on the lock
screen and in the overview. Test effects from **Overlay → Start Preview**.

## Input Mapping is grey or buttons cannot be remapped

Mapping requires access to both the presenter's input nodes and `/dev/uinput`.
The application must run as your ordinary desktop user and without
`--disable-uinput`. Check the virtual input device:

```sh
test -w /dev/uinput && echo writable || echo not-writable
udevadm info --query=property --name=/dev/uinput
```

An existing `/dev/uinput` node can predate the package's access rules. New RPMs
reload the rules and retrigger that node specifically. For an older installation:

```sh
sudo modprobe uinput
sudo udevadm control --reload-rules
sudo udevadm trigger --action=change --subsystem-match=misc --sysname-match=uinput
sudo udevadm settle
```

Reconnect the presenter after installing its rules. Do not grant world access
to input devices or run the application as root. New versions retry virtual
input initialization when access becomes available and explain unavailable
mapping in the Devices page. Another application grabbing the same presenter
can also prevent mapping; close that application before retrying.

## The top-button double-click does not change mode

The firmware's HID++ switch-highlight report selects the next mode. The application
accepts both echoed configuration replies and normalized reporting-state replies.
The first GNOME test RPM rejected the latter even though the device had accepted
the configuration. Update the RPM, restart Projecteur and reconnect/wake the
presenter. Test double-click separately from holding the button still.

See [protocol and hardware notes](HIDPP-POINTER-NOTES.txt). Firmware support and
transport matter; a motion-timeout fallback is not equivalent to confirmed
press/release reporting.

## Battery or magnifier questions

Battery values come from the device and can be unavailable until it wakes up.
Unknown values are shown as unknown, not as an empty battery. GNOME supports a
round live magnifier with smooth scaling. Test screen sharing with a whole-screen
capture; an individual application-window capture does not necessarily include
Shell effects. Real mixed-scale monitors and presentation software still need
their own acceptance test.

## Logs and clean settings

```sh
journalctl --user -b --no-pager | grep -i projecteur
```

Settings are stored in `~/.config/projecteurrc`. Stop Projecteur before starting
an isolated configuration, so the existing process does not handle the request:

```sh
projecteur --cfg /tmp/projecteur-clean-test.rc --show-dialog
```

The retained optional Plasma backend has different compositor requirements.
Do not apply its KWin or Plasma tray instructions to the GNOME build.
