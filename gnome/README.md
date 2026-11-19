# GNOME Wayland renderer

The Fedora GNOME build uses the existing Projecteur device, HID++, input mapping,
settings and presentation timer code. The service runs without a display
connection. The GTK 4/libadwaita preferences run as a separate GJS application. This extension owns display effects in
the compositor, so overlays neither need an X11 window nor take presentation
focus. The system package installs the UUID directory into
`/usr/share/gnome-shell/extensions`.

## Protocol

Session bus service/interface `org.projecteur.Gnome`, object
`/org/projecteur/Gnome`:

- `Ping() -> s`: protocol version `1`.
- `SetState(b active, s settingsJson)`: apply a complete settings snapshot and
  set visibility. Settings use the camelCase `Settings` properties; colors are
  `#RRGGBB`, opacities are 0–1. Unknown fields are ignored. Invalid JSON or
  oversized messages return `org.projecteur.Gnome.InvalidSettings`.

An active client must renew state at least every two seconds. The extension hides
an abandoned overlay after five seconds (up to one extra second for the timeout
check), and immediately when the Projecteur application's bus name disappears.
It releases cursor visibility and fullscreen composition inhibitors on hiding,
disabling or locking the desktop. The extension only runs in user sessions.

GNOME currently supports the circular spotlight, solid/diffuse pointer with
optional trail, smooth live magnification, a circular border, colors, opacity,
size, magnification factor, current/all-monitor shading, and hidden/default
cursor. Other shapes and the KDE text/pixel zoom filters are not implemented.
The preferences UI limits its controls accordingly. The panel supplies mode selection, presets, preferences and the timer, plus
start/stop controls and the reported battery level for each connected device.
Start launches only the background service; preferences open through their own
menu item. Start is disabled while running, and service-dependent actions are
disabled while stopped or transitioning. The ten-second preview is available
only in preferences. No global preview shortcut is installed.

## Preferences

The preferences use the device service on the session bus. The shared
`org.projecteur.Projecteur` interface provides presets, battery and timer state
at `/org/projecteur/Projecteur/Control`. The preferences read and edit overlay
and device settings at `/org/projecteur/Projecteur/Preferences` through
`org.projecteur.Projecteur.Preferences`. The service persists existing
`projecteurrc` settings and owns device input recording. The preferences close
independently of the service.

The RPM desktop launcher starts the GTK app. If the device service is stopped,
D-Bus starts it. The panel and the existing `projecteur --show-dialog` command
can also open the same window. The GTK window and the Shell menu follow the
session language. German is provided by the shared gettext catalog, and English
is the default for other languages.

## Rendering

Cursor positions come directly from GNOME 51's cursor tracker in logical
coordinates, and updates are coalesced before repaint. The view actors live on
the stage outside `Main.uiGroup`; the magnifier clones `Main.uiGroup` so there
is no recursive overlay capture. A GNOME 51 `Clutter.ShaderEffect` masks the
clipped live clone. The source and lens stay within the current monitor at
screen edges. Fullscreen direct scanout is inhibited only while effects are
active. Actors are nonreactive and hidden from picking.

Shading uses four solid compositor rectangles plus a small Cairo surface around
the circle. The pointer and border use a bounded drawing surface. Moving the
spotlight therefore does not rasterize two complete monitor-sized images per
frame.

The API choices follow the GNOME 51 implementation and porting guide:

- https://raw.githubusercontent.com/GNOME/gnome-shell/gnome-51/js/ui/magnifier.js
- https://raw.githubusercontent.com/GNOME/mutter/gnome-51/clutter/clutter/clutter-shader-effect.h
- https://gjs.guide/extensions/upgrading/gnome-shell-51.html

## Verification

`node gnome/tests/geometry.test.mjs` checks invalid settings, geometry at monitor
edges, negative/fractional coordinates, and source-region bounds. CTest includes
this test in the GNOME build. The translation tests check German, English and
the English fallback using the compiled catalog. `node gnome/tests/panel.test.mjs` additionally checks
service transition labels and battery formatting, including unknown/error states.

`gnome/tests/shell-smoke.sh` runs on GNOME 51 with separate private session and
system buses, private XDG directories and a headless virtual display. The system
bus uses `tests/system-bus.conf` without activatable system services, preventing
failed `login1` activation in containers without systemd. It loads a test-only
helper (never installed by the RPM), renders all three modes, saves screenshots,
and checks disable/re-enable, D-Bus cleanup and JavaScript/shader errors. A private
application mock also checks repeated start/quit clicks, separate preferences
activation, live battery updates, restart state and a connection closing before
its Quit response. The menu screenshot is saved as `menu-running.png`. It does
not enable or modify extensions in the user's running desktop. Set
`PROJECTEUR_SMOKE_LANGUAGE=de` to check the German menu. That run requires a
German system locale and `msgfmt`. From Toolbx it
can be run with `flatpak-spawn --host` and the absolute script path.

Hardware movement, button combinations, screen sharing, real mixed-DPI monitor
setups and suspend/reconnect still need acceptance testing with the presenter.
