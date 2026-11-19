// SPDX-License-Identifier: MIT
import Adw from 'gi://Adw?version=1';
import Gdk from 'gi://Gdk?version=4.0';
import Gio from 'gi://Gio';
import Gtk from 'gi://Gtk?version=4.0';
import {_} from './i18n.js';

const actions = new Map([
    [1, _('Send Keyboard Shortcut')],
    [2, _('Cycle Presets')],
    [3, _('Toggle Spotlight')],
    [11, _('Scroll Horizontally')],
    [12, _('Scroll Vertically')],
    [13, _('Adjust Volume')],
]);
const shortcuts = [['alt-tab', 'Alt+Tab'], ['alt-f4', 'Alt+F4'], ['super', 'Super']];
const modifierKeys = new Set([
    'Control_L', 'Control_R', 'Shift_L', 'Shift_R', 'Alt_L', 'Alt_R',
    'Super_L', 'Super_R', 'Meta_L', 'Meta_R', 'ISO_Level3_Shift',
]);
const keyNames = {
    Return: 'Return', ISO_Left_Tab: 'Tab', BackSpace: 'Backspace', Escape: 'Esc',
    Page_Up: 'PgUp', Page_Down: 'PgDown', Print: 'Print', Scroll_Lock: 'ScrollLock',
    Caps_Lock: 'CapsLock', Num_Lock: 'NumLock', KP_Enter: 'Enter', Sys_Req: 'SysReq',
};
const same = (left, right) => JSON.stringify(left) === JSON.stringify(right);

function group(page, title) {
    const section = new Adw.PreferencesGroup({title});
    page.add(section);
    return section;
}

function row(parent, title, subtitle = '') {
    const item = new Adw.ActionRow({title, subtitle, use_markup: false});
    if (parent instanceof Gtk.ListBox)
        parent.append(item);
    else
        parent.add(item);
    return item;
}

function button(label, callback) {
    const widget = new Gtk.Button({label, valign: Gtk.Align.CENTER});
    widget.connect('clicked', callback);
    return widget;
}

function spin(parent, title, min, max, step) {
    const widget = new Adw.SpinRow({title, numeric: true, adjustment: new Gtk.Adjustment({
        lower: min, upper: max, step_increment: step, page_increment: step * 4,
    })});
    parent.add(widget);
    return widget;
}

function batteryLabel(level, status) {
    const labels = {
        charging: _('charging'), 'slow-charging': _('charging slowly'), 'almost-full': _('almost full'),
        full: _('fully charged'), 'invalid-battery': _('battery error'), 'thermal-error': _('temperature error'),
        'charging-error': _('charging error'),
    };
    const percentage = level >= 0 && level <= 100 ? _('%d%%').replace('%d', level).replace('%%', '%') : _('Unknown');
    return Object.hasOwn(labels, status) ? `${percentage} · ${labels[status]}` : percentage;
}

class MappingEditor {
    constructor(page, mapping) {
        this.page = page;
        this.device = page.selected.id;
        this.mapping = mapping;
        this.trigger = mapping;
        this.modifiers = new Set();
        this.pressed = new Set();
        this.activations = [];
        this.closed = false;
        this.dialog = new Adw.AlertDialog({
            heading: mapping ? _('Edit Button Mapping') : _('Add Button Mapping'),
            close_response: 'cancel',
        });
        this.dialog.add_response('cancel', _('Cancel'));
        this.dialog.add_response('save', _('Save'));
        this.dialog.set_response_appearance('save', Adw.ResponseAppearance.SUGGESTED);
        this.dialog.connect('response::save', () => this.save());
        this.dialog.connect('closed', () => this.close());

        const list = new Gtk.ListBox({selection_mode: Gtk.SelectionMode.NONE});
        list.add_css_class('boxed-list');
        this.dialog.set_extra_child(list);
        this.source = row(list, _('Presenter Input'));
        this.record = button(_('Record'), () => this.recordTrigger());
        this.source.add_suffix(this.record);

        this.movement = new Adw.ComboRow({title: _('Movement Input'),
            model: Gtk.StringList.new([_('Select…'), ...page.mappingData.movement.map(item => item.label)]),
            visible: page.mappingData.movement.length > 0});
        list.append(this.movement);
        this.movement.connect('notify::selected', () => {
            if (this.movement.selected > 0) {
                this.stopTriggerRecording();
                this.trigger = page.mappingData.movement[this.movement.selected - 1];
                this.movement.selected = 0;
                this.updateActions();
            }
        });

        this.action = new Adw.ComboRow({title: _('Action')});
        this.action.connect('notify::selected', () => this.updateSave());
        list.append(this.action);
        this.shortcut = row(list, _('Keyboard Shortcut'));
        this.keyButton = button(_('Record'), () => this.toggleShortcut());
        this.shortcut.add_suffix(this.keyButton);
        this.predefined = new Adw.ComboRow({title: _('Predefined Shortcut'),
            model: Gtk.StringList.new([_('Select…'), ...shortcuts.map(([, label]) => label)])});
        list.append(this.predefined);
        this.predefined.connect('notify::selected', () => {
            if (this.predefined.selected > 0) {
                this.recordingKeys = false;
                this.activations = [];
                this.standardShortcut = shortcuts[this.predefined.selected - 1];
                this.predefined.selected = 0;
                this.updateSave();
            }
        });

        const keys = new Gtk.EventControllerKey();
        keys.connect('key-pressed', (_controller, keyval, keycode, modifiers) => {
            if (!this.recordingKeys)
                return false;
            this.recordKey(keyval, keycode, modifiers);
            return true;
        });
        keys.connect('key-released', (_controller, keyval, keycode) => {
            if (this.recordingKeys) {
                this.modifiers.delete(keycode - 8);
                this.pressed.delete(keycode);
            }
        });
        keys.propagation_phase = Gtk.PropagationPhase.CAPTURE;
        this.dialog.add_controller(keys);
        this.updateActions(mapping?.type);
        this.dialog.present(this.page.frontend.window);
    }

