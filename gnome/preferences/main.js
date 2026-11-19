// SPDX-License-Identifier: MIT
import Adw from 'gi://Adw?version=1';
import Gdk from 'gi://Gdk?version=4.0';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk?version=4.0';
import {DevicesPage} from './devices.js';
import {_} from './i18n.js';

const service = 'org.projecteur.Projecteur';
const controlPath = '/org/projecteur/Projecteur/Control';
const preferencesPath = '/org/projecteur/Projecteur/Preferences';
const preferencesInterface = service + '.Preferences';

Gio._promisify(Gio.DBusProxy, 'new_for_bus', 'new_for_bus_finish');
Gio._promisify(Gio.DBusProxy.prototype, 'call', 'call_finish');
Gio._promisify(Gio.DBusConnection.prototype, 'call', 'call_finish');

function group(page, title, description) {
    const row = new Adw.PreferencesGroup({title, description: description ?? ''});
    page.add(row);
    return row;
}

function button(label, callback, cssClass) {
    const widget = new Gtk.Button({label, valign: Gtk.Align.CENTER});
    if (cssClass)
        widget.add_css_class(cssClass);
    widget.connect('clicked', callback);
    return widget;
}

function actionRow(parent, title, subtitle = '') {
    const row = new Adw.ActionRow({title, subtitle, use_markup: false});
    parent.add(row);
    return row;
}

function spinRow(parent, title, min, max, step, digits = 0) {
    const row = new Adw.SpinRow({title, digits, numeric: true, adjustment: new Gtk.Adjustment({
        lower: min, upper: max, step_increment: step, page_increment: step * 10,
    })});
    parent.add(row);
    return row;
}

function switchRow(parent, title) {
    const row = new Adw.SwitchRow({title});
    parent.add(row);
    return row;
}

function confirm(parent, heading, body, label, callback) {
    const dialog = new Adw.AlertDialog({heading, body, close_response: 'cancel'});
    dialog.add_response('cancel', _('Cancel'));
    dialog.add_response('confirm', label);
    dialog.set_response_appearance('confirm', Adw.ResponseAppearance.DESTRUCTIVE);
    dialog.connect('response::confirm', callback);
    dialog.present(parent);
}

class Preferences {
    constructor(app) {
        this.app = app;
        this.syncing = false;
        this.finished = false;
        this.refreshing = false;
        this.refreshPending = false;
        this.cancellable = new Gio.Cancellable();
        this.fields = [];
        this.state = {};
        this.window = new Adw.ApplicationWindow({
            application: app, title: 'Projecteur', default_width: 700, default_height: 690,
            icon_name: preferencesInterface,
        });
        this.window.set_size_request(430, 360);
        this.window.connect('close-request', () => {
            this.finished = true;
            this.devices.close();
            this.cancellable.cancel();
            for (const [proxy, handler] of this.subscriptions ?? [])
                proxy.disconnect(handler);
            return false;
        });
        this.toastOverlay = new Adw.ToastOverlay();
        this.window.set_content(this.toastOverlay);

        const toolbar = new Adw.ToolbarView();
        this.toastOverlay.set_child(toolbar);
        const header = new Adw.HeaderBar();
        toolbar.add_top_bar(header);
        const menu = new Gio.Menu();
        menu.append(_('About Projecteur'), 'app.about');
        header.pack_end(new Gtk.MenuButton({icon_name: 'open-menu-symbolic', menu_model: menu,
            tooltip_text: _('Menu')}));
        this.stack = new Adw.ViewStack();
        header.set_title_widget(new Adw.ViewSwitcher({stack: this.stack, policy: Adw.ViewSwitcherPolicy.WIDE}));
        toolbar.set_content(this.stack);
        this.serviceBanner = new Adw.Banner({title: _('The Projecteur service is disconnected.'),
            button_label: _('Start Service'), revealed: true});
        this.serviceBanner.connect('button-clicked', () => this.startService());
        toolbar.add_top_bar(this.serviceBanner);
        this.stack.sensitive = false;

        this.buildOverlay();
        this.devices = new DevicesPage(this);
        this.stack.add_titled_with_icon(this.devices.page, 'devices', _('Devices'), 'input-mouse-symbolic');
        this.buildTimer();
        this.connectService();
    }

