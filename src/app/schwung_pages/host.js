// SPDX-License-Identifier: AGPL-3.0-only
// A Schwung module's pages and menus as Schwung's own knob-page code decides
// them (its shared/param_pages, MIT, Charles Vestal and contributors), for
// Melodee to draw in its own look (src/app/schwung_pages.h).
//
// Schwung's page controller is the model: the level walk, visible_if, the
// staggered reads, doors, the Sections picker, the preset browser, Back one
// layer at a time, the enum peek. Melodee is the view: this file draws
// nothing of Schwung's. Each frame it publishes the page as data, from the
// controller's describePage() ("the third way to use this library"), plus
// what that leaves out, and the footer verbs Schwung's shadow UI shows
// (src/shadow/shadow_ui_param_pages.mjs footerHints(), ported here: it is the
// one piece that lives only in Schwung's host).
//
// Nothing of Schwung's is copied: its library is imported from the Schwung
// installed beside Melodee, so a module behaves as under the very code it was
// written against. MELODEE_SCHWUNG_SHARED names that folder elsewhere (tests).
//
// The seam, through the page ctx Melodee lends each hook:
//   getParam("melodee:module_dir")   where the module's own page scripts are
//   getParam("melodee:trailing")     the pages Melodee adds after the module's (JSON)
//   getParam("melodee:page_states")  the drawn pages' kept state, at start (JSON)
//   getParam("melodee:fresh")        once, after the pages are entered: start over
//                                    ({} or {"page", "enter"}: land there by name)
//   setParam("melodee:view", json)   the page as data, after every hook
//   setParam("melodee:action", id)   a trailing page's entry pushed
//   setParam("melodee:page_states", json) a drawn page's state changed
//   setParam("melodee:say", text)    a word for Melodee's message line
// and every other key is the module's (Schwung's "synth:" prefix taken off).
import * as std from "std";

const PREFIX = "synth";
const TICK_MS = 1000 / 44; // Schwung's shadow UI ticks once a block of 128 frames
const MAX_TICKS_PER_FRAME = 8;
const LOADING_POLL_TICKS = 8, LOADING_POLL_MAX_TICKS = 160; // as shadow_ui_param_pages.mjs

let ctx = null;          // the ctx of the hook running now
let controller = null;
let moduleDir = "";
let lastTick = 0;
let shift = false;
let exited = false;      // Back at the top: Melodee's to take
let pendingText = null;  // a text parameter whose keyboard is open
let textForPage = null;  // a drawn page whose keyboard is open
let enumPicker = null;   // {key, title, options, index}
let opened = null;       // a canvas parameter opened from its cell: {key, script, enterable}
let loadingPoll = 0, loadingInterval = LOADING_POLL_TICKS, wasLoading = false;

// Schwung's device globals, for the few shared helpers that reach for them.
globalThis.fill_rect = (x, y, w, h, c) => ctx && ctx.fillRect(x, y, w, h, c);
globalThis.set_pixel = (x, y, c) => ctx && ctx.setPixel(x, y, c);
globalThis.draw_line = (x0, y0, x1, y1, c) => ctx && ctx.line(x0, y0, x1, y1, c);
globalThis.print = (x, y, text, c) => ctx ? ctx.print(x, y, String(text), c) : 0;
globalThis.text_width = (text) => ctx ? ctx.textWidth(String(text)) : 0;
globalThis.clear_screen = () => ctx && ctx.fillRect(0, 0, 128, 64, 0);

// Schwung's library, imported while Melodee loads this page (it waits for
// the imports).
const SHARED = std.getenv("MELODEE_SCHWUNG_SHARED") || "/data/UserData/schwung/shared";
const lib = {};
{
    const parts = await Promise.all([
        import(SHARED + "/param_pages/page_controller.mjs"),
        import(SHARED + "/param_pages/page_input.mjs"),
        import(SHARED + "/param_pages/frame_ctx.mjs"),
        import(SHARED + "/param_pages/param_meta.mjs"),
        import(SHARED + "/param_pages/page_plan.mjs"),
    ]);
    for (const part of parts) Object.assign(lib, part);
}

