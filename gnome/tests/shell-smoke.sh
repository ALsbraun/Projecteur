#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Run on GNOME 51. Uses private system/session buses and private XDG directories;
# never enables the extension or changes settings in the user's desktop session.
set -euo pipefail
source_dir=$(cd "$(dirname "$0")/../projecteur@projecteur.github.io" && pwd)
export LANGUAGE="${PROJECTEUR_SMOKE_LANGUAGE:-en}"
if [[ "$LANGUAGE" == de ]]; then
    export LC_ALL=de_DE.UTF-8
else
    export LC_ALL=C.UTF-8
fi
if [[ ${1:-} == --system-bus ]]; then
    export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
    exec dbus-run-session -- "$0" --inside
fi
if [[ ${1:-} != --inside ]]; then
    test_dir=$(mktemp -d /tmp/projecteur-shell-test.XXXXXX)
    mkdir -p "$test_dir"/{data/gnome-shell/extensions,config,cache,runtime}
    chmod 700 "$test_dir/runtime"
    cp -a "$source_dir" "$test_dir/data/gnome-shell/extensions/"
    if [[ "$LANGUAGE" == de ]]; then
        catalog_dir="$test_dir/data/gnome-shell/extensions/$(basename "$source_dir")/locale/de/LC_MESSAGES"
        mkdir -p "$catalog_dir"
        msgfmt --check -o "$catalog_dir/projecteur.mo" "$(dirname "$0")/../../po/de/projecteur.po"
    fi
    cp -a "$(dirname "$0")/smoke-driver" "$test_dir/data/gnome-shell/extensions/projecteur-test-driver@local"
    export XDG_DATA_HOME="$test_dir/data" XDG_CONFIG_HOME="$test_dir/config"
    export XDG_CACHE_HOME="$test_dir/cache" XDG_RUNTIME_DIR="$test_dir/runtime"
    export GSETTINGS_BACKEND=keyfile LIBGL_ALWAYS_SOFTWARE=1
    export PROJECTEUR_SMOKE_DIR="$test_dir"
    unset WAYLAND_DISPLAY DISPLAY
    printf 'Isolated GNOME test artifacts: %s\n' "$test_dir"
    # A container's system bus can advertise login1 without a working systemd.
    # Use an empty private bus so GNOME can take its no-logind fallback instead
    # of trying (and failing) to activate host services.
    exec timeout 75s dbus-run-session --config-file="$(dirname "$0")/system-bus.conf" \
        -- "$0" --system-bus
fi

gsettings set org.gnome.shell enabled-extensions "['projecteur@projecteur.github.io', 'projecteur-test-driver@local']"
gsettings set org.gnome.shell disable-user-extensions false
gnome-shell --headless --wayland --no-x11 --virtual-monitor=1280x720 \
    --wayland-display=projecteur-smoke >"$PROJECTEUR_SMOKE_DIR/shell.log" 2>&1 &
shell_pid=$!
trap 'kill "$shell_pid" 2>/dev/null || true; wait "$shell_pid" 2>/dev/null || true' EXIT
for _attempt in $(seq 1 80); do
    if gdbus call --session --dest org.projecteur.Gnome --object-path /org/projecteur/Gnome \
        --method org.projecteur.Gnome.Ping >"$PROJECTEUR_SMOKE_DIR/ping.txt" 2>/dev/null; then
        break
    fi
    if ! kill -0 "$shell_pid" 2>/dev/null; then
        cat "$PROJECTEUR_SMOKE_DIR/shell.log"
        exit 1
    fi
    sleep 0.25
done
cat "$PROJECTEUR_SMOKE_DIR/ping.txt"
sleep 2
gdbus call --session --dest org.projecteur.Smoke --object-path /org/projecteur/Smoke --method org.projecteur.Smoke.Prepare
sleep 1
index=0
for settings in \
    '{"showSpotShade":true,"spotSize":30}' \
    '{"showSpotShade":false,"showCenterDot":true,"dotSize":16,"dotMode":"diffuse","dotTrailEnabled":true}' \
    '{"zoomEnabled":true,"zoomFactor":2,"showSpotShade":true,"showBorder":true}'; do
    gdbus call --session --dest org.projecteur.Gnome --object-path /org/projecteur/Gnome \
        --method org.projecteur.Gnome.SetState true "$settings"
    sleep 1
    index=$((index + 1))
    state=$(gdbus call --session --dest org.projecteur.Smoke --object-path /org/projecteur/Smoke --method org.projecteur.Smoke.State)
    printf '%s\n' "$state"
    [[ "$state" == *'"active":true'* && "$state" == *'"visible":1'* ]]
    if [[ $index == 3 ]]; then [[ "$state" == *'"lens":true'* ]]; fi
    gdbus call --session --dest org.projecteur.Smoke --object-path /org/projecteur/Smoke --method org.projecteur.Smoke.Snapshot "$PROJECTEUR_SMOKE_DIR/mode-$index.png"
    gdbus call --session --dest org.projecteur.Gnome --object-path /org/projecteur/Gnome \
        --method org.projecteur.Gnome.SetState false '{}'
done
# Exercise menu state against a private mock with a separate D-Bus connection.
smoke_call() {
    gdbus call --session --dest org.projecteur.Smoke --object-path /org/projecteur/Smoke \
        --method "org.projecteur.Smoke.$1" "${@:2}"
}
if [[ "$LANGUAGE" == de ]]; then
    running_label='Projecteur läuft'
    battery_known='Akku 73 %'
    battery_unknown='Akkustand unbekannt'
    battery_charging='Akku 42 % · lädt'
    battery_full='Akku 100 % · voll'
    battery_error='Akkustand unbekannt · Ladefehler'
    battery_empty='Akku 0 %'
    preferences_label='Einstellungen …'
    stop_label='Projecteur Beenden'