    async connectService() {
        const paths = [[controlPath, service], [preferencesPath, preferencesInterface]];
        try {
            const [control, preferences] = await Promise.all(paths.map(([path, iface]) =>
                Gio.DBusProxy.new_for_bus(Gio.BusType.SESSION,
                    Gio.DBusProxyFlags.DO_NOT_AUTO_START, null, service, path, iface, this.cancellable)));
            if (this.finished)
                return;
            this.control = control;
            this.proxy = preferences;
            this.subscriptions = [
                [control, control.connect('notify::g-name-owner', () => this.ownerChanged())],
                [preferences, preferences.connect('notify::g-name-owner', () => this.ownerChanged())],
                [control, control.connect('g-properties-changed', () => this.syncControls())],
                [preferences, preferences.connect('g-signal', (_proxy, _sender, name) => {
                    if (name === 'StateChanged')
                        this.refresh();
                })],
            ];
            this.ownerChanged();
            if (!control.g_name_owner)
                this.startService();
        } catch (error) {
            if (!this.finished)
                this.toast(error.message);
        }
    }

    get owner() {
        const owner = this.control?.g_name_owner;
        return owner && owner === this.proxy?.g_name_owner ? owner : null;
    }

    ownerChanged() {
        const connected = Boolean(this.owner);
        this.stack.sensitive = connected;
        this.serviceBanner.revealed = !connected;
        if (connected) {
            this.refresh();
        } else {
            this.state = {};
            this.devices.sync([], [], []);
        }
        this.syncControls();
    }

    async startService() {
        try {
            // The desktop and autostart entries can race for the same service.
            // Standard activation lets D-Bus reuse the running instance.
            await Gio.DBus.session.call('org.freedesktop.DBus', '/org/freedesktop/DBus',
                'org.freedesktop.DBus', 'StartServiceByName', new GLib.Variant('(su)', [service, 0]),
                null, Gio.DBusCallFlags.NONE, 5000, this.cancellable);
        } catch (error) {
            if (!this.finished)
                this.toast(_('Could not start the service. %s').replace('%s', () => error.message));
        }
    }

    read(key, fallback) {
        return this.control?.get_cached_property(key)?.recursiveUnpack() ?? fallback;
    }

    async call(proxy, method, signature, args, timeout = 3000, cancellable = this.cancellable) {
        const parameters = signature ? new GLib.Variant(signature, args) : null;
        const reply = await proxy.call(method, parameters, Gio.DBusCallFlags.NO_AUTO_START,
            timeout, cancellable);
        return reply.recursiveUnpack()[0];
    }

    async run(proxy, method, signature, args, rejected = _('Could not apply the change.')) {
        try {
            const value = await this.call(proxy, method, signature, args);
            if (value === false)
                this.toast(rejected);
            return value;
        } catch (error) {
            if (!this.finished)
                this.toast(error.message);
            return false;
        }
    }

    settingsCall(method, signature, args, rejected) {
        return this.run(this.proxy, method, signature, args, rejected);
    }

    controlCall(method, signature, args) {
        return this.run(this.control, method, signature, args);
    }

    async refresh() {
        if (this.refreshing) {
            this.refreshPending = true;
            return;
        }
        this.refreshing = true;
        try {
            do {
                this.refreshPending = false;
                const owner = this.owner;
                if (!owner || this.finished)
                    break;
                const reply = await this.call(this.proxy, 'GetState');
                if (this.owner !== owner || this.finished)
                    continue;
                this.state = JSON.parse(reply);
                this.syncControls(true);
                this.devices.refreshMappings();
            } while (this.refreshPending);
        } catch (error) {
            if (!this.finished && this.owner)
                this.toast(error.message);
        } finally {
            this.refreshing = false;
            if (this.refreshPending && this.owner && !this.finished)
                this.refresh();
        }
    }