function bare(key) {
    const k = String(key);
    return k.startsWith(PREFIX + ":") ? k.slice(PREFIX.length + 1) : k;
}

// Three answers, as Schwung's param channel gives them: text, "" (served,
// nothing), null (no answer). Melodee's read answers text or "".
function getParam(key) {
    if (!ctx) return null;
    const answer = ctx.getParam(bare(key));
    return answer === undefined ? null : answer;
}
function setParam(key, value) {
    if (ctx) ctx.setParam(bare(key), String(value));
    return true;
}

// visible_if, as shadow_ui.js's evaluateVisibilityCondition on the grid:
// against the value the controller holds first (it writes a turned knob's
// value there at once, while the write itself waits its throttle; Schwung's
// host reads cache-first for the same reason), else the module's, failing
// open when there is none.
function same(actual, expected) {
    if (typeof expected === "boolean") return ["1", "true", "on", "yes"].includes(String(actual).toLowerCase()) === expected;
    if (typeof expected === "number") { const n = Number(actual); return Number.isFinite(n) && n === expected; }
    return String(actual) === String(expected);
}
function visible(cond) {
    if (!cond || typeof cond !== "object") return true;
    const param = cond.param || cond.key || cond.param_key;
    if (!param) return true;
    const cached = controller && controller.state && controller.state.values ? controller.state.values[param] : undefined;
    const raw = cached !== undefined && cached !== null ? String(cached) : getParam(PREFIX + ":" + param);
    if (raw === null || raw === undefined || raw === "") return true;
    if (cond.equals !== undefined) return same(raw, cond.equals);
    if (cond.not_equals !== undefined) return !same(raw, cond.not_equals);
    const gt = cond.gt ?? cond.greater_than ?? cond.greater, lt = cond.lt ?? cond.smaller_than ?? cond.smaller;
    if (gt !== undefined) return Number(raw) > Number(gt);
    if (lt !== undefined) return Number(raw) < Number(lt);
    if (cond.truthy !== undefined) return (Number(raw) !== 0 || same(raw, true)) === !!cond.truthy;
    if (cond.falsey !== undefined || cond.falsy !== undefined) return !(Number(raw) !== 0 || same(raw, true));
    return true;
}