    updateActions(type = this.actionIds?.[this.action.selected]) {
        this.source.subtitle = this.trigger?.label ?? _('No Input Selected');
        this.actionIds = this.trigger?.movement ? [12, 11, 13] : [1, 2, 3];
        this.action.model = Gtk.StringList.new(this.actionIds.map(id => actions.get(id)));
        this.action.selected = Math.max(0, this.actionIds.indexOf(type));
        this.updateSave();
    }

    updateSave() {
        const type = this.actionIds?.[this.action.selected];
        if (type !== 1)
            this.recordingKeys = false;
        this.shortcut.visible = type === 1;
        this.predefined.visible = type === 1;
        const oldShortcut = this.mapping?.shortcut;
        this.shortcut.subtitle = this.recordingKeys ? _('Press the keys, then stop recording')
            : this.activations.map(key => key.label).join(', ') || this.standardShortcut?.[1]
                || oldShortcut || _('No Keyboard Shortcut');
        this.keyButton.label = this.recordingKeys ? _('Stop Recording') : _('Record');
        this.dialog.set_response_enabled('save', Boolean(this.trigger && type
            && !this.recording && !this.recordingKeys
            && (type !== 1 || this.activations.length || this.standardShortcut || oldShortcut)));
    }

    async recordTrigger() {
        if (this.recording) {
            this.stopTriggerRecording();
            return;
        }
        this.recordingKeys = false;
        const frontend = this.page.frontend;
        const recording = new Gio.Cancellable();
        this.recording = recording;
        this.source.subtitle = _('Press a presenter button or button sequence');
        this.record.label = _('Cancel');
        this.updateSave();
        try {
            const reply = await frontend.call(frontend.proxy, 'RecordInput', '(s)', [this.device],
                12000, recording);
            if (this.recording !== recording || this.closed)
                return;
            if (reply)
                this.trigger = JSON.parse(reply);
            else
                frontend.toast(_('No presenter input was recorded.'));
        } catch (error) {
            if (!recording.is_cancelled() && !this.closed)
                frontend.toast(error.message);
        } finally {
            if (this.recording === recording) {
                this.recording = null;
                this.record.label = _('Record');
                this.updateActions();
            }
        }
    }

    stopTriggerRecording() {
        if (!this.recording)
            return;
        const frontend = this.page.frontend;
        frontend.settingsCall('CancelRecording');
        this.recording.cancel();
        this.recording = null;
        this.record.label = _('Record');
        this.updateActions();
    }

    toggleShortcut() {
        if (this.recordingKeys) {
            this.recordingKeys = false;
        } else {
            this.stopTriggerRecording();
            this.activations = [];
            this.standardShortcut = null;
            this.modifiers.clear();
            this.pressed.clear();
            this.recordingKeys = true;
        }
        this.updateSave();
    }