    toast(title) {
        if (!this.finished)
            this.toastOverlay.add_toast(new Adw.Toast({title}));
    }

    bind(row, signal, read, write, key) {
        this.fields.push([key, write]);
        row.connect(signal, () => {
            if (!this.syncing)
                this.settingsCall('SetOverlay', '(ss)', [key, JSON.stringify([read()])]);
        });
    }

    overlaySwitch(parent, title, key) {
        const row = switchRow(parent, title);
        this.bind(row, 'notify::active', () => row.active, value => { row.active = value; }, key);
    }

    overlaySpin(parent, title, key, min, max, step, digits = 0, multiplier = 1) {
        const row = spinRow(parent, title, min, max, step, digits);
        this.bind(row, 'notify::value', () => row.value / multiplier,
            value => { row.value = value * multiplier; }, key);
    }

    overlayColor(parent, title, key) {
        const dialog = new Gtk.ColorDialog({title, with_alpha: false});
        const picker = new Gtk.ColorDialogButton({dialog, valign: Gtk.Align.CENTER});
        const row = actionRow(parent, title);
        row.add_suffix(picker);
        row.activatable_widget = picker;
        this.bind(picker, 'notify::rgba', () => {
            const color = picker.rgba;
            return '#' + [color.red, color.green, color.blue]
                .map(channel => Math.round(channel * 255).toString(16).padStart(2, '0')).join('');
        }, value => {
            const color = new Gdk.RGBA();
            if (color.parse(value))
                picker.rgba = color;
        }, key);
    }