else
    running_label='Projecteur Is Running'
    battery_known='Battery 73%'
    battery_unknown='Battery level unknown'
    battery_charging='Battery 42% · charging'
    battery_full='Battery 100% · fully charged'
    battery_error='Battery level unknown · charging error'
    battery_empty='Battery 0%'
    preferences_label='Preferences…'
    stop_label='Stop Projecteur'
fi
state=$(smoke_call MenuState)
[[ "$state" == *'"startEnabled":true'* && "$state" == *'"quitEnabled":false'* ]]
[[ "$state" == *"$preferences_label"* ]]
smoke_call ShowPreferences
smoke_call StartTwice
state=$(smoke_call MenuState)
[[ "$state" == *'"starting":true'* && "$state" == *'"startEnabled":false'* && "$state" == *'"spawnCount":1'* ]]
sleep 1
state=$(smoke_call MenuState)
printf 'Running menu: %s\n' "$state"
[[ "$state" == *"\"start\":\"$running_label\""* && "$state" == *'"quitEnabled":true'* ]]
[[ "$state" == *"\"$stop_label\""* ]]
[[ "$state" == *"Spotlight USB — $battery_known"* && "$state" == *"Spotlight Bluetooth — $battery_unknown"* ]]
[[ "$state" == *'"preferencesCount":0'* ]]
smoke_call StartTwice
smoke_call ShowPreferences
smoke_call SetBattery '[42, 100]' "['charging', 'full']"
sleep 0.3
state=$(smoke_call MenuState)
[[ "$state" == *'"spawnCount":1'* && "$state" == *'"preferencesCount":1'* ]]
[[ "$state" == *"$battery_charging"* && "$state" == *"$battery_full"* ]]
smoke_call OpenMenu
sleep 0.3
smoke_call Snapshot "$PROJECTEUR_SMOKE_DIR/menu-running.png"
smoke_call CloseMenu
smoke_call SetBattery '[-1, 0]' "['charging-error', 'discharging']"
sleep 0.3
state=$(smoke_call MenuState)
[[ "$state" == *"$battery_error"* && "$state" == *"$battery_empty"* ]]
smoke_call QuitTwice
sleep 0.4
smoke_call QuitTwice
smoke_call ShowPreferences
state=$(smoke_call MenuState)
printf 'Stopped menu: %s\n' "$state"
[[ "$state" == *'"startEnabled":true'* && "$state" == *'"quitEnabled":false'* && "$state" == *'"running":false'* ]]
[[ "$state" == *'"quitCount":1'* && "$state" == *'"spawnCount":1'* && "$state" == *'"notifications":[]'* ]]
smoke_call StartTwice
sleep 1
state=$(smoke_call MenuState)
[[ "$state" == *'"spawnCount":2'* && "$state" == *"\"start\":\"$running_label\""* && "$state" == *"$battery_known"* ]]
smoke_call QuitTwice
sleep 0.4
state=$(smoke_call MenuState)
[[ "$state" == *'"quitCount":2'* && "$state" == *'"notifications":[]'* ]]
printf 'Menu lifecycle, repeated clicks, background start, preferences and battery updates passed.\n'
# A stalled backend must not leave a dimmed desktop or hidden cursor behind.
gdbus call --session --dest org.projecteur.Gnome --object-path /org/projecteur/Gnome \
    --method org.projecteur.Gnome.SetState true '{"showCenterDot":true}'
sleep 6.2
state=$(gdbus call --session --dest org.projecteur.Smoke --object-path /org/projecteur/Smoke --method org.projecteur.Smoke.State)
[[ "$state" == *'"active":false'* && "$state" == *'"visible":0'* ]]
printf 'Abandoned overlay lease expired correctly.\n'
gdbus call --session --dest org.projecteur.Gnome --object-path /org/projecteur/Gnome \
    --method org.projecteur.Gnome.SetState true '{"zoomEnabled":true,"hideCursor":true}'
sleep 0.25
gnome-extensions disable projecteur@projecteur.github.io
sleep 0.25
if gdbus call --session --dest org.projecteur.Gnome --object-path /org/projecteur/Gnome \
    --method org.projecteur.Gnome.Ping >/dev/null 2>&1; then
    printf 'FAIL: D-Bus service leaked after extension disable\n'
    exit 1
fi
gnome-extensions enable projecteur@projecteur.github.io
sleep 0.5
gdbus call --session --dest org.projecteur.Gnome --object-path /org/projecteur/Gnome \
    --method org.projecteur.Gnome.Ping
state=$(gdbus call --session --dest org.projecteur.Smoke --object-path /org/projecteur/Smoke --method org.projecteur.Smoke.State)
[[ "$state" == *'"active":false'* && "$state" == *'"visible":0'* ]]
printf 'Disable during active magnification and fresh re-enable passed.\n'
if grep -E 'JS ERROR|JS WARNING|Shader compilation failed|Failed to compile|Error enabling extension|Error disabling extension' "$PROJECTEUR_SMOKE_DIR/shell.log"; then
    exit 1
fi
printf 'GNOME runtime smoke test passed (spotlight, pointer, live magnifier, disable/re-enable).\n'
