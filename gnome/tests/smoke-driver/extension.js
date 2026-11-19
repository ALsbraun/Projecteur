// SPDX-License-Identifier: MIT
// Test fixture only. Never installed by the RPM.
import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import St from 'gi://St';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
const XML = `<node><interface name="org.projecteur.Smoke">
<method name="Prepare"/>
<method name="Snapshot"><arg type="s" name="filename" direction="in"/></method>
<method name="State"><arg type="s" name="state" direction="out"/></method>
<method name="MenuState"><arg type="s" name="state" direction="out"/></method>
<method name="StartTwice"/>
<method name="QuitTwice"/>
<method name="ShowPreferences"/>
<method name="OpenMenu"/>
<method name="CloseMenu"/>
<method name="SetBattery"><arg type="ai" name="levels" direction="in"/><arg type="as" name="statuses" direction="in"/></method>
</interface></node>`;
const APP_SERVICE = 'org.projecteur.Projecteur';
const APP_PATH = '/org/projecteur/Projecteur/Control';
const APP_XML = `<node><interface name="org.projecteur.Projecteur">
<property name="ConnectedDevices" type="as" access="read"/>
<property name="ConnectedDeviceBatteryLevels" type="ai" access="read"/>
<property name="ConnectedDeviceBatteryStatuses" type="as" access="read"/>
<property name="Presets" type="as" access="read"/>
<method name="Quit"/>
<method name="ShowPreferences"/>
</interface></node>`;
export default class Driver extends Extension {
    enable() {
        this._spawnCount = 0;
        this._quitCount = 0;
        this._preferencesCount = 0;
        this._notifications = [];
        this._object = Gio.DBusExportedObject.wrapJSObject(XML, this);
        this._object.export(Gio.DBus.session, '/org/projecteur/Smoke');
        this._owner = Gio.bus_own_name_on_connection(Gio.DBus.session,
            'org.projecteur.Smoke', Gio.BusNameOwnerFlags.NONE, null, null);
    }
    Prepare() {
        Main.overview.hide();
        if (!this._surface) {
            this._surface = new St.Widget({x: 0, y: 40, width: 1280, height: 680,
                style: 'background-color: #f7f7fa;'});
            for (let y = 0; y < 12; y++) {
                for (let x = 0; x < 18; x++) {
                    this._surface.add_child(new St.Label({x: x * 80, y: y * 60,
                        text: `${x},${y}`, style: 'color: #333333; font-size: 16px;'}));
                }
            }
            Main.uiGroup.add_child(this._surface);
            this._pointer = global.stage.get_context().get_backend().get_default_seat().create_virtual_device(Clutter.InputDeviceType.POINTER_DEVICE);
        }
        global.stage.get_context().get_backend().get_default_seat().warp_pointer(640, 360);
    }
    State() {
        const extension = Main.extensionManager.lookup('projecteur@projecteur.github.io').stateObj;
        return JSON.stringify({active: extension._active, views: extension._views.length,
            visible: extension._views.filter(v => v.actor.visible).length,
            lens: extension._views.some(v => v.lens.visible),
            pointer: [extension._cursorTracker.get_pointer()[0].x, extension._cursorTracker.get_pointer()[0].y], globalPointer: global.get_pointer(), overview: Main.overview.visible});
    }
    _extension() {
        return Main.extensionManager.lookup('projecteur@projecteur.github.io').stateObj;
    }
    MenuState() {
        const extension = this._extension();
        return JSON.stringify({
            start: extension._startItem.label.text,
            startEnabled: extension._startItem.reactive,
            quitEnabled: extension._quitItem.reactive,
            actionsEnabled: extension._applicationItems.every(item => item.reactive),
            starting: extension._starting, quitting: extension._quitting,
            running: Boolean(extension._appOwner),
            devices: extension._devicesSection._getMenuItems().map(item => item.label.text),
            labels: extension._indicator.menu._getMenuItems().filter(item => item.label)
                .map(item => item.label.text),
            spawnCount: this._spawnCount, quitCount: this._quitCount,
            preferencesCount: this._preferencesCount, notifications: this._notifications,
        });
    }
    StartTwice() {
        const extension = this._extension();
        extension._notify = message => this._notifications.push(message);
        extension._spawnApplication = () => {
            this._spawnCount++;
            this._startTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 500, () => {
                this._startTimer = 0;
                this._startMock();
                return GLib.SOURCE_REMOVE;
            });
        };
        extension._startApplication();
        extension._startApplication();
    }
    QuitTwice() {
        const extension = this._extension();
        const wasRunning = Boolean(extension._appOwner);
        extension._quitApplication();
        extension._quitApplication();
        if (wasRunning)
            extension._startApplication();
    }
    ShowPreferences() {
        this._extension()._call('ShowPreferences');
    }
    OpenMenu() {
        this._surface?.hide();
        this._extension()._indicator.menu.open();
    }
    CloseMenu() {
        this._extension()._indicator.menu.close();
        this._surface?.show();
    }
    SetBattery(levels, statuses) {
        this._levels = levels;
        this._statuses = statuses;
        this._mockObject.emit_property_changed('ConnectedDeviceBatteryLevels', new GLib.Variant('ai', levels));
        this._mockObject.emit_property_changed('ConnectedDeviceBatteryStatuses', new GLib.Variant('as', statuses));
        // Coalesced refreshes must not overwrite the newer PropertiesChanged data.
        this._extension()._refreshPanel();
        this._extension()._refreshPanel();
    }
    _startMock() {
        const driver = this;
        this._levels = [73, -1];
        this._statuses = ['discharging', ''];
        this._mockBus = Gio.DBusConnection.new_for_address_sync(GLib.getenv('DBUS_SESSION_BUS_ADDRESS'),
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            null, null);
        this._mockBus.set_exit_on_close(false);
        this._mockObject = Gio.DBusExportedObject.wrapJSObject(APP_XML, {
            get ConnectedDevices() { return ['Spotlight USB', 'Spotlight Bluetooth']; },
            get ConnectedDeviceBatteryLevels() { return driver._levels; },
            get ConnectedDeviceBatteryStatuses() { return driver._statuses; },
            get Presets() { return ['Presentation']; },
            ShowPreferences() { driver._preferencesCount++; },
            QuitAsync(_parameters, _invocation) {
                driver._quitCount++;
                // Simulate the real application's connection closing before it
                // can reply. No successful D-Bus response is sent deliberately.
                driver._quitTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 100, () => {
                    driver._quitTimer = 0;
                    driver._stopMock();
                    return GLib.SOURCE_REMOVE;
                });
            },
        });
        this._mockObject.export(this._mockBus, APP_PATH);
        this._mockOwner = Gio.bus_own_name_on_connection(this._mockBus,
            APP_SERVICE, Gio.BusNameOwnerFlags.NONE, null, null);
    }
    _stopMock() {
        if (this._mockOwner)
            Gio.bus_unown_name(this._mockOwner);
        this._mockOwner = 0;
        this._mockObject?.unexport();
        this._mockObject = null;
        this._mockBus?.close_sync(null);
        this._mockBus = null;
    }
    SnapshotAsync([filename], invocation) {
        const stream = Gio.File.new_for_path(filename).replace(null, false, Gio.FileCreateFlags.NONE, null);
        const shot = new Shell.Screenshot();
        shot.screenshot(false, stream, (object, result) => {
            try {
                object.screenshot_finish(result);
                stream.close(null);
                invocation.return_value(null);
            } catch (error) {
                invocation.return_dbus_error('org.projecteur.Smoke.Failed', error.message);
            }
        });
    }
    disable() {
        if (this._startTimer)
            GLib.Source.remove(this._startTimer);
        if (this._quitTimer)
            GLib.Source.remove(this._quitTimer);
        this._stopMock();
        this._surface?.destroy();
        this._surface = null;
        this._pointer = null;
        this._object.unexport();
        Gio.bus_unown_name(this._owner);
    }
}