    recordKey(keyval, keycode, modifiers) {
        const name = Gdk.keyval_name(keyval);
        if (name === 'Escape') {
            this.recordingKeys = false;
            this.activations = [];
            this.updateSave();
            return;
        }
        const code = keycode - 8; // Linux Wayland/X11 GDK keycodes use XKB's offset.
        if (code <= 0 || this.pressed.has(keycode) || !name)
            return;
        this.pressed.add(keycode);
        if (modifierKeys.has(name)) {
            this.modifiers.add(code);
            return;
        }

        const held = new Set(this.modifiers);
        // Also handle modifiers that were already held when recording started.
        const modifierPairs = [
            [Gdk.ModifierType.SHIFT_MASK, 42, 54],
            [Gdk.ModifierType.CONTROL_MASK, 29, 97],
            [Gdk.ModifierType.ALT_MASK, 56, 100],
            [Gdk.ModifierType.SUPER_MASK | Gdk.ModifierType.META_MASK, 125, 126],
        ];
        for (const [mask, left, right] of modifierPairs) {
            if ((modifiers & mask) && !held.has(left) && !held.has(right))
                held.add(left);
        }

        const unicode = Gdk.keyval_to_unicode(keyval);
        const symbol = Object.hasOwn(keyNames, name) ? keyNames[name]
            : unicode >= 32 && unicode !== 127 ? String.fromCodePoint(unicode) : name;
        this.activations.push({code, symbol, modifiers: [...held],
            label: Gtk.accelerator_get_label(keyval, modifiers & Gtk.accelerator_get_default_mod_mask())});
        if (this.activations.length === 4)
            this.recordingKeys = false;
        this.updateSave();
    }

    save() {
        const update = {
            previous: this.mapping?.sequence ?? [],
            sequence: this.trigger.sequence,
            type: this.actionIds[this.action.selected],
        };
        if (update.type === 1 && this.standardShortcut) {
            update.shortcut = this.standardShortcut[0];
        } else if (update.type === 1 && this.activations.length) {
            update.shortcut = this.activations.map(({code, symbol, modifiers}) =>
                ({code, symbol, modifiers}));
        }
        this.page.updateMapping(this.device, update);
    }

    close() {
        this.closed = true;
        this.stopTriggerRecording();
        if (this.page.editor === this)
            this.page.editor = null;
    }
}

export class DevicesPage {
    constructor(frontend) {
        this.frontend = frontend;
        this.devices = [];
        this.mappingRows = [];
        this.mappingData = {items: [], movement: []};
        this.page = new Adw.PreferencesPage();
        const connection = group(this.page, _('Connected Presenters'));
        this.empty = row(connection, _('No Presenter Connected'),
            _('Connect the presenter via its USB receiver or Bluetooth.'));
        this.select = new Adw.ComboRow({title: _('Device'), use_markup: false, visible: false});
        this.select.connect('notify::selected', () => {
            if (!this.syncing) {
                this.syncSelection();
                this.refreshMappings();
            }
        });
        connection.add(this.select);
        this.battery = row(connection, _('Battery Level'));

        this.input = group(this.page, _('Button Mappings'));
        this.status = row(this.input, _('Device Access'));
        this.status.add_suffix(button(_('Retry'), () => frontend.settingsCall('RetryDevices')));
        this.interval = spin(this.input, _('Button Sequence Timeout (ms)'), 100, 950, 50);
        this.interval.connect('notify::value', () => this.setOption('interval', this.interval.value));
        this.add = row(this.input, _('Add Button Mapping'));
        this.add.add_suffix(button(_('Add…'), () => this.editMapping()));

        this.haptic = group(this.page, _('Timer Feedback'));
        this.strength = spin(this.haptic, _('Vibration on Completion (%)'), 0, 100, 5);
        this.strength.subtitle = _('Set to 0% to disable vibration.');
        this.strength.connect('notify::value', () => this.setOption('strength', this.strength.value));

        this.details = group(this.page, _('Device Details'));
        this.ids = row(this.details, _('Vendor and Product ID'));
        this.bus = row(this.details, _('Connection'));
        this.physicalPath = row(this.details, _('Device Path'));
        this.subDevices = row(this.details, _('Device Interfaces'));
        this.hidpp = row(this.details, 'HID++');
        this.syncSelection();
    }

    setOption(name, value) {
        if (!this.syncing && this.selected) {
            this.frontend.settingsCall('SetDeviceOption', '(ssi)',
                [this.selected.id, name, Math.round(value)]);
        }
    }