    buildOverlay() {
        const page = new Adw.PreferencesPage();
        this.stack.add_titled_with_icon(page, 'overlay', 'Overlay', 'view-reveal-symbolic');
        const general = group(page, 'Overlay');
        this.overlayEnabled = switchRow(general, _('Enable Overlay'));
        this.overlayEnabled.connect('notify::active', () => {
            if (!this.syncing)
                this.controlCall('SetOverlayEnabled', '(b)', [this.overlayEnabled.active]);
        });
        this.overlayStatus = actionRow(general, _('GNOME Extension Disconnected'),
            _('Enable the Projecteur extension in GNOME to display the effects.'));
        const preview = actionRow(general, _('Preview'), _('Show the effect for ten seconds.'));
        this.preview = button(_('Start Preview'), () => this.settingsCall('Preview', null, null,
            _('The overlay is unavailable or in use.')), 'suggested-action');
        preview.add_suffix(this.preview);
        this.overlaySpin(general, _('Circle Size (% of Screen Height)'), 'spotSize', 5, 100, 1);
        this.overlaySwitch(general, _('Hide the Cursor During the Effect'), 'hideCursor');
        this.overlaySwitch(general, _('Dim All Displays'), 'multiScreenOverlayEnabled');

        const presets = group(page, _('Presets'));
        this.presetNames = [];
        this.preset = new Adw.ComboRow({title: _('Saved Preset'), use_markup: false});
        presets.add(this.preset);
        this.preset.connect('notify::selected', () => this.updatePresetButtons());
        const presetButtons = actionRow(presets, _('Manage Preset'));
        this.loadPreset = button(_('Load'), () =>
            this.controlCall('LoadPreset', '(s)', [this.presetNames[this.preset.selected]]));
        this.deletePreset = button(_('Delete…'), () => {
            const name = this.presetNames[this.preset.selected];
            confirm(this.window, _('Delete Preset?'), name, _('Delete'),
                () => this.settingsCall('DeletePreset', '(s)', [name]));
        });
        presetButtons.add_suffix(this.loadPreset);
        presetButtons.add_suffix(this.deletePreset);
        const savePreset = new Adw.EntryRow({title: _('New Preset Name')});
        const save = async () => {
            const name = savePreset.text.trim();
            if (name && await this.settingsCall('SavePreset', '(s)', [name],
                _('The name is already in use or contains a slash.')))
                savePreset.text = '';
        };
        const saveButton = button(_('Save'), save);
        saveButton.sensitive = false;
        savePreset.add_suffix(saveButton);
        savePreset.connect('changed', () => { saveButton.sensitive = Boolean(savePreset.text.trim()); });
        savePreset.connect('entry-activated', save);
        presets.add(savePreset);

        const shade = group(page, 'Spotlight');
        this.overlaySwitch(shade, _('Dim the Surroundings'), 'showSpotShade');
        this.overlayColor(shade, _('Shade Color'), 'shadeColor');
        this.overlaySpin(shade, _('Opacity (%)'), 'shadeOpacity', 0, 100, 1, 0, 100);

        const dot = group(page, 'Pointer');
        this.overlaySwitch(dot, _('Show Pointer'), 'showCenterDot');
        const dotModes = ['solid', 'diffuse'];
        const dotMode = new Adw.ComboRow({title: _('Appearance'), model: Gtk.StringList.new([_('Solid'), _('Diffuse')])});
        dot.add(dotMode);
        this.bind(dotMode, 'notify::selected', () => dotModes[dotMode.selected],
            value => { dotMode.selected = Math.max(0, dotModes.indexOf(value)); }, 'dotMode');
        this.overlaySpin(dot, _('Size (px)'), 'dotSize', 3, 100, 1);
        this.overlayColor(dot, _('Pointer Color'), 'dotColor');
        this.overlaySpin(dot, _('Opacity (%)'), 'dotOpacity', 0, 100, 1, 0, 100);
        this.overlaySwitch(dot, _('Show Fading Trail'), 'dotTrailEnabled');

        const zoom = group(page, _('Magnifier'));
        this.overlaySwitch(zoom, _('Show Magnifier'), 'zoomEnabled');
        this.overlaySpin(zoom, _('Magnification'), 'zoomFactor', 1.5, 20, 0.1, 1);

        const border = group(page, _('Circle Border'));
        this.overlaySwitch(border, _('Show Border'), 'showBorder');
        this.overlaySpin(border, _('Width (% of Circle Size)'), 'borderSize', 0, 100, 1);
        this.overlayColor(border, _('Border Color'), 'borderColor');
        this.overlaySpin(border, _('Opacity (%)'), 'borderOpacity', 0, 100, 1, 0, 100);

        const defaults = group(page, _('Defaults'));
        actionRow(defaults, _('Reset Overlay Settings')).add_suffix(button(_('Reset…'),
            () => confirm(this.window, _('Reset Overlay Settings?'),
                _('This restores the default overlay settings.'),
                _('Reset'), () => this.settingsCall('ResetOverlay'))));
    }

    buildTimer() {
        const page = new Adw.PreferencesPage();
        this.stack.add_titled_with_icon(page, 'timer', 'Timer', 'alarm-symbolic');
        const timer = group(page, _('Presentation Timer'));
        this.timerEnabled = switchRow(timer, _('Enable Timer'));
        this.timerEnabled.connect('notify::active', () => {
            if (!this.syncing)
                this.controlCall('SetTimerEnabled', '(b)', [this.timerEnabled.active]);
        });
        this.duration = spinRow(timer, _('Duration (Seconds)'), 60, 180 * 60, 1);
        this.duration.connect('notify::value', () => {
            if (!this.syncing)
                this.controlCall('SetTimerDurationSeconds', '(i)', [Math.round(this.duration.value)]);
        });
        this.timerStatus = actionRow(timer, _('Ready'));
        this.timerStart = button(_('Start'), () =>
            this.controlCall(this.read('TimerState') === 'idle' ? 'StartTimer' : 'RestartTimer'));
        this.timerReset = button(_('Reset'), () => this.controlCall('ResetTimer'));
        this.timerStatus.add_suffix(this.timerStart);
        this.timerStatus.add_suffix(this.timerReset);
    }

