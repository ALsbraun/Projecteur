// SPDX-License-Identifier: MIT
// Runs the real application on a private session bus with a mock Shell overlay.
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

const overlayName = 'org.projecteur.Gnome';
const appName = 'org.projecteur.Projecteur';
const path = '/org/projecteur/Projecteur/Control';
const preferencesName = appName + '.Preferences';
const preferencesPath = '/org/projecteur/Projecteur/Preferences';
const records = [];
const mock = Gio.DBusExportedObject.wrapJSObject(`<node>
  <interface name="org.projecteur.Gnome"><method name="SetState">
  <arg direction="in" type="b"/><arg direction="in" type="s"/>
  </method></interface></node>`, {
    SetState(active, json) { records.push({active, settings: JSON.parse(json)}); },
});
const bus = Gio.bus_get_sync(Gio.BusType.SESSION, null);
mock.export(bus, '/org/projecteur/Gnome');
let owner = 0;
let child = null;
const temporary = GLib.dir_make_tmp('projecteur-test-XXXXXX');

function assert(condition, message) {
    if (!condition)
        throw new Error(message);
}

function delay(ms) {
    return new Promise(resolve => GLib.timeout_add(GLib.PRIORITY_DEFAULT, ms, () => {
        resolve();
        return GLib.SOURCE_REMOVE;
    }));
}

async function until(predicate, message, timeout = 8000) {
    const deadline = GLib.get_monotonic_time() + timeout * 1000;
    while (!predicate()) {
        assert(GLib.get_monotonic_time() < deadline, message);
        await delay(25);
    }
}

function ownOverlay() {
    return new Promise(resolve => {
        owner = Gio.bus_own_name_on_connection(bus, overlayName,
            Gio.BusNameOwnerFlags.NONE, resolve, null);
    });
}

function call(method, parameters = null, preferences = false) {
    return new Promise((resolve, reject) => {
        bus.call(appName, preferences ? preferencesPath : path, preferences ? preferencesName : appName,
            method, parameters, null,
            Gio.DBusCallFlags.NO_AUTO_START, 4000, null, (connection, result) => {
                try { resolve(connection.call_finish(result)); }
                catch (error) { reject(error); }
            });
    });
}

async function preferences(method, signature, ...args) {
    const reply = await call(method, signature ? new GLib.Variant(signature, args) : null, true);
    return reply.recursiveUnpack()[0];
}

function removeTree(file) {
    if (file.query_file_type(Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS, null) === Gio.FileType.DIRECTORY) {
        const entries = file.enumerate_children('standard::name', Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS, null);
        let info;
        while ((info = entries.next_file(null)) !== null)
            removeTree(file.get_child(info.get_name()));
        entries.close(null);
    }
    file.delete(null);
}