    sync(devices, levels, statuses) {
        this.levels = levels;
        this.statuses = statuses;
        const old = this.devices[this.select.selected]?.id;
        this.syncing = true;
        if (!same(devices.map(device => device.id), this.devices.map(device => device.id))) {
            this.select.model = Gtk.StringList.new(devices.map(device =>
                `${device.name} (${device.vendor}:${device.product}) · ${device.bus}`));
            this.select.selected = Math.max(0, devices.findIndex(device => device.id === old));
        }
        this.devices = devices;
        this.syncSelection();
    }

    syncSelection() {
        this.syncing = true;
        const device = this.devices[this.select.selected];
        if (device?.id !== this.selected?.id || (!device?.inputAvailable && this.selected?.inputAvailable)) {
            this.close();
            this.renderMappings({items: [], movement: []});
        }
        this.selected = device;
        this.empty.visible = !device;
        this.select.visible = Boolean(device);
        this.battery.visible = Boolean(device);
        this.input.visible = Boolean(device);
        this.haptic.visible = Boolean(device?.vibration);
        this.details.visible = Boolean(device);
        if (device) {
            const index = this.select.selected;
            this.battery.subtitle = batteryLabel(this.levels[index], this.statuses[index]);
            this.status.visible = Boolean(device.inputStatus);
            this.status.subtitle = device.inputStatus;
            this.interval.value = device.interval;
            this.strength.value = device.strength;
            this.interval.sensitive = device.inputAvailable;
            this.add.sensitive = device.inputAvailable;
            this.ids.subtitle = `${device.vendor}:${device.product}`;
            this.bus.subtitle = device.bus;
            this.physicalPath.subtitle = device.physicalPath;
            this.hidpp.visible = device.hidpp.length > 0;
            this.hidpp.subtitle = device.hidpp.map(({protocol, presenter, receiver}) =>
                [protocol, presenter, receiver].filter(Boolean).join(' · ')).join('\n');
            this.subDevices.subtitle = device.subDevices.map(({path, mode, flags}) =>
                [path, mode, flags.join(', ')].filter(Boolean).join(' · ')).join('\n');
        }
        this.syncing = false;
    }

    async refreshMappings() {
        const device = this.selected?.id;
        const owner = this.frontend.owner;
        if (!device || !owner)
            return;
        const lastRequest = this.lastRequest = {};
        try {
            const result = await this.frontend.call(this.frontend.proxy, 'GetMappings', '(s)', [device]);
            if (this.lastRequest === lastRequest && this.selected?.id === device
                && owner === this.frontend.owner && result)
                this.renderMappings(JSON.parse(result));
        } catch (error) {
            if (this.lastRequest === lastRequest && !this.frontend.finished)
                this.frontend.toast(error.message);
        }
    }

    renderMappings(data) {
        const editable = Boolean(this.selected?.inputAvailable);
        if (same(data, this.mappingData) && editable === this.mappingEditable)
            return;
        this.mappingData = data;
        this.mappingEditable = editable;
        for (const item of this.mappingRows)
            this.input.remove(item);
        this.mappingRows = data.items.map(mapping => {
            const description = [actions.get(mapping.type), mapping.shortcut].filter(Boolean).join(' · ');
            const item = row(this.input, mapping.label, description);
            item.add_suffix(button(_('Edit…'), () => this.editMapping(mapping)));
            item.add_suffix(button(_('Delete…'), () => this.deleteMapping(mapping)));
            item.sensitive = editable;
            return item;
        });
    }

    editMapping(mapping) {
        if (!this.editor && this.selected?.inputAvailable)
            this.editor = new MappingEditor(this, mapping);
    }

    deleteMapping(mapping) {
        const id = this.selected.id;
        const dialog = new Adw.AlertDialog({heading: _('Delete Button Mapping?'), body: mapping.label,
            close_response: 'cancel'});
        dialog.add_response('cancel', _('Cancel'));
        dialog.add_response('delete', _('Delete'));
        dialog.set_response_appearance('delete', Adw.ResponseAppearance.DESTRUCTIVE);
        dialog.connect('response::delete', () =>
            this.updateMapping(id, {previous: mapping.sequence, sequence: []}));
        dialog.present(this.frontend.window);
    }

    async updateMapping(device, update) {
        await this.frontend.settingsCall('UpdateMapping', '(ss)', [device, JSON.stringify(update)],
            _('The input is already mapped or the device was disconnected.'));
        this.refreshMappings();
    }

    close() {
        if (this.editor) {
            const editor = this.editor;
            editor.close();
            editor.dialog.force_close();
        }
    }
}
