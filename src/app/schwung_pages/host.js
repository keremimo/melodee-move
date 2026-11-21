// SPDX-License-Identifier: AGPL-3.0-only
// A Schwung module's own pages and menus, drawn by Schwung's knob-page code
// (its shared/param_pages, MIT, Charles Vestal and contributors) as Schwung's
// shadow UI draws them, inside a page Melodee hosts (app/canvas_page.h).
//
// Nothing of Schwung's is copied here: its library is imported from the
// Schwung installed beside Melodee, so a module is drawn by the very code it
// was written against. This file is only the seam Schwung's own consumer
// (src/shadow/shadow_ui_param_pages.mjs) is for the shadow UI: parameter
// reads and writes, visible_if, the module's drawn pages, the clock and the
// Move's controls in.
//
// Melodee answers ctx.getParam("melodee:module_dir") and "melodee:trailing",
// takes ctx.setParam("melodee:described") (where the controller is) and
// "melodee:action" (a trailing menu's entry pushed); every other key is the
// module's (Schwung's "synth:" prefix taken off). MELODEE_SCHWUNG_SHARED
// names Schwung's shared folder where it is not the Move's.
import * as std from "std";

const PREFIX = "synth";
const TICK_MS = 1000 / 44; // Schwung's shadow UI ticks once a block of 128 frames, about 44 times a second
const MAX_TICKS_PER_DRAW = 8;

let ctx = null;          // the ctx of the hook running now: Melodee's drawing and parameters
let controller = null;
let lib = null;          // Schwung's modules, once imported
let moduleDir = "";
let lastTick = 0;
let shift = false;
const stats = { ticks: 0, tickMs: 0, renders: 0, renderMs: 0, slowestRender: 0, slowestTick: 0, reads: 0, writes: 0 };

