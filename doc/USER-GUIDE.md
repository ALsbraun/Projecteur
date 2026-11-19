# Projecteur GNOME user guide

This guide covers the Fedora 45 / GNOME 51 / Wayland edition. Start with the
[README](../README.md) and [installation instructions](FEDORA-GNOME.md).
The interface follows the session language and uses English by default.

## During a presentation

Use **Start Projecteur** in the GNOME panel menu to start the background
application. While it runs, this entry becomes **Projecteur Is Running** and cannot
be started again. **Preferences…** opens the preferences; **Quit** stops
the application and becomes unavailable after shutdown. The extension remains
in the panel so you can start Projecteur again.

The menu lists connected presenters and their reported battery percentage and
charging state. Unknown battery data is labeled accordingly. It also provides
Spotlight, Pointer and Magnifier mode selection, saved presets and the presentation
timer. Testing controls are in preferences rather than in the panel menu.

On a Logitech Spotlight, hold the top button to show the selected effect and
release it to hide it. Double-click that button to cycle the effect. The device
must support and report the corresponding HID++ controls; see the
[protocol notes](HIDPP-POINTER-NOTES.txt). Next and Back advance presentation
slides. Check receiver and Bluetooth connections separately.

## Appearance and presets

In **Preferences → Overlay**, adjust spotlight size and shading, pointer color,
solid/diffuse appearance and trail, border, magnification factor and multi-screen
shading. GNOME uses a round spotlight/lens, smooth live magnification, and a
hidden or normal system cursor. Additional Plasma shapes and zoom filters are
not exposed in this edition.

Use **Start Preview** for a ten-second preview without presenter hardware.
Save different appearances as presets and load them from preferences or the panel.
Changes are saved immediately to `~/.config/projecteurrc`. To reuse a particular
appearance later, save it as a named preset.

## Presenter button mapping

In **Preferences → Devices → Button Mappings**, choose **Add…**.
Record a presenter button sequence or select an available hold-and-move gesture,
then choose an action and save. Button actions include keyboard shortcuts,
preset cycling and toggling the overlay. Hold-and-move gestures support
scrolling and volume control.

Mapping uses a virtual input device to forward unmapped events. If input access
is unavailable, the Devices page explains the problem. See
[troubleshooting](TROUBLESHOOTING.md#input-mapping-is-grey-or-buttons-cannot-be-remapped).
`--disable-uinput` intentionally disables remapping. Avoid competing tools that
grab the same presenter.

## Timer

Set the duration under **Preferences → Timer** and start the timer there or
from the GNOME panel. For compatible presenters, set the vibration strength
under **Devices → Timer Feedback**. Completion notifications use the desktop
notification service.

## Command-line control

```sh
projecteur --background
projecteur --show-dialog
projecteur --command spot=toggle
projecteur --command settings=show
projecteur --command preset="Demo"
```

`--background` never opens preferences when another instance is already running.
Control commands target an existing application. Use `projecteur --help-all`
or `man projecteur` for the complete reference.
