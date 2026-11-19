// SPDX-License-Identifier: MIT
// Pure geometry and input validation, shared with the headless regression tests.
export function clamp(value, minimum, maximum) {
    return Math.max(minimum, Math.min(maximum, value));
}

export function parseSettings(json) {
    if (typeof json !== 'string' || json.length > 65536)
        throw new Error('Settings must be a JSON object of at most 64 KiB');
    const input = JSON.parse(json);
    if (!input || Array.isArray(input) || typeof input !== 'object')
        throw new Error('Settings must be an object');
    const number = (key, fallback, min, max) =>
        typeof input[key] === 'number' && Number.isFinite(input[key])
            ? clamp(input[key], min, max) : fallback;
    const boolean = (key, fallback) =>
        typeof input[key] === 'boolean' ? input[key] : fallback;
    const color = (key, fallback) =>
        typeof input[key] === 'string' && /^#[0-9a-f]{6}$/i.test(input[key])
            ? input[key] : fallback;
    return {
        showSpotShade: boolean('showSpotShade', true),
        spotSize: number('spotSize', 30, 5, 100),
        shadeColor: color('shadeColor', '#000000'),
        shadeOpacity: number('shadeOpacity', 0.45, 0, 1),
        showCenterDot: boolean('showCenterDot', false),
        dotSize: number('dotSize', 10, 3, 100),
        dotColor: color('dotColor', '#ff3030'),
        dotOpacity: number('dotOpacity', 1, 0, 1),
        dotMode: input.dotMode === 'diffuse' ? 'diffuse' : 'solid',
        dotTrailEnabled: boolean('dotTrailEnabled', false),
        showBorder: boolean('showBorder', false),
        borderSize: number('borderSize', 3, 0, 100),
        borderColor: color('borderColor', '#ffffff'),
        borderOpacity: number('borderOpacity', 0.8, 0, 1),
        zoomEnabled: boolean('zoomEnabled', false),
        zoomFactor: number('zoomFactor', 2, 1, 20),
        multiScreenOverlayEnabled: boolean('multiScreenOverlayEnabled', false),
        // Qt::BlankCursor is 10. Also accept an explicit boolean for other clients.
        hideCursor: boolean('hideCursor', input.cursor === undefined || input.cursor === 10),
    };
}

export function rgb(color) {
    return [1, 3, 5].map(offset => parseInt(color.slice(offset, offset + 2), 16) / 255);
}

export function monitorAt(monitors, x, y) {
    return monitors.find(m => x >= m.x && y >= m.y &&
        x < m.x + m.width && y < m.y + m.height) ?? null;
}

export function lensGeometry(monitor, x, y, percent, factor) {
    const diameter = Math.min(monitor.width, monitor.height,
        Math.max(50, monitor.height * percent / 100));
    // Keep the lens visible at monitor edges while sampling the pointer position.
    const left = clamp(x - diameter / 2, monitor.x, monitor.x + monitor.width - diameter);
    const top = clamp(y - diameter / 2, monitor.y, monitor.y + monitor.height - diameter);
    // At edges, keep the sampled region on the same monitor too. Otherwise
    // the clone would expose transparent or neighboring-monitor content.
    const sampleX = clamp(x, monitor.x + diameter / (2 * factor),
        monitor.x + monitor.width - diameter / (2 * factor));
    const sampleY = clamp(y, monitor.y + diameter / (2 * factor),
        monitor.y + monitor.height - diameter / (2 * factor));
    return {
        diameter, left, top, sampleX, sampleY,
        centerX: left + diameter / 2,
        centerY: top + diameter / 2,
        cloneX: diameter / 2 - sampleX * factor,
        cloneY: diameter / 2 - sampleY * factor,
    };
}