async function run() {
    await ownOverlay();
    const launcher = new Gio.SubprocessLauncher({flags: Gio.SubprocessFlags.NONE});
    const headless = GLib.getenv('PROJECTEUR_TEST_HEADLESS') === '1';
    launcher.setenv('QT_QPA_PLATFORM', headless ? 'projecteur-test-invalid-platform' : 'offscreen', true);
    launcher.unsetenv('DISPLAY');
    launcher.unsetenv('WAYLAND_DISPLAY');
    launcher.setenv('XDG_CURRENT_DESKTOP', 'GNOME', true);
    launcher.setenv('XDG_CONFIG_HOME', temporary, true);
    launcher.setenv('XDG_CACHE_HOME', temporary, true);
    launcher.setenv('KDE_FULL_SESSION', '', true);
    launcher.setenv('DESKTOP_SESSION', 'gnome', true);
    child = launcher.spawnv([ARGV[0], '--background', '--disable-uinput']);
    await until(() => records.length > 0, 'Application did not connect to the Shell overlay');
    assert(records.at(-1).active === false, 'Startup overlay must be inactive');

    const initial = JSON.parse(await preferences('GetState'));
    assert(initial.available && !initial.preview, 'Preferences did not report the Shell connection');
    if (headless) {
        const command = launcher.spawnv([ARGV[0], '--command=dot.size=19']);
        await new Promise((resolve, reject) => command.wait_check_async(null, (process, result) => {
            try { process.wait_check_finish(result); resolve(); }
            catch (error) { reject(error); }
        }));
        await until(() => records.at(-1).settings.dotSize === 19, 'A second command-line invocation did not reach the service');
    }
    assert(await preferences('SetOverlay', '(ss)', 'dotColor', '["#4273c8"]'), 'Color edit rejected');
    assert(await preferences('SetOverlay', '(ss)', 'spotSize', '[45]'), 'Size edit rejected');
    assert(await preferences('SetOverlay', '(ss)', 'zoomFactor', '[3.2]'), 'Zoom edit rejected');
    assert(await preferences('SetOverlay', '(ss)', 'hideCursor', '[false]'), 'Cursor edit rejected');
    assert(!await preferences('SetOverlay', '(ss)', 'spotSize', '["99"]'), 'Invalid type accepted');
    assert(!await preferences('SetOverlay', '(ss)', 'spotShape', '["Square"]'), 'Unavailable option accepted');
    const snapshot = JSON.parse(await preferences('GetState')).overlay;
    assert(snapshot.dotColor === '#4273c8' && snapshot.spotSize === 45 && snapshot.zoomFactor === 3.2
        && !snapshot.hideCursor, 'Preferences did not read back the edited settings');
    await until(() => records.at(-1).settings.spotSize === 45, 'Preference edit did not reach the Shell');

    assert(await preferences('SavePreset', '(s)', 'GTK demo'), 'Could not save a preset');
    assert(!await preferences('SavePreset', '(s)', 'GTK demo'), 'Duplicate preset was accepted');
    await preferences('ResetOverlay');
    assert(JSON.parse(await preferences('GetState')).overlay.spotSize === initial.overlay.spotSize,
        'Reset did not restore the default spot size');
    assert((await call('LoadPreset', new GLib.Variant('(s)', ['GTK demo']))).recursiveUnpack()[0],
        'Panel could not load a preset saved from preferences');
    assert(JSON.parse(await preferences('GetState')).overlay.spotSize === 45, 'Preset did not restore its settings');
    assert(await preferences('DeletePreset', '(s)', 'GTK demo'), 'Could not delete a preset');
    assert(await preferences('Preview'), 'Could not start preview');
    await until(() => records.at(-1).active, 'Preview did not reach the Shell');
    assert(JSON.parse(await preferences('GetState')).preview, 'Preview reported itself inactive');
    assert(await preferences('Preview'), 'Could not cancel preview');
    await until(() => !records.at(-1).active, 'Cancel did not hide preview');
    assert(await preferences('GetMappings', '(s)', 'missing') === '', 'Unknown device reported mappings');
    assert(!await preferences('SetDeviceOption', '(ssi)', 'missing', 'strength', 80),
        'Unknown device accepted a setting');

    await call('ApplyCommands', new GLib.Variant('(as)', [[
        'shade=off', 'dot=on', 'dot.color=#12abef', 'dot.size=13',
        'zoom=on', 'zoom.factor=2.5',
    ]]));
    await call('SetSpotlightActive', new GLib.Variant('(b)', [true]));
    await until(() => records.at(-1).active, 'Activation did not reach the overlay');
    const state = records.at(-1).settings;
    assert(!state.showSpotShade && state.showCenterDot && state.zoomEnabled, 'Modes serialized incorrectly');
    assert(state.dotColor === '#12abef' && state.dotSize === 13 && state.zoomFactor === 2.5,
        'Color, size or zoom did not reach the Shell');
    assert(typeof state.cursor === 'number', 'Cursor enum must be serialized as a number');
    const count = records.length;
    await until(() => records.length > count, 'Active state lease was not renewed', 3500);

    await call('SetOverlayEnabled', new GLib.Variant('(b)', [false]));
    await until(() => !records.at(-1).active, 'Disabling the overlay must hide it');
    await call('SetOverlayEnabled', new GLib.Variant('(b)', [true]));
    await call('SetSpotlightActive', new GLib.Variant('(b)', [true]));
    await until(() => records.at(-1).active, 'Could not reactivate overlay');
    Gio.bus_unown_name(owner);
    owner = 0;
    await delay(150);
    const beforeReconnect = records.length;
    await ownOverlay();
    await until(() => records.length > beforeReconnect, 'Extension restart did not restore state');
    assert(records.at(-1).active, 'Extension restart lost active state');

    await call('Quit');
    await until(() => !records.at(-1).active, 'Shutdown did not hide overlay');
    await new Promise((resolve, reject) => child.wait_check_async(null, (process, result) => {
        try { process.wait_check_finish(result); resolve(); }
        catch (error) { reject(error); }
    }));
    child = null;
    print('PASS: GNOME bridge startup, settings, activation, heartbeat, disable, reconnect and shutdown');
}

const loop = new GLib.MainLoop(null, false);
let failed = false;
run().catch(error => { printerr(error.message, error.stack); failed = true; }).finally(() => {
    if (child)
        child.force_exit();
    if (owner)
        Gio.bus_unown_name(owner);
    mock.unexport();
    removeTree(Gio.File.new_for_path(temporary));
    loop.quit();
});
loop.run();
if (failed)
    throw new Error('GNOME integration test failed');
