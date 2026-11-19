// SPDX-License-Identifier: MIT
import Cairo from 'cairo';
import Clutter from 'gi://Clutter';
import Cogl from 'gi://Cogl';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import St from 'gi://St';

import {Extension, gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';

import {parseSettings, rgb, monitorAt, lensGeometry} from './geometry.js';
import {deviceLabels, serviceMenuState} from './panelModel.js';

const SERVICE = 'org.projecteur.Projecteur';
const CONTROL_PATH = '/org/projecteur/Projecteur/Control';
const OVERLAY_SERVICE = 'org.projecteur.Gnome';
const OVERLAY_PATH = '/org/projecteur/Gnome';
const LEASE_MS = 5000;
const PANEL_PROPERTIES = new Set([
    'ConnectedDevices', 'ConnectedDeviceBatteryLevels',
    'ConnectedDeviceBatteryStatuses', 'Presets',
]);
const INTERFACE = `<node><interface name="org.projecteur.Gnome">
  <method name="Ping"><arg type="s" name="version" direction="out"/></method>
  <method name="SetState">
    <arg type="b" name="active" direction="in"/>
    <arg type="s" name="settingsJson" direction="in"/>
  </method>
</interface></node>`;

// The clipped container is rendered into an offscreen texture, then masked on
// the GPU. No screenshot/readback, screen-capture permission or feedback loop.
function circleMask() {
    const snippet = Cogl.Snippet.new(Cogl.SnippetHook.FRAGMENT, '', `
        vec2 p = cogl_tex_coord0_in.xy * 2.0 - 1.0;
        float edge = max(fwidth(length(p)), 0.001);
        cogl_color_out *= 1.0 - smoothstep(1.0 - edge, 1.0, length(p));
    `);
    // GNOME 51 moved shader effects from Shell.GLSLEffect to Clutter.
    return Clutter.ShaderEffect.new_with_snippet(snippet);
}

export default class ProjecteurExtension extends Extension {
    enable() {
        this._enabled = true;
        this._active = false;
        this._settings = parseSettings('{}');
        this._signals = [];
        this._views = [];
        this._trail = [];
        this._lastJson = '';
        this._appOwner = null;
        this._starting = false;
        this._quitting = false;
        this._properties = {};
        this._propertiesRevision = 0;
        this._refreshRequest = null;
        this._refreshAgain = false;
        this._cursorTracker = global.backend.get_cursor_tracker();
        this._cancellable = new Gio.Cancellable();
        this._connect(Main.layoutManager, 'monitors-changed', () => this._rebuild());
        this._connect(Main.sessionMode, 'updated', () => {
            if (Main.sessionMode.isLocked || Main.sessionMode.isGreeter)
                this._setActive(false);
        });
        this._connect(Main.overview, 'showing', () => this._setActive(false));
        this._createPanel();
        this._appWatch = Gio.bus_watch_name(Gio.BusType.SESSION, SERVICE,
            Gio.BusNameWatcherFlags.NONE,
            (_connection, _name, owner) => this._setAppOwner(owner),
            () => this._setAppOwner(null));
        this._propertySubscription = Gio.DBus.session.signal_subscribe(SERVICE,
            'org.freedesktop.DBus.Properties', 'PropertiesChanged', CONTROL_PATH,
            SERVICE, Gio.DBusSignalFlags.NONE,
            (_connection, sender, _path, _iface, _signal, parameters) => {
                if (!this._enabled || sender !== this._appOwner)
                    return;
                const [, changed, invalidated] = parameters.deep_unpack();
                const relevant = Object.keys(changed).some(name => PANEL_PROPERTIES.has(name));
                if (relevant) {
                    this._propertiesRevision++;
                    this._applyProperties(changed);
                }
                if (invalidated.some(name => PANEL_PROPERTIES.has(name)))
                    this._refreshPanel();
            });
        this._export = Gio.DBusExportedObject.wrapJSObject(INTERFACE, this);
        this._export.export(Gio.DBus.session, OVERLAY_PATH);
        this._busOwner = Gio.bus_own_name_on_connection(Gio.DBus.session,
            OVERLAY_SERVICE, Gio.BusNameOwnerFlags.NONE, null,
            () => this._setActive(false));
        this._rebuild();
    }

    Ping() {
        return '1';
    }

    SetStateAsync([active, settingsJson], invocation) {
        try {
            const settings = parseSettings(settingsJson);
            const changed = settingsJson !== this._lastJson;
            this._lastJson = settingsJson;
            this._settings = settings;
            this._lastUpdate = GLib.get_monotonic_time();
            this._setActive(active && !Main.sessionMode.isLocked &&
                !Main.sessionMode.isGreeter && !Main.overview.visible);
            if (changed && this._active)
                this._updatePosition();
            invocation.return_value(null);
        } catch (error) {
            invocation.return_dbus_error('org.projecteur.Gnome.InvalidSettings', error.message);
        }
    }

    _connect(object, signal, callback) {
        this._signals.push([object, object.connect(signal, callback)]);
    }

    _createPanel() {
        this._indicator = new PanelMenu.Button(0.0, 'Projecteur');
        this._indicator.add_child(new St.Icon({
            icon_name: 'find-location-symbolic', style_class: 'system-status-icon',
        }));
        this._startItem = this._indicator.menu.addAction(_('Start Projecteur'),
            () => this._startApplication());
        this._devicesSection = new PopupMenu.PopupMenuSection();
        this._indicator.menu.addMenuItem(this._devicesSection);
        this._indicator.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._applicationItems = [
            this._indicator.menu.addAction('Spotlight', () => this._commands([
                'shade=on', 'dot=off', 'zoom=off', 'spot.shape=Circle',
            ])),
            this._indicator.menu.addAction('Pointer', () => this._commands([
                'shade=off', 'dot=on', 'zoom=off', 'border=off', 'dot.size=12',
            ])),
            this._indicator.menu.addAction(_('Magnifier'), () => this._commands([
                'shade=off', 'dot=off', 'zoom=on', 'border=on', 'spot.shape=Circle',
            ])),
        ];
        this._indicator.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._presetsMenu = new PopupMenu.PopupSubMenuMenuItem(_('Presets'));
        this._indicator.menu.addMenuItem(this._presetsMenu);
        this._applicationItems.push(
            this._presetsMenu,
            this._indicator.menu.addAction(_('Preferences…'), () => this._call('ShowPreferences')),
            this._indicator.menu.addAction(_('Start Presentation Timer'), () => this._call('StartTimer')));
        this._quitItem = this._indicator.menu.addAction(_('Stop Projecteur'), () => this._quitApplication());
        this._connect(this._indicator.menu, 'open-state-changed', (_menu, open) => {
            if (open)
                this._refreshPanel();
        });
        Main.panel.addToStatusArea(this.uuid, this._indicator);
        this._updatePanelState();
    }

    _updatePanelState() {
        const state = serviceMenuState(Boolean(this._appOwner), this._starting, this._quitting, _);
        this._startItem.label.text = state.startLabel;
        this._startItem.setSensitive(state.startEnabled);
        this._quitItem.setSensitive(state.quitEnabled);
        this._applicationItems.forEach(item => item.setSensitive(state.actionsEnabled));
        this._devicesSection.actor.visible = Boolean(this._appOwner);
    }

    _clearOperationTimers() {
        for (const name of ['_startTimer', '_quitTimer']) {
            if (this[name])
                GLib.Source.remove(this[name]);
            this[name] = 0;
        }
    }

    _setAppOwner(owner) {
        if (!this._enabled || this._appOwner === owner)
            return;
        this._refreshRequest?.cancellable.cancel();
        this._refreshRequest = null;
        this._refreshAgain = false;
        this._appOwner = owner;
        this._properties = {};
        this._propertiesRevision++;
        this._starting = false;
        this._quitting = false;
        this._clearOperationTimers();
        this._renderProperties();
        this._updatePanelState();
        if (owner)
            this._refreshPanel();
        else
            this._setActive(false);
    }

    _notify(message) {
        Main.notify('Projecteur', message);
    }

    _spawnApplication() {
        // Unlike a normal launcher invocation, this must never open preferences.
        Gio.Subprocess.new(['projecteur', '--background'], Gio.SubprocessFlags.NONE);
    }

    _startApplication() {
        if (!this._enabled || this._appOwner || this._starting || this._quitting)
            return;
        this._starting = true;
        this._updatePanelState();
        this._startTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 8000, () => {
            this._startTimer = 0;
            this._starting = false;
            this._updatePanelState();
            if (!this._appOwner)
                this._notify(_('Could not start Projecteur.'));
            return GLib.SOURCE_REMOVE;
        });
        try {
            this._spawnApplication();
        } catch (_error) {
            this._clearOperationTimers();
            this._starting = false;
            this._updatePanelState();
            this._notify(_('Could not start Projecteur.'));
        }
    }

    _quitApplication() {
        if (!this._enabled || !this._appOwner || this._quitting)
            return;
        this._quitting = true;
        this._setActive(false);
        this._updatePanelState();
        this._quitTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 5000, () => {
            this._quitTimer = 0;
            this._quitting = false;
            this._updatePanelState();
            return GLib.SOURCE_REMOVE;
        });
        // QCoreApplication may close its bus before replying. That is normal
        // for Quit; the name-owner watcher completes the transition.
        this._call('Quit');
    }

    _commands(commands) {
        this._call('ApplyCommands', new GLib.Variant('(as)', [commands]));
    }

    _call(method, parameters = null) {
        const owner = this._appOwner;
        if (!this._enabled || !owner || (this._quitting && method !== 'Quit'))
            return;
        const cancellable = this._cancellable;
        // Address the unique owner: an in-flight click must never target a new
        // application instance that happens to acquire the well-known name.
        Gio.DBus.session.call(owner, CONTROL_PATH, SERVICE, method, parameters,
            null, Gio.DBusCallFlags.NO_AUTO_START, 2000, cancellable,
            (connection, result) => {
                try {
                    connection.call_finish(result);
                } catch (error) {
                    if (!this._enabled || this._cancellable !== cancellable ||
                        this._appOwner !== owner || method === 'Quit' || this._quitting)
                        return;
                    const remoteName = Gio.DBusError.get_remote_error(error);
                    if (error.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED) ||
                        ['org.freedesktop.DBus.Error.NameHasNoOwner',
                            'org.freedesktop.DBus.Error.ServiceUnknown',
                            'org.freedesktop.DBus.Error.NoReply',
                            'org.freedesktop.DBus.Error.Disconnected'].includes(remoteName))
                        return;
                    this._notify(_('Could not complete the action. Restart Projecteur and try again.'));
                }
            });
    }

    _applyProperties(properties) {
        for (const name of PANEL_PROPERTIES) {
            if (Object.hasOwn(properties, name))
                this._properties[name] = properties[name].deep_unpack();
        }
        this._renderProperties();
    }

    _renderProperties() {
        const properties = this._properties;
        const devices = deviceLabels(properties.ConnectedDevices,
            properties.ConnectedDeviceBatteryLevels, properties.ConnectedDeviceBatteryStatuses, _);
        const deviceKey = JSON.stringify(devices);
        if (deviceKey !== this._deviceKey) {
            this._deviceKey = deviceKey;
            this._devicesSection.removeAll();
            for (const text of devices.length ? devices : [_('No Presenter Connected')])
                this._devicesSection.addMenuItem(new PopupMenu.PopupMenuItem(text, {reactive: false}));
        }
        const presets = properties.Presets ?? [];
        const presetKey = JSON.stringify(presets);
        if (presetKey !== this._presetKey) {
            this._presetKey = presetKey;
            this._presetsMenu.menu.removeAll();
            for (const preset of presets) {
                this._presetsMenu.menu.addAction(preset, () =>
                    this._call('LoadPreset', new GLib.Variant('(s)', [preset])));
            }
        }
    }

    _refreshPanel() {
        const owner = this._appOwner;
        if (!this._enabled || !owner || this._quitting)
            return;
        if (this._refreshRequest) {
            this._refreshAgain = true;
            return;
        }
        const request = {owner, revision: this._propertiesRevision, cancellable: new Gio.Cancellable()};
        this._refreshRequest = request;
        Gio.DBus.session.call(owner, CONTROL_PATH, 'org.freedesktop.DBus.Properties',
            'GetAll', new GLib.Variant('(s)', [SERVICE]), null,
            Gio.DBusCallFlags.NO_AUTO_START, 2000, request.cancellable,
            (connection, result) => {
                let properties;
                try {
                    [properties] = connection.call_finish(result).deep_unpack();
                } catch (_error) {
                    // Owner loss and cancellation are expected during shutdown.
                }
                if (!this._enabled || this._refreshRequest !== request || this._appOwner !== owner)
                    return;
                this._refreshRequest = null;
                const stale = request.revision !== this._propertiesRevision;
                if (properties && !stale)
                    this._applyProperties(properties);
                const refreshAgain = this._refreshAgain || (Boolean(properties) && stale);
                this._refreshAgain = false;
                if (refreshAgain)
                    this._refreshPanel();
            });
    }

    _rebuild() {
        this._views.forEach(view => view.actor.destroy());
        this._views = [];
        for (const monitor of Main.layoutManager.monitors) {
            const actor = new Clutter.Actor({
                x: monitor.x, y: monitor.y, width: monitor.width, height: monitor.height,
                reactive: false, clip_to_allocation: true, visible: false,
            });
            // Keep the entire overlay outside the cloned scene, as GNOME's own
            // magnifier does. Hidden-from-pick preserves click and keyboard focus.
            global.stage.add_child(actor);
            Shell.util_set_hidden_from_pick(actor, true);
            const shade = new Clutter.Actor({width: monitor.width, height: monitor.height});
            const rectangles = Array.from({length: 4}, () => new St.Widget());
            rectangles.forEach(rect => shade.add_child(rect));
            const corners = new St.DrawingArea();
            shade.add_child(corners);
            actor.add_child(shade);
            const lens = new Clutter.Actor({clip_to_allocation: true, reactive: false});
            const clone = new Clutter.Clone({source: Main.uiGroup, reactive: false});
            lens.add_child(clone);
            lens.add_effect(circleMask());
            actor.add_child(lens);
            const markings = new St.DrawingArea({width: monitor.width, height: monitor.height});
            actor.add_child(markings);
            const view = {actor, shade, rectangles, corners, lens, clone, markings,
                monitor, current: false, shadeKey: ''};
            corners.connect('repaint', () => this._draw(view, false));
            markings.connect('repaint', () => this._draw(view, true));
            this._views.push(view);
        }
        if (this._active)
            this._updatePosition();
    }

    _setActive(active) {
        if (!this._enabled && active)
            return;
        if (this._active === active)
            return;
        this._active = active;
        if (active) {
            global.compositor.disable_unredirect();
            this._unredirectInhibited = true;
            this._pointerSignal = this._cursorTracker.connect('position-invalidated',
                () => this._queuePosition());
            this._leaseTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 1000, () => {
                if (GLib.get_monotonic_time() - this._lastUpdate > LEASE_MS * 1000) {
                    this._leaseTimer = 0;
                    this._setActive(false);
                    return GLib.SOURCE_REMOVE;
                }
                return GLib.SOURCE_CONTINUE;
            });
            this._updatePosition();
        } else {
            this._views.forEach(view => view.actor.hide());
            this._trail = [];
            if (this._pointerSignal)
                this._cursorTracker.disconnect(this._pointerSignal);
            this._pointerSignal = 0;
            if (this._positionLater)
                global.compositor.get_laters().remove(this._positionLater);
            this._positionLater = 0;
            if (this._leaseTimer)
                GLib.Source.remove(this._leaseTimer);
            this._leaseTimer = 0;
            if (this._trailTimer)
                GLib.Source.remove(this._trailTimer);
            this._trailTimer = 0;
            if (this._cursorHidden)
                this._cursorTracker.uninhibit_cursor_visibility();
            this._cursorHidden = false;
            if (this._unredirectInhibited)
                global.compositor.enable_unredirect();
            this._unredirectInhibited = false;
        }
    }

    _queuePosition() {
        if (this._positionLater)
            return;
        this._positionLater = global.compositor.get_laters().add(Meta.LaterType.BEFORE_REDRAW, () => {
            this._positionLater = 0;
            if (this._active)
                this._updatePosition();
            return GLib.SOURCE_REMOVE;
        });
    }

    _updatePosition() {
        const [point] = this._cursorTracker.get_pointer();
        const {x, y} = point;
        const settings = this._settings;
        const monitor = monitorAt(Main.layoutManager.monitors, x, y);
        if (settings.hideCursor && !this._cursorHidden) {
            this._cursorTracker.inhibit_cursor_visibility();
            this._cursorHidden = true;
        } else if (!settings.hideCursor && this._cursorHidden) {
            this._cursorTracker.uninhibit_cursor_visibility();
            this._cursorHidden = false;
        }
        const now = GLib.get_monotonic_time();
        this._trail = this._trail.filter(p => now - p.time < 250000);
        if (settings.dotTrailEnabled && settings.showCenterDot) {
            this._trail.push({x, y, time: now});
            if (!this._trailTimer) {
                this._trailTimer = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 33, () => {
                    this._trail = this._trail.filter(p => GLib.get_monotonic_time() - p.time < 250000);
                    this._views.forEach(v => v.markings.queue_repaint());
                    if (!this._trail.length) {
                        this._trailTimer = 0;
                        return GLib.SOURCE_REMOVE;
                    }
                    return GLib.SOURCE_CONTINUE;
                });
            }
        }
        for (const view of this._views) {
            view.current = view.monitor === monitor;
            view.actor.visible = view.current || settings.multiScreenOverlayEnabled;
            view.pointer = {x: x - view.monitor.x, y: y - view.monitor.y};
            const geometry = lensGeometry(view.monitor, x, y, settings.spotSize, settings.zoomFactor);
            view.geometry = geometry;
            view.lens.visible = view.current && settings.zoomEnabled;
            view.lens.set_position(geometry.left - view.monitor.x, geometry.top - view.monitor.y);
            view.lens.set_size(geometry.diameter, geometry.diameter);
            view.clone.set_scale(settings.zoomFactor, settings.zoomFactor);
            view.clone.set_position(geometry.cloneX, geometry.cloneY);
            this._placeShade(view);
            this._placeMarkings(view);
            view.markings.queue_repaint();
        }
    }

    _placeShade(view) {
        const s = this._settings;
        view.shade.visible = s.showSpotShade;
        if (!s.showSpotShade)
            return;
        const {width, height} = view.monitor;
        const radius = view.geometry.diameter / 2;
        const cx = s.zoomEnabled ? view.geometry.centerX - view.monitor.x : view.pointer.x;
        const cy = s.zoomEnabled ? view.geometry.centerY - view.monitor.y : view.pointer.y;
        const left = Math.max(0, Math.min(width, cx - radius));
        const right = Math.max(0, Math.min(width, cx + radius));
        const top = Math.max(0, Math.min(height, cy - radius));
        const bottom = Math.max(0, Math.min(height, cy + radius));
        const regions = view.current ? [
            [0, 0, width, top], [0, bottom, width, height - bottom],
            [0, top, left, bottom - top], [right, top, width - right, bottom - top],
        ] : [[0, 0, width, height], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]];
        const key = `${s.shadeColor}:${s.shadeOpacity}:${radius}`;
        const color = rgb(s.shadeColor).map(c => Math.round(c * 255)).join(',');
        view.rectangles.forEach((rect, index) => {
            const [x, y, w, h] = regions[index];
            rect.set_position(x, y);
            rect.set_size(w, h);
            if (view.shadeKey !== key)
                rect.set_style(`background-color: rgba(${color},${s.shadeOpacity});`);
        });
        view.corners.visible = view.current;
        view.corners.set_position(cx - radius, cy - radius);
        view.corners.set_size(radius * 2, radius * 2);
        // Only the small circle corners are rasterized. Moving the spotlight
        // repositions existing textures rather than redrawing the whole monitor.
        if (view.shadeKey !== key)
            view.corners.queue_repaint();
        view.shadeKey = key;
    }

    _placeMarkings(view) {
        const s = this._settings;
        view.markings.visible = view.current && (s.showBorder || s.showCenterDot);
        if (!view.markings.visible)
            return;
        const radius = view.geometry.diameter / 2;
        const cx = s.zoomEnabled ? view.geometry.centerX - view.monitor.x : view.pointer.x;
        const cy = s.zoomEnabled ? view.geometry.centerY - view.monitor.y : view.pointer.y;
        const extent = s.showBorder ? radius + 2 : s.dotSize + 2;
        let left = Math.min(cx - extent, view.pointer.x - s.dotSize - 2);
        let right = Math.max(cx + extent, view.pointer.x + s.dotSize + 2);
        let top = Math.min(cy - extent, view.pointer.y - s.dotSize - 2);
        let bottom = Math.max(cy + extent, view.pointer.y + s.dotSize + 2);
        for (const point of s.dotTrailEnabled ? this._trail : []) {
            left = Math.min(left, point.x - view.monitor.x - s.dotSize - 2);
            right = Math.max(right, point.x - view.monitor.x + s.dotSize + 2);
            top = Math.min(top, point.y - view.monitor.y - s.dotSize - 2);
            bottom = Math.max(bottom, point.y - view.monitor.y + s.dotSize + 2);
        }
        view.markingBounds = {x: Math.floor(left), y: Math.floor(top)};
        view.markings.set_position(view.markingBounds.x, view.markingBounds.y);
        view.markings.set_size(Math.ceil(right - left), Math.ceil(bottom - top));
    }

    _draw(view, foreground) {
        const area = foreground ? view.markings : view.corners;
        const cr = area.get_context();
        try {
            cr.setOperator(Cairo.Operator.CLEAR);
            cr.paint();
            cr.setOperator(Cairo.Operator.OVER);
            if (!this._active || !view.geometry)
                return;
            const s = this._settings;
            const radius = view.geometry.diameter / 2;
            const cx = s.zoomEnabled ? view.geometry.centerX - view.monitor.x : view.pointer.x;
            const cy = s.zoomEnabled ? view.geometry.centerY - view.monitor.y : view.pointer.y;
            if (!foreground && s.showSpotShade) {
                cr.setSourceRGBA(...rgb(s.shadeColor), s.shadeOpacity);
                cr.rectangle(0, 0, radius * 2, radius * 2);
                cr.newSubPath();
                cr.arc(radius, radius, radius, 0, Math.PI * 2);
                cr.setFillRule(Cairo.FillRule.EVEN_ODD);
                cr.fill();
            }
            if (!foreground || !view.current)
                return;
            cr.translate(-view.markingBounds.x, -view.markingBounds.y);
            if (s.showBorder && s.borderSize > 0) {
                const borderWidth = radius * s.borderSize / 100;
                cr.setSourceRGBA(...rgb(s.borderColor), s.borderOpacity);
                cr.setLineWidth(borderWidth);
                cr.arc(cx, cy, Math.max(1, radius - borderWidth / 2), 0, Math.PI * 2);
                cr.stroke();
            }
            if (s.showCenterDot) {
                if (s.dotTrailEnabled) {
                    const now = GLib.get_monotonic_time();
                    for (const point of this._trail) {
                        const age = 1 - (now - point.time) / 250000;
                        this._drawDot(cr, point.x - view.monitor.x, point.y - view.monitor.y,
                            s.dotSize / 2 * age, s.dotOpacity * age * 0.35);
                    }
                }
                this._drawDot(cr, view.pointer.x, view.pointer.y, s.dotSize / 2, s.dotOpacity);
            }
        } finally {
            cr.$dispose();
        }
    }

    _drawDot(cr, x, y, radius, opacity) {
        if (radius <= 0 || opacity <= 0)
            return;
        const color = rgb(this._settings.dotColor);
        if (this._settings.dotMode === 'diffuse') {
            const gradient = new Cairo.RadialGradient(x, y, 0, x, y, radius * 2);
            gradient.addColorStopRGBA(0, ...color, opacity);
            gradient.addColorStopRGBA(0.4, ...color, opacity * 0.8);
            gradient.addColorStopRGBA(1, ...color, 0);
            cr.setSource(gradient);
            cr.arc(x, y, radius * 2, 0, Math.PI * 2);
        } else {
            cr.setSourceRGBA(...color, opacity);
            cr.arc(x, y, radius, 0, Math.PI * 2);
        }
        cr.fill();
    }

    disable() {
        this._enabled = false;
        this._setActive(false);
        this._cancellable?.cancel();
        this._clearOperationTimers();
        this._refreshRequest?.cancellable.cancel();
        this._refreshRequest = null;
        this._refreshAgain = false;
        this._signals?.forEach(([object, id]) => object.disconnect(id));
        this._signals = [];
        if (this._propertySubscription)
            Gio.DBus.session.signal_unsubscribe(this._propertySubscription);
        this._propertySubscription = 0;
        if (this._appWatch)
            Gio.bus_unwatch_name(this._appWatch);
        this._appWatch = 0;
        if (this._busOwner)
            Gio.bus_unown_name(this._busOwner);
        this._busOwner = 0;
        this._export?.unexport();
        this._export = null;
        this._views?.forEach(view => view.actor.destroy());
        this._views = [];
        this._indicator?.destroy();
        this._indicator = null;
        this._cursorTracker = null;
        this._cancellable = null;
        this._appOwner = null;
        this._starting = false;
        this._quitting = false;
        this._deviceKey = null;
        this._presetKey = null;
        this._applicationItems = [];
        this._devicesSection = null;
        this._presetsMenu = null;
        this._startItem = null;
        this._quitItem = null;
    }
}
