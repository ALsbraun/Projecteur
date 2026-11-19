// SPDX-License-Identifier: MIT
// Display-only helpers: the service's unique D-Bus owner is the source of truth.
const untranslated = text => text;

export function serviceMenuState(running, starting = false, quitting = false, _ = untranslated) {
    return {
        startLabel: quitting ? _('Stopping Projecteur…') : running ? _('Projecteur Is Running')
            : starting ? _('Starting Projecteur…') : _('Start Projecteur'),
        startEnabled: !running && !starting && !quitting,
        quitEnabled: running && !quitting,
        actionsEnabled: running && !quitting,
    };
}

export function batteryLabel(level, status = '', _ = untranslated) {
    const known = typeof level === 'number' && Number.isFinite(level) && level >= 0 && level <= 100;
    const charge = known ? _('Battery %d%%').replace('%d', Math.round(level)).replace('%%', '%') : _('Battery level unknown');
    const labels = {
        charging: _('charging'),
        'slow-charging': _('charging slowly'),
        'almost-full': _('almost full'),
        full: _('fully charged'),
        'invalid-battery': _('battery error'),
        'thermal-error': _('temperature error'),
        'charging-error': _('charging error'),
    };
    return Object.hasOwn(labels, status) ? `${charge} · ${labels[status]}` : charge;
}

export function deviceLabels(names = [], levels = [], statuses = [], _ = untranslated) {
    return names.map((name, index) => `${name || 'Presenter'} — ${batteryLabel(levels[index], statuses[index], _)}`);
}
