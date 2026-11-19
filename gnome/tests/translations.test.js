// SPDX-License-Identifier: MIT
import Gio from 'gi://Gio';

const [preferencesDir, language] = ARGV;
const {_} = await import(Gio.File.new_for_path(preferencesDir).get_child('i18n.js').get_uri());
const {serviceMenuState, batteryLabel} = await import('../projecteur@projecteur.github.io/panelModel.js');

const actual = [
    _('Devices'),
    _('Start Preview'),
    _('Edit Button Mapping'),
    _('Timer Running'),
    serviceMenuState(false, false, false, _).startLabel,
    batteryLabel(42, 'charging', _),
];
const expected = language === 'de'
    ? ['Geräte', 'Vorschau starten', 'Tastenzuordnung bearbeiten', 'Timer läuft',
        'Projecteur starten', 'Akku 42 % · lädt']
    : ['Devices', 'Start Preview', 'Edit Button Mapping', 'Timer Running',
        'Start Projecteur', 'Battery 42% · charging'];
if (actual.some((label, index) => label !== expected[index]))
    throw new Error(`Unexpected labels for ${language}: ${JSON.stringify(actual)}`);

print(`PASS: Projecteur labels for ${language}`);