// The module's drawn pages: its canvas script, loaded once into this context
// the way Schwung loads it. A plain script (Schimera's) runs at once; a module
// (one that imports std, as Radio Garden's and Webstream's do) is imported,
// and is there from the next frame (Melodee runs pending jobs after a hook).
const overlays = {};
let pageStates = {};
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
function stateFor(key) { return pageStates[key] || (pageStates[key] = {}); }
let savedStates = "";
function keepStates() { // a drawn page's ctx.state, kept with the project: {key: its state as JSON text}
    const each = {};
    for (const key of Object.keys(pageStates)) {
        try { each[key] = JSON.stringify(pageStates[key]); } catch (e) { /* a cycle, a BigInt: not kept */ }
    }
    const text = JSON.stringify(each);
    if (text !== savedStates) { savedStates = text; if (ctx) ctx.setParam("melodee:page_states", text); }
}
function pageCtx(canvas) {
    return {
        width: 128, height: 64, state: stateFor(canvas.key),
        getParam: (k) => getParam(PREFIX + ":" + k),
        setParam: (k, v) => setParam(PREFIX + ":" + k, v),
        getValue: () => getParam(PREFIX + ":" + canvas.key) || "",
        openTextEntry: (o) => { textForPage = canvas; if (ctx) ctx.openTextEntry(o || {}); },
        openFileInTool: (path, tool) => ctx && ctx.openFileInTool(path, tool),
        close: () => { if (opened && opened.key === canvas.key) opened = null; },
    };
}
function canvasPageHook(canvas, hook, payload) {
    const ov = overlayFor(canvas);
    if (!ov || typeof ov[hook] !== "function") return undefined;
    try { return ov[hook](pageCtx(canvas), payload); } catch (e) { console.log("schwung pages: " + hook + ": " + e); return undefined; }
}
// A drawn page into Melodee's band, with what Schwung's host hands it
// (shadow_ui.js drawCanvasPageBody).
function drawCanvas(c, band, canvas) {
    const ov = overlayFor(canvas);
    if (!ov || typeof ov.drawPage !== "function") return;
    const s = controller.state;
    const values = Object.assign({}, s.values || {}, s.modValues || {});
    const page = controller.page;
    let preset = null;
    if (page && page.kind === lib.PAGE_PRESET) {
        const st = (s.preset || {})[page.name] || {};
        preset = { name: st.name, index: st.index, count: st.count, entered: menuEntered() };
    }
    try {
        ov.drawPage(lib.frameCtx(c, band), {
            key: canvas.key, values, base: s.values || {}, metaIndex: s.metaIndex,
            keys: (page && page.keys) || [], touched: typeof s.touched === "number" ? s.touched : -1,
            preset, nowMs: Date.now(), width: band.w, height: band.h, state: stateFor(canvas.key),
        });
    } catch (e) {
        console.log("schwung pages: drawPage of " + canvas.key + ": " + e);
    }
}

function menuEntered() {
    if (!controller) return false;
    return !!(typeof controller.menuEntered === "function" ? controller.menuEntered() : controller.menuEntered);
}

// Pages Melodee adds after the module's, as Schwung adds My Presets and Module.
// When Melodee's answer changes (a preset saved, loaded, edited: "* name"),
// the controller is asked to take them again, as Schwung's host does after a
// Save (refreshTrailing).
let trailingText = null;
function trailingMenus() {
    trailingText = (ctx && ctx.getParam("melodee:trailing")) || "[]";
    try { return JSON.parse(trailingText); } catch (e) { return []; }
}
function refreshTrailingIfChanged() {
    if (!controller || trailingText === null || typeof controller.refreshTrailing !== "function") return;
    const now = (ctx && ctx.getParam("melodee:trailing")) || "[]";
    if (now !== trailingText) controller.refreshTrailing();
}

// A turn on a cell that cannot be turned (Schwung's io.turnDoor). A text
// parameter (Schimera's Lyrics): its keyboard opens (Kerem, 2026-11-21: so
// trying the knobs finds it; under Schwung a turn there does nothing, the
// held knob and the click being its only way in, which stays). Once, while
// the keyboard is up: a turn's further detents do not open it again.
function openText(key, meta) {
    if (pendingText || !ctx) return;
    pendingText = key;
    ctx.openTextEntry({ title: (meta && (meta.label || meta.name)) || key, initial: getParam(PREFIX + ":" + key) || "" });
}
function turnDoor(fullKey, direction, slot, held, info) {
    const meta = info && info.meta;
    if (!meta || meta.type !== "string") return false;
    openText(info.key || bare(fullKey), meta);
    return true;
}

// Entered from the Device View: a fresh controller, as Schwung's host builds
// one on every entry (shadow_ui_param_pages.mjs: exitParamPages drops it,
// enterParamPages makes it). Its plan and values are then the module's as
// they are now, so a gate moved from outside (Schimera's Mode, turned on the
// Device View's knobs) shows the pages it gates. A throttled write, due by
// now, goes out first. The drawn pages' scripts and kept state stay.
function startOver() {
    try { controller.tick(); } catch (e) { /* nothing pending is lost by a throw here */ }
    controller = null;
    shift = false;
    exited = false;
    pendingText = null;
    textForPage = null;
    enumPicker = null;
    opened = null;
    loadingPoll = 0; loadingInterval = LOADING_POLL_TICKS; wasLoading = false;
    trailingText = null;
}

