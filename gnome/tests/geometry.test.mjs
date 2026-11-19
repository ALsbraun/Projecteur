// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
// Works on Node 18+, without changing the repository's module type.
const source = readFileSync(new URL('../projecteur@projecteur.github.io/geometry.js', import.meta.url));
const {parseSettings, rgb, monitorAt, lensGeometry} = await import(`data:text/javascript;base64,${source.toString('base64')}`);

const monitors = [
    {x: -1280, y: 0, width: 1280, height: 1024},
    {x: 0, y: 0, width: 2048, height: 1152},
    {x: 2048, y: -600, width: 1280, height: 720},
];
assert.equal(monitorAt(monitors, -0.25, 50), monitors[0]);
assert.equal(monitorAt(monitors, 0, 50), monitors[1]);
assert.equal(monitorAt(monitors, 2048, 50), monitors[2]);
assert.equal(monitorAt(monitors, 2048, 500), null);
assert.equal(monitorAt([], 0, 0), null);
for (const monitor of monitors) {
    for (const [x, y] of [[monitor.x, monitor.y],
        [monitor.x + monitor.width - 0.5, monitor.y + monitor.height - 0.5],
        [monitor.x + 150.25, monitor.y + 200.75]]) {
        const g = lensGeometry(monitor, x, y, 30, 2.5);
        assert.ok(g.left >= monitor.x && g.top >= monitor.y - 1e-9);
        assert.ok(g.left + g.diameter <= monitor.x + monitor.width + 1e-9);
        assert.ok(g.top + g.diameter <= monitor.y + monitor.height + 1e-9);
        // The selected source position lands exactly at the lens center,
        // including fractional coordinates and monitors left of the origin.
        assert.ok(Math.abs(g.cloneX + g.sampleX * 2.5 - g.diameter / 2) < 1e-9);
        assert.ok(Math.abs(g.cloneY + g.sampleY * 2.5 - g.diameter / 2) < 1e-9);
        assert.ok(g.sampleX - g.diameter / 5 >= monitor.x - 1e-9);
        assert.ok(g.sampleX + g.diameter / 5 <= monitor.x + monitor.width + 1e-9);
        assert.ok(g.sampleY - g.diameter / 5 >= monitor.y - 1e-9);
        assert.ok(g.sampleY + g.diameter / 5 <= monitor.y + monitor.height + 1e-9);
    }
}
assert.equal(lensGeometry({x: 0, y: 0, width: 30, height: 40}, 0, 0, 100, 2).diameter, 30);
const settings = parseSettings('{"zoomFactor":1e300,"spotSize":-10,"shadeOpacity":2,"dotSize":"100000","hideCursor":false,"shadeColor":"url(bad)"}');
assert.equal(settings.zoomFactor, 20);
assert.equal(settings.spotSize, 5);
assert.equal(settings.shadeOpacity, 1);
assert.equal(settings.dotSize, 10);
assert.equal(settings.hideCursor, false);
assert.equal(settings.shadeColor, '#000000');
assert.equal(parseSettings('{"cursor":0}').hideCursor, false);
assert.equal(parseSettings('{"cursor":10}').hideCursor, true);
assert.equal(parseSettings('{"zoomFactor":1e400}').zoomFactor, 2);
for (const invalid of ['[]', 'null', 'true', '"text"', '{', ' '.repeat(65537)])
    assert.throws(() => parseSettings(invalid));
assert.deepEqual(rgb('#ff0080'), [1, 0, 128 / 255]);
console.log('GNOME geometry/settings: all regression checks passed');