// Schwung's device globals, over whichever ctx is drawing: its menus draw
// through them when handed no ctx of their own.
globalThis.fill_rect = (x, y, w, h, c) => ctx && ctx.fillRect(x, y, w, h, c);
globalThis.set_pixel = (x, y, c) => ctx && ctx.setPixel(x, y, c);
globalThis.draw_line = (x0, y0, x1, y1, c) => ctx && ctx.line(x0, y0, x1, y1, c);
globalThis.print = (x, y, text, c) => ctx ? ctx.print(x, y, String(text), c) : 0;
globalThis.text_width = (text) => ctx ? ctx.textWidth(String(text)) : 0;
globalThis.clear_screen = () => ctx && ctx.fillRect(0, 0, 128, 64, 0);
globalThis.fill_circle = (cx, cy, r, c) => {
    for (let dy = -r; dy <= r; dy++) {
        const dx = Math.floor(Math.sqrt(r * r - dy * dy));
        ctx && ctx.fillRect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
};

function bare(key) {
    const k = String(key);
    return k.startsWith(PREFIX + ":") ? k.slice(PREFIX.length + 1) : k;
}

// Three answers, as Schwung's param channel gives them: text, "" (served,
// nothing), null (no answer). Melodee's read answers text or "".
function getParam(key) {
    stats.reads++;
    if (!ctx) return null;
    const answer = ctx.getParam(bare(key));
    return answer === undefined ? null : answer;
}
function setParam(key, value) {
    stats.writes++;
    if (ctx) ctx.setParam(bare(key), String(value));
    return true;
}

// visible_if, as shadow_ui.js's evaluateVisibilityCondition: against the
// module's value now, failing open when there is none.
function same(actual, expected) {
    if (typeof expected === "boolean") return ["1", "true", "on", "yes"].includes(String(actual).toLowerCase()) === expected;
    if (typeof expected === "number") { const n = Number(actual); return Number.isFinite(n) && n === expected; }
    return String(actual) === String(expected);
}
function visible(cond) {
    if (!cond || typeof cond !== "object") return true;
    const param = cond.param || cond.key || cond.param_key;
    if (!param) return true;
    const raw = getParam(PREFIX + ":" + param);
    if (raw === null || raw === undefined || raw === "") return true;
    if (cond.equals !== undefined) return same(raw, cond.equals);
    if (cond.not_equals !== undefined) return !same(raw, cond.not_equals);
    const gt = cond.gt ?? cond.greater_than ?? cond.greater, lt = cond.lt ?? cond.smaller_than ?? cond.smaller;
    if (gt !== undefined) return Number(raw) > Number(gt);
    if (lt !== undefined) return Number(raw) < Number(lt);
    if (cond.truthy !== undefined) return same(raw, true) === !!cond.truthy || (Number(raw) !== 0) === !!cond.truthy;
    if (cond.falsey !== undefined || cond.falsy !== undefined) return Number(raw) === 0 || !same(raw, true);
    return true;
}

// The module's drawn pages: its canvas script, loaded once into this
// context the way Schwung loads it. A plain script (Schimera's) runs at once,
// canvas_overlay saved and put back; a module (one that imports std, as Radio
// Garden's and Webstream's do) is imported, and is there from the next frame
// (Melodee runs the script's pending jobs after each hook).
const overlays = {};
const pageStates = {};
function overlayFor(canvas) {
    const path = moduleDir + "/" + (canvas.script || "canvas.js");
    if (path in overlays) return overlays[path];
    const ours = globalThis.canvas_overlay;
    overlays[path] = null;
    const source = std.loadFile(path);
    if (source === null) { console.log("schwung pages: no " + path); return null; }
    if (/^\s*(import|export)\b/m.test(source)) {
        delete globalThis.canvas_overlay;
        import(path).then(() => {
            overlays[path] = globalThis.canvas_overlay || null;
            globalThis.canvas_overlay = ours;
        }, (e) => {
            console.log("schwung pages: " + path + ": " + e);
            globalThis.canvas_overlay = ours;
        });
        return null;
    }
    try {
        delete globalThis.canvas_overlay;
        std.evalScript(source);
        overlays[path] = globalThis.canvas_overlay || null;
    } catch (e) {
        console.log("schwung pages: " + path + ": " + e);
    }
    globalThis.canvas_overlay = ours;
    return overlays[path];
}
function drawCanvasPage(drawCtx, band, canvas, payload) {
    const ov = overlayFor(canvas);
    if (!ov || typeof ov.drawPage !== "function") return;
    const state = pageStates[canvas.key] || (pageStates[canvas.key] = {});
    try {
        ov.drawPage(lib.frameCtx(drawCtx, band), {
            key: canvas.key,
            values: (payload && payload.values) || {},
            base: (payload && payload.base) || {},
            keys: (payload && payload.keys) || [],
            touched: payload && typeof payload.touched === "number" ? payload.touched : -1,
            preset: (payload && payload.preset) || null,
            nowMs: payload && typeof payload.nowMs === "number" ? payload.nowMs : Date.now(),
            width: band.w, height: band.h, state,
        });
    } catch (e) {
        console.log("schwung pages: drawPage of " + canvas.key + ": " + e);
    }
}
function canvasPageHook(canvas, hook, payload) {
    const ov = overlayFor(canvas);
    if (!ov || typeof ov[hook] !== "function") return undefined;
    const state = pageStates[canvas.key] || (pageStates[canvas.key] = {});
    const pageCtx = {
        width: 128, height: 64, state,
        getParam: (k) => getParam(PREFIX + ":" + k),
        setParam: (k, v) => setParam(PREFIX + ":" + k, v),
        getValue: () => getParam(PREFIX + ":" + canvas.key) || "",
        openTextEntry: (o) => ctx && ctx.openTextEntry(o),
    };
    try { return ov[hook](pageCtx, payload); } catch (e) { console.log("schwung pages: " + hook + ": " + e); return undefined; }
}

// Schwung's library, imported while Melodee loads this page (it waits for
// the imports); the controller is made on the first hook, when the module's
// parameters answer.
const SHARED = std.getenv("MELODEE_SCHWUNG_SHARED") || "/data/UserData/schwung/shared";
{
    const [pc, input, frame] = await Promise.all([
        import(SHARED + "/param_pages/page_controller.mjs"),
        import(SHARED + "/param_pages/page_input.mjs"),
        import(SHARED + "/param_pages/frame_ctx.mjs"),
    ]);
    lib = { ...pc, ...input, ...frame };
}

function ensure() {
    if (controller) return;
    moduleDir = ctx.getParam("melodee:module_dir");
    controller = lib.createController({
        getParam, setParam,
        announce: () => {},
        now: () => Date.now(),
        drawCanvasPage, canvasPageHook,
        trailingMenus,
    });
    controller.load({ slot: 0, component: PREFIX, prefix: PREFIX, visible, paginate: true });
    controller.setLayout(lib.LAYOUT_MOVY || "movy");
    lastTick = Date.now();
}

// Pages Melodee adds after the module's, as Schwung adds My Presets and
// Module: what Melodee answers for "melodee:trailing" (JSON, the same shape).
// An entry's action goes back to Melodee as "melodee:action".
function trailingMenus() {
    try { return JSON.parse(ctx.getParam("melodee:trailing") || "[]"); } catch (e) { return []; }
}

function tick() {
    const now = Date.now();
    let n = Math.min(MAX_TICKS_PER_DRAW, Math.max(1, Math.floor((now - lastTick) / TICK_MS)));
    lastTick = now;
    while (n-- > 0) {
        const t0 = Date.now();
        controller.tick();
        const took = Date.now() - t0;
        stats.ticks++;
        stats.tickMs += took;
        stats.slowestTick = Math.max(stats.slowestTick, took);
    }
}

// What the probe and Melodee read back: where the controller is.
function menuEntered() {
    if (!controller) return false;
    return !!(typeof controller.menuEntered === "function" ? controller.menuEntered() : controller.menuEntered);
}
let exited = false; // Back at the top: Melodee's to take
function describe() {
    const page = controller && controller.page;
    return JSON.stringify({
        index: controller ? controller.pageIndex : 0, count: controller ? (controller.pages || []).length : 0,
        wheel: menuEntered() || !!(controller && controller.pickerOpen), exit: exited,
        page: page ? page.name : null, kind: page ? page.kind : null, keys: page ? page.keys : [],
        pages: controller ? (controller.pages || []).map((p) => p.name) : [],
        menu: menuEntered(),
        unresolved: controller ? !!controller.contractUnresolved : false,
        stats,
    });
}

// A throw in a hook ends the page for good (Melodee's rule, as Schwung's):
// say where, once, and carry on.
let reported = 0;
function guarded(name, f) {
    return (...args) => {
        try { return f(...args); } catch (e) {
            if (reported++ < 5) console.log("schwung pages: " + name + ": " + e + "\n" + (e && e.stack));
            return undefined;
        }
    };
}

globalThis.canvas_overlay = {
    drawPage(c, info) {
        ctx = c;
        ensure();
        tick();
        const t0 = Date.now();
        controller.render(
            { fillRect: c.fillRect, print: (x, y, s, col) => c.print(x, y, String(s), col), textWidth: (s) => c.textWidth(String(s)),
              line: c.line, fillCircle: globalThis.fill_circle },
            { title: (info && info.title) || "", footer: [["JOG", "PG"], ["SHFT", "SECT"], ["CLK", "MENU"]] });
        controller.renderOverlays({ fillRect: c.fillRect, print: (x, y, s, col) => c.print(x, y, String(s), col), textWidth: (s) => c.textWidth(String(s)) },
                                  { clearScreen: globalThis.clear_screen });
        const took = Date.now() - t0;
        stats.renders++;
        stats.renderMs += took;
        stats.slowestRender = Math.max(stats.slowestRender, took);
        c.setParam("melodee:described", describe());
    },
    // The Move's controls, raw: knobs (CC 71-78) and their touches (notes
    // 0-7), the jogwheel (CC 14, its push CC 3), Back (CC 51), Shift (CC 49).
    onMidi(c, msg) {
        ctx = c;
        ensure();
        if (!msg || !msg.data) return;
        const data = msg.data;
        if ((data[0] & 0xf0) === 0xb0 && data[1] === 49) { shift = data[2] > 0; return; }
        const intent = lib.decodeInput(data, { shift, mute: false });
        if (!intent) return;
        const todo = lib.applyInput(controller, intent, { nowMs: Date.now(), reveal: false });
        exited = !!(todo && todo.action === "exit");
        if (todo && todo.action === "menu" && todo.entry && todo.entry.action) c.setParam("melodee:action", String(todo.entry.action));
        if (todo && todo.action === "open" && todo.meta && todo.meta.type === "string") {
            const key = todo.key || bare(todo.fullKey || "");
            pendingText = key;
            c.openTextEntry({ title: todo.meta.label || todo.meta.name || key, initial: getParam(PREFIX + ":" + key) || "" });
        }
        c.setParam("melodee:described", describe());
    },
    onTextEntry(c, answer) {
        ctx = c;
        if (pendingText && answer && !answer.cancelled) setParam(PREFIX + ":" + pendingText, answer.text);
        pendingText = null;
    },
    handleBack(c) {
        ctx = c;
        if (menuEntered()) { controller.exitMenu(); return true; }
        return false;
    },
};
let pendingText = null;
for (const name of ["drawPage", "onMidi", "onTextEntry", "handleBack"])
    globalThis.canvas_overlay[name] = guarded(name, globalThis.canvas_overlay[name]);
