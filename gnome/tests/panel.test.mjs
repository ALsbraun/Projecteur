// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const source = readFileSync(new URL('../projecteur@projecteur.github.io/panelModel.js', import.meta.url));
const {serviceMenuState, batteryLabel, deviceLabels} = await import(`data:text/javascript;base64,${source.toString('base64')}`);
assert.deepEqual(serviceMenuState(false), {
    startLabel: 'Start Projecteur', startEnabled: true, quitEnabled: false, actionsEnabled: false,
});
assert.deepEqual(serviceMenuState(true), {
    startLabel: 'Projecteur Is Running', startEnabled: false, quitEnabled: true, actionsEnabled: true,
});
for (const state of [serviceMenuState(false, true), serviceMenuState(true, false, true)]) {
    assert.equal(state.startEnabled, false);
    assert.equal(state.quitEnabled, false);
    assert.equal(state.actionsEnabled, false);
}
assert.equal(batteryLabel(0), 'Battery 0%');
assert.equal(batteryLabel(100, 'full'), 'Battery 100% · fully charged');
assert.equal(batteryLabel(67, 'charging'), 'Battery 67% · charging');
assert.equal(batteryLabel(20, 'slow-charging'), 'Battery 20% · charging slowly');
assert.equal(batteryLabel(-1, 'charging'), 'Battery level unknown · charging');
assert.equal(batteryLabel(40, 'thermal-error'), 'Battery 40% · temperature error');
for (const value of [undefined, null, -1, 255, NaN, Infinity, '80'])
    assert.equal(batteryLabel(value), 'Battery level unknown');
assert.equal(batteryLabel(30, 'unrecognized-status'), 'Battery 30%');
assert.equal(batteryLabel(30, 'toString'), 'Battery 30%');
assert.deepEqual(deviceLabels(['Spotlight USB', 'Spotlight BT'], [75, -1], ['charging']), [
    'Spotlight USB — Battery 75% · charging', 'Spotlight BT — Battery level unknown',
]);
assert.deepEqual(deviceLabels(), []);
console.log('GNOME menu state and battery labels: all regression checks passed');