function ensure() {
    if (!ctx) return;
    let landing = null;
    const fresh = ctx.getParam("melodee:fresh");
    if (fresh) {
        try { landing = JSON.parse(fresh) || {}; } catch (e) { landing = {}; }
        if (controller) startOver();
    }
    if (controller) return;
    moduleDir = ctx.getParam("melodee:module_dir");
    pageStates = {};
    try {
        const kept = JSON.parse(ctx.getParam("melodee:page_states") || "{}") || {};
        for (const key of Object.keys(kept)) pageStates[key] = typeof kept[key] === "string" ? JSON.parse(kept[key]) : kept[key];
    } catch (e) { pageStates = {}; }
    if (typeof lib.createController !== "function") return;
    controller = lib.createController({
        getParam, setParam,
        announce: () => {},
        now: () => Date.now(),
        // Melodee draws plain cells, never Schwung's graphics: the peek and the
        // two-option turn then behave as on a plain cell.
        enableViz: false,
        heldStep: () => -1, deleteHeld: () => false, sceneArmed: () => false, heldStepIsHold: () => false,
        canvasPageHook,
        trailingMenus,
        turnDoor,
    });
    controller.load({ slot: 0, component: PREFIX, prefix: PREFIX, visible, paginate: true });
    controller.setLayout(lib.LAYOUT_MOVY || "movy");
    // Back from My Presets after a Load: on that page, its door closed
    // (shadow_ui.js reconciles the grid with restorePage("My Presets")).
    if (landing && landing.page) controller.restorePage(String(landing.page), { enter: !!landing.enter });
    lastTick = Date.now();
}

// Schwung's tick, and its host's duties around it (shadow_ui_param_pages.mjs
// tickParamPages): a module still loading is asked, with backoff, and planned
// again once it has loaded; Shift reveals.
function tick() {
    const now = Date.now();
    let n = Math.min(MAX_TICKS_PER_FRAME, Math.max(1, Math.floor((now - lastTick) / TICK_MS)));
    lastTick = now;
    while (n-- > 0) {
        if (++loadingPoll >= loadingInterval) {
            loadingPoll = 0;
            const raw = getParam(PREFIX + ":is_loading");
            if (raw === "") loadingInterval = Infinity;
            else if (raw === null || raw === undefined) { if (loadingInterval < LOADING_POLL_MAX_TICKS) loadingInterval *= 2; }
            else {
                loadingInterval = LOADING_POLL_TICKS;
                const loading = raw === "1";
                if (!loading && wasLoading) controller.reloadIfChanged({ visible });
                wasLoading = loading;
            }
        }
        controller.setReveal(shift);
        controller.tick();
    }
}