    updatePresetButtons() {
        const selected = this.preset.selected < this.presetNames.length;
        this.loadPreset.sensitive = selected;
        this.deletePreset.sensitive = selected;
    }

    syncControls(overlayChanged = false) {
        this.syncing = true;
        try {
            if (overlayChanged) {
                for (const [key, write] of this.fields) {
                    if (Object.hasOwn(this.state.overlay, key))
                        write(this.state.overlay[key]);
                }
            }
            this.overlayEnabled.active = this.read('OverlayEnabled', false);
            this.overlayStatus.visible = !this.state.available;
            this.preview.sensitive = this.state.available && this.overlayEnabled.active
                && (this.state.preview || !this.read('SpotlightActive', false));
            this.preview.label = this.state.preview ? _('Stop Preview') : _('Start Preview');

            const presets = this.read('Presets', []);
            const current = this.read('CurrentPreset', '');
            if (JSON.stringify(presets) !== JSON.stringify(this.presetNames)) {
                const selection = this.presetNames[this.preset.selected];
                this.presetNames = presets;
                this.preset.model = Gtk.StringList.new(presets);
                this.preset.selected = Math.max(0, presets.indexOf(selection));
            }
            if (current !== this.currentPreset && presets.includes(current))
                this.preset.selected = presets.indexOf(current);
            this.currentPreset = current;
            this.updatePresetButtons();

            const enabled = this.read('TimerEnabled', false);
            const state = this.read('TimerState', 'idle');
            const duration = this.read('TimerDurationSeconds', 900);
            const remaining = this.read('TimerRemainingSeconds', duration);
            this.timerEnabled.active = enabled;
            this.duration.value = duration;
            this.timerStart.label = state === 'idle' ? _('Start') : _('Restart');
            this.timerStart.sensitive = enabled;
            this.timerReset.sensitive = enabled && state !== 'idle';
            this.timerStatus.title = {idle: _('Ready'), running: _('Timer Running'), completed: _('Time Is Up')}[state];
            this.timerStatus.subtitle = `${Math.floor(remaining / 60)}:${String(remaining % 60).padStart(2, '0')}`;

            if (this.owner) {
                this.devices.sync(this.state.devices ?? [], this.read('ConnectedDeviceBatteryLevels', []),
                    this.read('ConnectedDeviceBatteryStatuses', []));
            }
        } finally {
            this.syncing = false;
        }
    }

    about() {
        this.window.present();
        new Adw.AboutDialog({
            application_name: 'Projecteur', application_icon: preferencesInterface,
            developer_name: 'Projecteur contributors', version: this.state.version ?? '',
            license_type: Gtk.License.MIT_X11,
            website: 'https://github.com/ALsbraun/Projecteur',
            issue_url: 'https://github.com/ALsbraun/Projecteur/issues',
        }).present(this.window);
    }
}

const app = new Adw.Application({
    application_id: preferencesInterface,
    flags: Gio.ApplicationFlags.HANDLES_COMMAND_LINE,
});
for (const option of ['show', 'hide', 'about', 'minimize', 'minimize-existing'])
    app.add_main_option(option, 0, GLib.OptionFlags.NONE, GLib.OptionArg.NONE, '', null);
let preferences;
const about = new Gio.SimpleAction({name: 'about'});
about.connect('activate', () => preferences?.about());
app.add_action(about);
app.set_accels_for_action('window.close', ['<Control>w']);
app.connect('command-line', (_app, command) => {
    const options = command.get_options_dict();
    if (options.contains('hide')) {
        preferences?.window.close();
    } else if (options.contains('minimize-existing')) {
        preferences?.window.minimize();
    } else {
        if (!preferences || preferences.finished)
            preferences = new Preferences(app);
        preferences.window.present();
        if (options.contains('about'))
            preferences.about();
        if (options.contains('minimize'))
            preferences.window.minimize();
    }
    return 0;
});
app.run(['projecteur-preferences', ...ARGV]);