// shadow_ui_param_pages.mjs footerHints(), as Schwung's shadow UI builds it:
// jog first, click second, Back last; the verbs of what is under the cursor.
let sectionsPages = null, sectionsDiffer = false;
function sectionsAreDistinct() {
    const pages = controller.pages;
    if (!pages || !pages.length) return false;
    if (pages === sectionsPages) return sectionsDiffer;
    sectionsPages = pages;
    const seen = new Set();
    sectionsDiffer = false;
    for (const p of pages) {
        const lv = p && p.level;
        if (lv == null) continue;
        if (seen.has(lv)) { sectionsDiffer = true; break; }
        seen.add(lv);
    }
    return sectionsDiffer;
}
function hints({ jog, click, extra }) {
    const out = [];
    if (jog) out.push(["JOG", jog]);
    if (click) out.push(["CLK", click]);
    for (const e of (extra || [])) if (e) out.push(e);
    return out;
}
function footer() {
    if (enumPicker) return hints({ jog: "SEL", click: "SET", extra: [["BACK", "EXIT"]] });
    if (opened) return hints({ extra: [["BACK", "OUT"]] });
    if (controller.pickerOpen) return hints({ jog: "SECT", click: "GO", extra: [["BACK", "EXIT"]] });
    const fine = shift ? [["KNB", "FINE"]] : null;
    const mp = controller.page;
    const entered = menuEntered();
    if (mp && mp.kind === lib.PAGE_MENU)
        return entered ? hints({ jog: "SEL", click: "OPEN", extra: [["BACK", "OUT"]] }) : hints({ jog: "PAGE", click: "ENTER", extra: fine });
    if (mp && mp.kind === lib.PAGE_KNOBS && controller.isDoor && controller.isDoor(mp)) {
        if (!entered) return hints({ jog: "PAGE", click: "ENTER", extra: fine });
        if (controller.knobEditing) return hints({ jog: "ADJ", click: "DONE", extra: [["BACK", "OUT"]] });
        const rows = controller.knobRows ? controller.knobRows() : [];
        const row = rows[controller.knobRowIndex ? controller.knobRowIndex() : 0];
        const rmeta = row && controller.metaAt ? controller.metaAt(row.slot) : null;
        let verb = null;
        if (rmeta) {
            if (rmeta.writeOnly) verb = "FIRE";
            else if (lib.isTurnable(rmeta) && (!rmeta.divable || lib.flipsOnClick(rmeta))) verb = "EDIT";
            else if (rmeta.divable) verb = "OPEN";
        }
        return hints({ jog: "SEL", click: verb, extra: [["BACK", "OUT"]] });
    }
    if (mp && mp.kind === lib.PAGE_ITEMS)
        return entered ? hints({ jog: "SEL", click: "LOAD", extra: [["BACK", "OUT"]] }) : hints({ jog: "PAGE", click: "ENTER", extra: fine });
    if (mp && mp.kind === lib.PAGE_PRESET)
        return entered ? hints({ jog: "PRST", click: "EDIT", extra: [["BACK", "OUT"]] }) : hints({ jog: "PAGE", click: "ENTER", extra: fine });
    const held = controller.state ? controller.state.touched : -1;
    if (held >= 0) {
        const meta = controller.metaAt ? controller.metaAt(held) : null;
        if (meta && meta.writeOnly) return hints({ jog: "PAGE", click: "FIRE", extra: [["KNB", "FIRE"], ...(fine || [])] });
        if (lib.flipsOnClick(meta)) return hints({ jog: "PAGE", click: "FLIP", extra: fine });
        if ((meta && meta.divable) || (controller.diveTargetAt && controller.diveTargetAt(held)))
            return hints({ jog: "PAGE", click: "OPEN", extra: fine });
    }
    return hints({ jog: (shift && sectionsAreDistinct()) ? "SECT" : "PAGE", click: "MENU", extra: fine });
}

// The page as data, for Melodee to draw (schwung_pages.h, ModuleView).
function view() {
    if (typeof lib.createController !== "function" || (controller && typeof controller.describePage !== "function"))
        return { unsupported: true };
    if (!controller) return {};
    const d = controller.describePage({ title: "" });
    const s = controller.state;
    const p = controller.page;
    const entered = menuEntered();
    const v = {
        kind: p ? p.kind : null,
        page: p ? (controller.pageLabel ? controller.pageLabel(p) : p.name) : null,
        index: controller.pageIndex, count: (controller.pages || []).length,
        header: d.header || null,
        unresolved: !!d.unresolved,
        wheel: !!(enumPicker || opened || controller.pickerOpen || entered),
        exit: exited,
        footer: footer(),
        livePress: !!(controller.livePressParam && controller.livePressParam()),
        cells: [],
    };
    if (p && p.kind === lib.PAGE_KNOBS) {
        for (let slot = 0; slot < (p.keys || []).length; slot++) {
            const c = d.cells ? d.cells[slot] : null;
            if (!c) { v.cells.push(null); continue; }
            const meta = s.metaIndex ? s.metaIndex.getOrGuess(c.key) : null;
            v.cells.push({
                key: c.key, name: c.name,
                short: (p.shortNames && p.shortNames[c.key]) || (meta && meta.short_name) || c.name,
                value: c.value, headerValue: c.headerValue,
                normalized: typeof c.normalized === "number" ? c.normalized : null,
                widget: c.widget, enumIndex: c.enumIndex, options: c.options ? c.options.length : 0,
                readOnly: c.readOnly, turnable: c.turnable, divable: c.divable, touched: c.touched,
            });
        }
    }
    if (p && p.kind === lib.PAGE_MENU)
        v.menu = { entered, cursor: controller.menuIndex ? controller.menuIndex() : 0,
                   rows: (p.entries || []).map((e) => ({ label: String(e.label || ""), value: e.value === undefined || e.value === null ? "" : String(e.value) })) };
    if (p && p.kind === lib.PAGE_PRESET) {
        const st = (s.preset || {})[p.name] || {};
        v.preset = { entered, name: st.name || "", index: st.index || 0, count: st.count || 0 };
    }
    if (p && p.kind === lib.PAGE_ITEMS) {
        const st = (s.items || {})[p.name] || {};
        v.items = { entered, cursor: st.cursor || 0, current: st.current === undefined ? -1 : st.current,
                    rows: (st.list || []).map((i) => String(i.label)) };
    }
    if (controller.pickerOpen)
        v.picker = { cursor: controller.pickerIndex || 0,
                     rows: (controller.pickerEntries || []).map((e) => ({ label: String(e.name), value: e.pages > 1 ? String(e.pages) : "" })) };
    const peek = controller.enumPeek ? controller.enumPeek() : null;
    if (peek) v.peek = { title: String(peek.title || ""), index: peek.index, rows: (peek.options || []).map(String) };
    if (enumPicker) v.enumPicker = { title: enumPicker.title, index: enumPicker.index, rows: enumPicker.options };
    if (s.notice && (!s.notice.until || Date.now() < s.notice.until)) v.notice = String(s.notice.text || "");
    if (s.hintLines && s.hintLines.lines) v.hint = { title: String(s.hintLines.title || ""), lines: s.hintLines.lines.map(String) };
    const canvas = opened || (p && p.canvas) || null;
    if (canvas) v.canvas = { key: canvas.key, enterable: !!canvas.enterable, entered: !!opened || entered, opened: !!opened };
    return v;
}

function publish() {
    if (!ctx) return;
    ctx.setParam("melodee:view", JSON.stringify(view()));
    keepStates();
}

// An "open" the controller hands back (page_input.mjs click): an enum list
// picker, a text keyboard, a drawn page; a trailing page's entry is Melodee's.
function take(todo) {
    if (!todo) return;
    exited = todo.action === "exit";
    if (todo.action === "menu" && todo.entry && todo.entry.action) { ctx.setParam("melodee:action", String(todo.entry.action)); return; }
    if (todo.action !== "open") return;
    const meta = todo.meta || {};
    const key = todo.key || bare(todo.fullKey || "");
    const title = meta.label || meta.name || key;
    if (Array.isArray(todo.options) && todo.options.length) {
        enumPicker = { key, title: String(title), options: todo.options.map(String), index: todo.index || 0 };
        return;
    }
    if (meta.type === "string") { openText(key, meta); return; }
    if (meta.type === "canvas" || meta.canvas_script) {
        opened = { key, script: meta.canvas_script || "canvas.js", enterable: true };
        return;
    }
    ctx.setParam("melodee:say", "NOT ON MELODEE YET"); // a file or sample editor: Schwung's own screens
}

// The enum picker, as shadow_ui.js's ENUM_PICKER: the jog or any knob moves,
// the click sets, Back leaves it unset.
function pickerMidi(data) {
    const cc = (data[0] & 0xf0) === 0xb0, number = data[1], value = data[2];
    if (cc && (number === 14 || (number >= 71 && number <= 78))) {
        const delta = value === 0 ? 0 : value < 64 ? 1 : -1;
        enumPicker.index = Math.max(0, Math.min(enumPicker.options.length - 1, enumPicker.index + delta));
    } else if (cc && number === 3 && value > 0) {
        controller.commitEnum(enumPicker.key, enumPicker.index);
        enumPicker = null;
    } else if (cc && number === 51 && value > 0) {
        enumPicker = null;
    }
}

// A canvas opened from its cell: the jog and its push are the page's, Back
// asks the page first ("true": it went up a level) and else closes it.
function openedMidi(data) {
    const cc = (data[0] & 0xf0) === 0xb0, number = data[1], value = data[2];
    if (cc && number === 51) {
        if (value > 0 && canvasPageHook(opened, "handleBack", {}) !== true) opened = null;
        return;
    }
    if (cc && (number === 14 || number === 3)) canvasPageHook(opened, "onMidi", { data: [data[0], data[1], data[2]] });
}

// A throw in a hook ends the page for good (Melodee's rule, as Schwung's):
// say where, a few times, and carry on.
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
    // A frame: Schwung's ticks since the last, the page published, and a drawn
    // page into the band Melodee gives (info.x, y, width, height).
    drawPage(c, info) {
        ctx = c;
        ensure();
        if (controller) {
            refreshTrailingIfChanged();
            tick();
            const canvas = opened || (controller.page && controller.page.canvas) || null;
            // Under something open over the page (Sections, the enum picker,
            // the peek, a hint) the page is not drawn: Melodee draws that
            // instead, on black, as Schwung's overlays cover the page.
            const s = controller.state || {};
            const covered = !!(enumPicker || controller.pickerOpen || (controller.enumPeek && controller.enumPeek()) || (s.hintLines && s.hintLines.lines));
            if (canvas && !covered && info && info.height > 0)
                drawCanvas(c, { x: info.x || 0, y: info.y || 0, w: info.width || 128, h: info.height }, canvas);
        }
        publish();
    },
    // The Move's controls, raw: knobs (CC 71-78) and their touches (notes
    // 0-7), the jogwheel (CC 14, its push CC 3), Back (CC 51), Shift (CC 49),
    // Copy / Delete / Undo (CC 60 / 119 / 56), a pad (notes 68-99).
    onMidi(c, msg) {
        ctx = c;
        ensure();
        if (!controller || !msg || !msg.data) return;
        const data = msg.data;
        if ((data[0] & 0xf0) === 0xb0 && data[1] === 49) { shift = data[2] > 0; publish(); return; }
        const pressed = data[2] > 0 && (data[0] & 0xf0) !== 0x80;
        if (pressed) exited = false; // a release (Back's own) leaves the answer as it was
        if (enumPicker) { pickerMidi(data); publish(); return; }
        if (opened) { openedMidi(data); publish(); return; }
        if (lib.isHardwarePadPress && lib.isHardwarePadPress(data)) { controller.vouchLivePress(); publish(); return; }
        const intent = lib.decodeInput(data, { shift, mute: false });
        if (intent) take(lib.applyInput(controller, intent, { nowMs: Date.now(), reveal: false }));
        publish();
    },
    // The keyboard's answer: a text parameter's, or a drawn page's.
    onTextEntry(c, answer) {
        ctx = c;
        if (textForPage) { canvasPageHook(textForPage, "onTextEntry", answer); textForPage = null; }
        else if (pendingText && answer && !answer.cancelled) setParam(PREFIX + ":" + pendingText, answer.text);
        pendingText = null;
        publish();
    },
};
for (const name of ["drawPage", "onMidi", "onTextEntry"])
    globalThis.canvas_overlay[name] = guarded(name, globalThis.canvas_overlay[name]);
