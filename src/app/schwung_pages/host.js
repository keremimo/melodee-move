// SPDX-License-Identifier: AGPL-3.0-only
// Portions adapted from Schwung's src/shadow/shadow_ui.js (v1.9.4):
// Copyright (c) 2025-2026 Charles Vestal, MIT License (licenses/Schwung-LICENSE.txt).
//
// A Schwung module's pages, shown by Schwung's own screen code (Kerem,
// 2026-11-21: "perfectly replicate Schwung's GUI"): from the push into a
// module until Back out, every pixel is drawn by the code Schwung's shadow UI
// runs, imported at run time from the Schwung on the Move (MIT, Charles
// Vestal and contributors):
//
//   shadow/shadow_ui_param_pages.mjs  the grid's host: header, footer verbs,
//                                     drawing, input, Back
//   shared/...                        the page controller and the libraries
//                                     its screens draw with
//
// What Schwung's shadow_ui.js gives those (it cannot be imported: it is the
// whole shadow UI) is adapted here from it: the `ctx` it fills, the shim's
// globals, its tick and draw order, its views and how input reaches them. Schwung's screen itself, its shapes and font,
// is Melodee's C++ (schwung_display.h), as is shadow_load_ui_module
// (canvas_page.cpp). Imports name Schwung's folder on the Move;
// MELODEE_SCHWUNG_ROOT moves it (the tests').
//
// The pages after the module's own, My Presets and Module, are Schwung's
// too: its user presets (shadow/shadow_ui_presets.mjs), its help viewer
// (shadow/shadow_ui_settings.mjs), its module lists and its module picker
// (shared/module_lists.mjs, shared/chain_editor_chrome.mjs), with the glue
// shadow_ui.js gives them written here again. What is Melodee's there: the
// modules a swap may pick and the loading of one, Remove's removing, and its
// own Auto Sample row.
//
// The seam with Melodee, through the page ctx each hook is lent:
//   getParam("<key>")              the module's parameter (Schwung's "synth:"
//                                  or "fxN:" taken off): text, "" or null
//   getParam("melodee:entry")      once, after an entry: {slot, component,
//                                  module, name, abbrev, dir, presets,
//                                  instrument, page, enter}
//   getParam("melodee:shift")      "1" while Shift is held or locked
//   getParam("melodee:reader")     "1" while Melodee's screen reader is on
//   getParam("melodee:page_states") the drawn pages' kept state, at start
//   getParam("melodee:preset_record") the user preset the module is on
//                                  (Schwung's {name, hash}), at start
//   getParam("melodee:modules")    the modules a swap may pick, Melodee's
//                                  ([{id, name, abbrev, component_type,
//                                  subcategory}])
//   setParam("melodee:exit", "")   Back at the top: out to the Device View
//   setParam("melodee:knob", "n c") knob n's ring (0-7), Schwung's colour c
//                                  ("-1 0": all given back)
//   setParam("melodee:status", json) what is shown, for status lines and tests
//   setParam("melodee:page_states", json) the drawn pages' state changed
//   setParam("melodee:preset_record", json) the user preset record changed
//                                  ("": none), kept with Melodee's project
//   setParam("melodee:say", text)  Schwung's screen reader's words
//   setParam("melodee:action", what) "swap <id>", "remove" or "auto_sample"
//   setParam("melodee:pad_block", "1" or "0") the pads the keyboard's
//                                  (Schwung's pad typing): none to the module
//   setParam("melodee:pad", "n c") pad note n (68-99) lit Schwung's colour c
//                                  while the pads are the keyboard's
//
// Schwung's own files (its presets, module_lists.json, shadow_config.json)
// are where its code looks for them, MELODEE_SCHWUNG_ROOT's folder in the
// tests: the host_* calls here move the paths, and canvas_page.cpp gives
// Schwung's modules an "os" that moves them too.
import * as std from "std";
import * as os from "os";

// ---------------------------------------------------------------- Melodee

let melo = null; // the ctx of the hook running now
function seam(key) { return melo ? melo.getParam("melodee:" + key) : null; }
function tell(key, value) { if (melo) melo.setParam("melodee:" + key, String(value)); }

const ROOT = std.getenv("MELODEE_SCHWUNG_ROOT") || "/data/UserData/schwung";
function onDisk(path) { // Schwung's own paths, where its folder is
    const p = String(path);
    return p === "/data/UserData/schwung" || p.startsWith("/data/UserData/schwung/") ? ROOT + p.slice(22) : p;
}
function log(source, message) { globalThis.unified_log(source, message); }

// ---------------------------------------------------------------- the shim's globals
// What shadow_ui.c and the shim give the shadow UI, Melodee's way. They are
// set before Schwung's code is imported: some of it looks for them as it
// loads (logger.mjs).

globalThis.shadow_get_shift_held = () => (seam("shift") === "1" ? 1 : 0);
globalThis.shadow_get_display_mode = () => 1; // the shadow UI is what is on the screen
globalThis.tts_get_enabled = () => seam("reader") === "1";
let lastSaid = ""; // the last words, for the status (the tests)
globalThis.host_send_screenreader = (text) => { lastSaid = String(text); tell("say", text); return true; };
globalThis.shadow_get_held_step = () => -1;
globalThis.shadow_get_held_step_is_hold = () => 0;
globalThis.shadow_get_delete_held = () => 0;
globalThis.shadow_get_scene_state = () => null;
globalThis.shadow_get_plock_seq = () => 0;
let padObserve = false;
globalThis.host_pad_observe = (on) => { padObserve = !!on; return true; };
// The pads to the shadow UI alone (the keyboard's pad typing): the shim's
// pad_block, told Melodee when it changes. Asked every tick, as Schwung
// restates it.
let padBlock = -1;
globalThis.host_pad_block = (on) => {
    const want = on ? 1 : 0;
    if (want !== padBlock && melo) { padBlock = want; tell("pad_block", want); }
    return true;
};
// The LEDs Schwung's screens light: the knob rings, [cable 0x0b, CC,
// number, colour], and the pads the keyboard lights, [cable 0x09, note on,
// note, colour].
globalThis.move_midi_internal_send = (bytes) => {
    if (!bytes || bytes.length < 4) return true;
    const kind = bytes[1] & 0xf0;
    if (kind === 0xb0 && bytes[2] >= 71 && bytes[2] <= 78) tell("knob", (bytes[2] - 71) + " " + bytes[3]);
    else if (kind === 0x90 && bytes[2] >= 68 && bytes[2] <= 99) tell("pad", bytes[2] + " " + bytes[3]);
    return true;
};
globalThis.shadow_restore_knob_leds = () => { tell("knob", "-1 0"); return true; };
globalThis.unified_log = (source, message) => { if (std.getenv("MELODEE_SCHWUNG_LOG")) console.log("[" + source + "] " + message); };
globalThis.unified_log_enabled = () => !!std.getenv("MELODEE_SCHWUNG_LOG");
globalThis.host_log = (message) => log("host", message);
globalThis.host_file_exists = (path) => {
    const [st, err] = os.stat(onDisk(path));
    return !err && (st.mode & os.S_IFMT) === os.S_IFREG;
};
globalThis.host_read_file = (path) => std.loadFile(onDisk(path));
globalThis.host_write_file = (path, text) => {
    const f = std.open(onDisk(path), "w");
    if (!f) return false;
    f.puts(String(text));
    f.close();
    return true;
};
globalThis.host_ensure_dir = (path) => {
    let at = "";
    for (const part of onDisk(path).split("/").filter(Boolean)) {
        at += "/" + part;
        os.mkdir(at, 0o755);
    }
    return true;
};
// Schwung's Param View setting (Global Settings > Display), from its file,
// as shadow_ui.js reads it at start: Knobs unless it says List.
let paramView = 1;
try {
    const text = std.loadFile(onDisk("/data/UserData/schwung/param_view.json"));
    const cfg = text ? JSON.parse(text) : null;
    if (cfg && typeof cfg.param_view === "number") paramView = cfg.param_view;
} catch (e) { /* Knobs */ }
globalThis.param_view_get_mode = () => paramView;

// ---------------------------------------------------------------- Schwung's code

const SHARED = "/data/UserData/schwung/shared", SHADOW = "/data/UserData/schwung/shadow";
const [ctxModule, spp, frames, childKeys, , menu, lists, keyboard, enumList, listKnob, browser, flatFiles,
       reader, scrolling, inputFilter, knobEngine, paramFormat, loadGate, presetsUi, settingsUi, currentPreset,
       ModuleLists, ModuleCategories, chrome, renderPage, nameSort] = await Promise.all([
    import(SHADOW + "/shadow_ui_ctx.mjs"),
    import(SHADOW + "/shadow_ui_param_pages.mjs"),
    import(SHARED + "/param_pages/frame_ctx.mjs"),
    import(SHARED + "/param_pages/child_key.mjs"),
    import(SHARED + "/param_pages/wav_io_qjs.mjs"), // the sample cells' file IO, as shadow_ui.js imports it
    // The screens the grid hands off to, and what they draw and step with.
    import(SHARED + "/menu_layout.mjs"),
    import(SHARED + "/chain_ui_views.mjs"),
    import(SHARED + "/text_entry.mjs"),
    import(SHARED + "/param_pages/enum_list.mjs"),
    import(SHARED + "/param_pages/list_knob.mjs"),
    import(SHARED + "/filepath_browser.mjs"),
    import(SHARED + "/file_flat.mjs"),
    import(SHARED + "/screen_reader.mjs"),
    import(SHARED + "/scrollable_text.mjs"),
    import(SHARED + "/input_filter.mjs"),
    import(SHARED + "/knob_engine.mjs"),
    import(SHARED + "/param_format.mjs"),
    import(SHARED + "/component_load_gate.mjs"),
    // The trailing pages' screens: My Presets, Module Help, Add to List, Swap.
    import(SHADOW + "/shadow_ui_presets.mjs"),
    import(SHADOW + "/shadow_ui_settings.mjs"),
    import(SHARED + "/param_pages/current_preset.mjs"),
    import(SHARED + "/module_lists.mjs"),
    import(SHARED + "/module_categories.mjs"),
    import(SHARED + "/chain_editor_chrome.mjs"),
    import(SHARED + "/param_pages/render_page.mjs"),
    import(SHARED + "/name_sort.mjs"),
]);
const ctx = ctxModule.ctx;
const { enterParamPages, exitParamPages, tickParamPages, drawParamPages, handleParamPagesMidi, paramPagesActive,
        paramPagesCachedValue, paramPagesComponent, paramPagesSlot, paramPagesLevelNameOf, paramPagesChildIndex,
        paramPagesEntering, currentParamPage, paramPagesPageName, paramPagesPickerOpen, paramPagesFooterHints,
        enumPickerFooterHints, commitParamPagesValue, clearParamPagesTouch, paramPagesExitMenu,
        paramPagesRefreshTrailing, CONTRACT_SETTLE_MS } = spp;
const { drawMenuHeader: drawHeader, drawMenuFooter: drawFooter, drawMenuList, drawMessageOverlay,
        showOverlay, hideOverlay, tickOverlay, drawOverlay, drawConfirmOverlay } = menu;
const { SCREEN_WIDTH, SCREEN_HEIGHT, LIST_TOP_Y, FOOTER_RULE_Y, truncateText } = lists;
const { openTextEntry, closeTextEntry, isTextEntryActive, handleTextEntryMidi, drawTextEntry, tickTextEntry,
        setPadSelectGlobal, setTextPreviewGlobal, getTextEntryBuffer } = keyboard;
const { drawEnumList } = enumList;
const { listKnobInit, listKnobStep } = listKnob;
const { buildFilepathBrowserState, refreshFilepathBrowser, moveFilepathBrowserSelection,
        activateFilepathBrowserItem } = browser;
const { planFileFlatOpen, fileFlatPick, createRefusalLatch } = flatFiles;
const { announce, announceMenuItem, announceParameter } = reader;
const { wrapText, createScrollableText, handleScrollableTextJog, isActionSelected, drawScrollableText,
        visibleLinesFor } = scrolling;
const { decodeDelta } = inputFilter;
const { knobInit, knobStep } = knobEngine;
const { formatParamValue: ufFormatParamValue, formatParamForSet: ufFormatParamForSet } = paramFormat;
const { decideComponentEntry, holdProbeIntervalTicks, ENTRY_HOLD, ENTRY_ENTER } = loadGate;
const { makeRecord, presetRowValue, isModified } = currentPreset;
const { drawChainPicker, drawListScreen } = chrome;
const { fitText } = renderPage;
const { compareNames } = nameSort;

// Schwung's Display settings, Pad Typing and Text Preview, from its file, as
// shadow_ui.js reads them at start (on Kerem's Move both are on); and its
// Audition (browser_preview, default off): a preset list plays the preset
// under the cursor (on Kerem's Move it is on).
let previewEnabled = false;
for (const [name, set] of [["pad_typing", setPadSelectGlobal], ["text_preview", setTextPreviewGlobal],
                           ["browser_preview", (on) => { previewEnabled = on; }]]) {
    try {
        const content = host_read_file("/data/UserData/schwung/shadow_config.json");
        const config = content ? JSON.parse(content) : null;
        if (config && config[name] !== undefined) set(config[name]);
    } catch (e) { /* the setting's default */ }
}

// ---------------------------------------------------------------- the views

const VIEWS = { // shadow_ui.js's names, the ones these screens use
    CHAIN_EDIT: "chainedit", PARAM_PAGES: "parampages", HIERARCHY_EDITOR: "hierarch",
    PRESETS: "modpresets", PRESET_DETAIL: "modpresetdetail", COMPONENT_SELECT: "compselect",
    CANVAS: "canvas", FILEPATH_BROWSER: "filepathbrowser", GLOBAL_SETTINGS: "globalsettings",
    ENUM_PICKER: "enumpick", FILE_FLAT: "fileflat", COMPONENT_LOADING: "comploading",
    COMPONENT_EDIT: "compedit", MODULE_LISTS: "modulelists", MODULE_LISTS_EDIT: "modulelistsedit",
    MODULE_LISTS_ACTIONS: "modulelistsact",
};
const NUM_KNOBS = 8, KNOB_CC_START = 71, KNOB_CC_END = 78;
const CC_JOG = 14, CC_CLICK = 3, CC_BACK = 51;
const REDRAW_INTERVAL = 2; // shadow_ui.js draws every other tick unless asked
const isShiftHeld = () => shadow_get_shift_held() !== 0;
let view = VIEWS.CHAIN_EDIT;
let needsRedraw = true;

// shadow_ui.js's: a change only, and a redraw. Its chain editor is Melodee's
// Device View: reached and not taken back to a grid (drawView), the pages
// are left.
function setView(next) {
    if (view === next) return;
    view = next;
    needsRedraw = true;
}

// Which module, where (Melodee's track is Schwung's slot: "S2 > ..."), and
// what Melodee offers on its trailing pages: My Presets where its state can
// be read (a synth's module), Auto Sample on an instrument's.
let slot = 0, component = "synth", moduleId = "", moduleDir = "", moduleName = "", moduleAbbrev = "";
let presetsOffered = false, autoSampleOffered = false;
// The module in the position as these screens know it: the one entered,
// until a swap picks another or a removal empties it.
let positionModule = "";
function prefixOf(key) { return key === "midiFx" ? "midi_fx1" : key; } // shadow_ui.js's chainComponentId
function moduleKeyOf(key) { const p = prefixOf(key); return p ? `${p}_module` : null; }
// The position's label, as Schwung's chain model names it ("Synth", "FX 2").
function componentLabel(key) {
    const fx = /^fx(\d+)$/.exec(String(key));
    return fx ? `FX ${fx[1]}` : key === "synth" ? "Synth" : String(key);
}

// ---------------------------------------------------------------- the slot's parameters

// Schwung's param channel: text, "" (served, nothing), null (no answer).
function getSlotParam(s, key) {
    const k = String(key), p = prefixOf(component);
    if (s !== slot) return null;
    if (k === moduleKeyOf(component)) return moduleId;
    if (!k.startsWith(p + ":") || !melo) return null;
    // Schwung's chain host answers these for every parameter of a synth or
    // an effect (chain_mod.c); Melodee modulates nothing: never modulated,
    // the base the value itself.
    if (k.endsWith(":modulated")) return "0";
    if (k.endsWith(":base")) return getSlotParam(s, k.slice(0, -5));
    const answer = melo.getParam(k.slice(p.length + 1));
    return answer === undefined ? null : answer;
}
function setSlotParam(s, key, value) {
    const k = String(key), p = prefixOf(component);
    if (s !== slot || !k.startsWith(p + ":") || !melo) return false;
    melo.setParam(k.slice(p.length + 1), String(value));
    return true;
}
function getSlotStateWithRetry(s, key, retries) { // shadow_ui.js's: a null read asked again, "" an answer
    const limit = typeof retries === "number" ? retries : 3;
    let state = getSlotParam(s, key);
    if (state) return state;
    if (state === "") return "";
    for (let attempt = 1; attempt <= limit; attempt++) {
        state = getSlotParam(s, key);
        if (state) return state;
        if (state === "") return "";
    }
    return null;
}
const slotParamCache = {}; // shadow_ui.js's getSlotParamCached: half a second
function getSlotParamCached(s, key, owner) {
    const ck = `${s}:${key}`, hit = slotParamCache[ck], now = Date.now();
    if (hit && hit.module === owner && now - hit.ts < 500) return hit.value;
    const value = getSlotParam(s, key);
    if (value === null || value === undefined) return hit && hit.module === owner ? hit.value : value;
    slotParamCache[ck] = { module: owner, value, ts: now };
    return value;
}

// Module names, from module.json, as shadow_ui.js caches them (the one
// entered, and those a swap's list names).
const moduleAbbrevCache = Object.create(null), moduleNameCache = Object.create(null);
function cacheModuleAbbrev(json) {
    if (json && json.id && json.abbrev) moduleAbbrevCache[String(json.id).toLowerCase()] = json.abbrev;
    if (json && json.id && json.name) moduleNameCache[String(json.id).toLowerCase()] = json.name;
}
function moduleIdOf(ref) {
    let id = String(ref);
    if (id.indexOf("/") >= 0) {
        const parts = id.split("/").filter(Boolean);
        if (!parts.length) return "";
        const last = parts[parts.length - 1];
        id = /\.[A-Za-z0-9]+$/.test(last) && parts.length >= 2 ? parts[parts.length - 2] : last;
    }
    return id;
}
function getModuleAbbrev(ref) {
    if (!ref) return "--";
    const id = moduleIdOf(ref);
    if (!id) return "--";
    return moduleAbbrevCache[id.toLowerCase()] || id.substring(0, 2).toUpperCase();
}
function getModuleDisplayName(ref) {
    if (!ref) return "--";
    const id = moduleIdOf(ref);
    if (!id) return "--";
    return moduleNameCache[id.toLowerCase()] || getModuleAbbrev(id);
}

// ---------------------------------------------------------------- visible_if

// shadow_ui.js's evaluateVisibilityCondition for the grid: the condition's
// key in the grid's own dialect, its value from the grid's cache first, else
// a read kept half a second; no value: shown.
function parseMetaBool(value) {
    if (value === true || value === 1) return true;
    if (value === false || value === 0 || value === null || value === undefined) return false;
    const v = String(value).trim().toLowerCase();
    return v === "1" || v === "true" || v === "on" || v === "yes";
}
function parseMetaNumber(value, fallback) { const n = Number(value); return Number.isFinite(n) ? n : fallback; }
function compareConditionValue(actual, expected) {
    if (typeof expected === "boolean") return parseMetaBool(actual) === expected;
    if (typeof expected === "number") { const n = Number(actual); return Number.isFinite(n) && n === expected; }
    return String(actual) === String(expected);
}
function hierChildKeyFor(levelDef, index, rawKey) {
    if (!rawKey || index < 0 || !childKeys.hasChildren(levelDef)) return rawKey;
    const listed = (k) => (typeof k === "string" ? k : k && k.key) === rawKey;
    if (!(levelDef.knobs || []).some(listed) && !(levelDef.params || []).some(listed)) return rawKey;
    return childKeys.resolveChildKey(levelDef, index, rawKey) || rawKey;
}
function hierGenericKeyFor(levelDef, index, concreteKey) {
    if (!concreteKey || index < 0 || !childKeys.hasChildren(levelDef)) return concreteKey;
    const cand = [];
    for (const k of levelDef.knobs || []) cand.push(typeof k === "string" ? k : k && k.key);
    for (const p of levelDef.params || []) {
        if (p && typeof p === "object") { if (!p.level) cand.push(p.key); }
        else cand.push(p);
    }
    for (const g of cand) if (g && childKeys.resolveChildKey(levelDef, index, g) === concreteKey) return g;
    return concreteKey;
}
function evaluateVisibilityCondition(condition, levelDef) {
    if (!condition || typeof condition !== "object") return true;
    const param = condition.param || condition.key || condition.param_key;
    if (!param) return true;
    if (!(view === VIEWS.PARAM_PAGES || paramPagesEntering()) || !paramPagesActive()) return true;
    const comp = paramPagesComponent(), s = paramPagesSlot(), gridPrefix = prefixOf(comp);
    const lvlName = paramPagesLevelNameOf(levelDef);
    const childIdx = lvlName ? paramPagesChildIndex(lvlName) : -1;
    const raw = String(param);
    const fullKey = raw.includes(":") ? raw : gridPrefix ? `${gridPrefix}:${hierChildKeyFor(levelDef, childIdx, raw)}` : raw;
    const bare = fullKey.startsWith(`${gridPrefix}:`) ? fullKey.slice(gridPrefix.length + 1) : fullKey;
    let value = paramPagesCachedValue(hierGenericKeyFor(levelDef, childIdx, bare));
    if (value === undefined) value = getSlotParamCached(s, fullKey, `grid:${s}:${comp}`);
    if (value === null || value === undefined) return true; // fail open
    if (condition.equals !== undefined) return compareConditionValue(value, condition.equals);
    if (condition.not_equals !== undefined) return !compareConditionValue(value, condition.not_equals);
    if (condition.gt !== undefined || condition.greater_than !== undefined || condition.greater !== undefined) {
        const t = parseMetaNumber(condition.gt !== undefined ? condition.gt
            : condition.greater_than !== undefined ? condition.greater_than : condition.greater, null);
        const n = Number(value);
        return Number.isFinite(n) && Number.isFinite(t) && n > t;
    }
    if (condition.lt !== undefined || condition.smaller_than !== undefined || condition.smaller !== undefined) {
        const t = parseMetaNumber(condition.lt !== undefined ? condition.lt
            : condition.smaller_than !== undefined ? condition.smaller_than : condition.smaller, null);
        const n = Number(value);
        return Number.isFinite(n) && Number.isFinite(t) && n < t;
    }
    if (condition.truthy !== undefined) return parseMetaBool(condition.truthy) ? parseMetaBool(value) : !parseMetaBool(value);
    if (condition.falsey !== undefined || condition.falsy !== undefined) {
        const flag = condition.falsey !== undefined ? condition.falsey : condition.falsy;
        return parseMetaBool(flag) ? !parseMetaBool(value) : parseMetaBool(value);
    }
    return parseMetaBool(value);
}

// ---------------------------------------------------------------- a module's drawn pages

// shadow_ui.js's canvas page glue: the script found under the module's
// folder, loaded once (a null remembered too), its drawPage drawn into the
// band through Schwung's frame_ctx, its hooks given Schwung's hook ctx, a
// throw ending it for the session.
const canvasPageDrawers = {}, canvasPageOverlays = {}, canvasPageDisabled = {};
let canvasPageStates = {};
const CANVAS_PAGE_CLOSE = { close: true };
function moduleFileExists(path) {
    const [st, err] = os.stat(path);
    return !err && (st.mode & os.S_IFMT) === os.S_IFREG;
}
function resolveCardScriptPath(s, comp, scriptRef) {
    if (!scriptRef) return "";
    if (scriptRef.startsWith("/")) return moduleFileExists(scriptRef) ? scriptRef : "";
    if (s !== slot || !moduleDir) return "";
    const path = `${moduleDir}/${scriptRef}`;
    return moduleFileExists(path) ? path : "";
}
function getObjectPathValue(root, ref) {
    if (!root || !ref || typeof ref !== "string") return undefined;
    const parts = ref.split(".").map((p) => p.trim()).filter(Boolean);
    if (!parts.length) return undefined;
    let cur = root;
    for (const part of parts) {
        if (!cur || (typeof cur !== "object" && typeof cur !== "function")) return undefined;
        cur = cur[part];
    }
    return cur;
}
function resolveOverlayObject(candidate) {
    if (!candidate) return { overlay: null, error: "" };
    if (typeof candidate === "function") {
        try {
            const built = candidate();
            return built && typeof built === "object" ? { overlay: built, error: "" } : { overlay: null, error: "overlay factory returned invalid value" };
        } catch (e) { return { overlay: null, error: String(e) }; }
    }
    return typeof candidate === "object" ? { overlay: candidate, error: "" } : { overlay: null, error: "" };
}
function resolveOverlayFromGlobals(ref, fallbacks, missing) {
    const seen = new Set(), candidates = [];
    let firstError = "";
    const push = (v) => { if (v && !seen.has(v)) { seen.add(v); candidates.push(v); } };
    if (ref && typeof ref === "string" && ref.trim()) {
        const key = ref.trim();
        push(getObjectPathValue(globalThis, key));
        push(globalThis[key]);
        push(getObjectPathValue(globalThis.canvas_overlays, key));
    }
    for (const c of fallbacks || []) push(c);
    for (const c of candidates) {
        const r = resolveOverlayObject(c);
        if (r.overlay) return { overlay: r.overlay, error: "" };
        if (!firstError && r.error) firstError = r.error;
    }
    if (firstError) return { overlay: null, error: firstError };
    if (ref && typeof ref === "string" && ref.trim()) return { overlay: null, error: `overlay not found: ${ref.trim()}` };
    return { overlay: null, error: missing };
}
// A script loaded with the globals it may set put back as they were (ours,
// canvas_overlay, among them).
function loadKeepingGlobals(path, names, read) {
    const had = {}, saved = {};
    for (const n of names) { had[n] = Object.prototype.hasOwnProperty.call(globalThis, n); saved[n] = globalThis[n]; }
    let ok = false, error = "", result;
    try { ok = shadow_load_ui_module(path); } catch (e) { ok = false; error = String(e); }
    try { result = read(); } catch (e) { result = undefined; }
    for (const n of names) {
        if (had[n]) globalThis[n] = saved[n];
        else { try { delete globalThis[n]; } catch (e) { /* kept */ } }
    }
    return { ok, error, result };
}
const HOOK_GLOBALS = ["init", "tick", "onMidiMessageInternal", "onMidiMessageExternal", "canvas_overlay", "canvas_overlays"];
function loadCanvasOverlayScript(path, ref) {
    const r = loadKeepingGlobals(path, HOOK_GLOBALS,
        () => resolveOverlayFromGlobals(ref, [globalThis.canvas_overlay], "canvas script missing globalThis.canvas_overlay"));
    if (!r.ok) return { overlay: null, error: r.error || "failed to load canvas script" };
    if (!r.result || !r.result.overlay) return { overlay: null, error: (r.result && r.result.error) || "canvas script missing overlay object" };
    return { overlay: r.result.overlay, error: "" };
}
function canvasPageDrawer(s, comp, canvas) {
    if (!canvas || !canvas.script) return null;
    const path = resolveCardScriptPath(s, comp, canvas.script);
    if (!path) return null;
    const cacheKey = `${path}|${canvas.overlay || ""}`;
    if (canvasPageDisabled[cacheKey]) return null;
    if (Object.prototype.hasOwnProperty.call(canvasPageDrawers, cacheKey)) return canvasPageDrawers[cacheKey];
    const loaded = loadCanvasOverlayScript(path, canvas.overlay || "");
    const ov = loaded && loaded.overlay;
    const fn = ov && typeof ov.drawPage === "function" ? ov.drawPage.bind(ov) : null;
    if (!fn) log("canvas", `${path} exposes no drawPage${loaded && loaded.error ? ` (${loaded.error})` : ""}`);
    canvasPageDrawers[cacheKey] = fn;
    canvasPageOverlays[cacheKey] = ov || null;
    return fn;
}
function canvasPageState(s, cacheKey) {
    const k = `${s}|${cacheKey}`;
    if (!canvasPageStates[k]) canvasPageStates[k] = {};
    return canvasPageStates[k];
}
let keptStates = "";
function keepStates() { // the drawn pages' state, kept with Melodee's project
    const each = {};
    for (const k of Object.keys(canvasPageStates)) {
        try { each[k] = JSON.stringify(canvasPageStates[k]); } catch (e) { /* not kept */ }
    }
    const text = JSON.stringify(each);
    if (text !== keptStates) { keptStates = text; tell("page_states", text); }
}
function canvasPageHook(s, comp, canvas, hook, payload) {
    if (!canvas || !hook) return undefined;
    canvasPageDrawer(s, comp, canvas);
    const path = resolveCardScriptPath(s, comp, canvas.script);
    if (!path) return undefined;
    const cacheKey = `${path}|${canvas.overlay || ""}`;
    if (canvasPageDisabled[cacheKey]) return undefined;
    const ov = canvasPageOverlays[cacheKey];
    if (!ov || typeof ov[hook] !== "function") return undefined;
    const state = canvasPageState(s, cacheKey);
    const closed = { wanted: false };
    let textWanted = null;
    const prefix = prefixOf(comp);
    const full = (k) => (String(k).includes(":") ? String(k) : prefix ? `${prefix}:${k}` : String(k));
    const hookCtx = {
        width: 128, height: 64, state,
        getParam: (k) => getSlotParam(s, full(k)),
        setParam: (k, v) => setSlotParam(s, full(k), String(v)),
        getValue: () => (canvas.key ? getSlotParam(s, full(canvas.key)) || "" : ""),
        setValue: (v) => (canvas.key ? setSlotParam(s, full(canvas.key), String(v)) : false),
        measureText: (text) => text_width(String(text == null ? "" : text)),
        shiftHeld: () => shadow_get_shift_held() !== 0,
        now: () => Date.now(),
        random: () => Math.random(),
        close: () => { closed.wanted = true; return true; },
        openFileInTool: () => false, // Schwung's tools are not Melodee's
        openTextEntry: (opts) => {
            if (isTextEntryActive() || textWanted) return false;
            const o = opts || {};
            textWanted = { title: String(o.title == null ? "" : o.title), initial: String(o.initial == null ? "" : o.initial) };
            return true;
        },
    };
    try {
        const r = ov[hook](hookCtx, payload || {});
        if (textWanted) openCanvasPageTextEntry(s, comp, canvas, textWanted);
        return closed.wanted ? CANVAS_PAGE_CLOSE : r;
    } catch (e) {
        canvasPageDisabled[cacheKey] = true;
        log("canvas", `canvas page ${cacheKey} disabled after throw in ${hook}: ${e}`);
        return undefined;
    }
}
function drawCanvasPageBody(s, comp, drawCtx, band, canvas, payload) {
    const fn = canvasPageDrawer(s, comp, canvas);
    if (!fn) return;
    const path = resolveCardScriptPath(s, comp, canvas.script);
    const cacheKey = `${path}|${canvas.overlay || ""}`;
    try {
        fn(frames.frameCtx(drawCtx, band), {
            key: canvas.key,
            values: payload && payload.values ? payload.values : {},
            base: payload && payload.base ? payload.base : {},
            keys: payload && payload.keys ? payload.keys : [],
            touched: payload && typeof payload.touched === "number" ? payload.touched : -1,
            preset: (payload && payload.preset) || null,
            nowMs: payload && typeof payload.nowMs === "number" ? payload.nowMs : Date.now(),
            width: band.w, height: band.h,
            state: canvasPageState(s, cacheKey),
        });
    } catch (e) {
        canvasPageDisabled[cacheKey] = true;
        log("canvas", `canvas page ${cacheKey} disabled after throw: ${e}`);
    }
}
function loadCardScript(s, comp, scriptPath, exportRef) { // a parameter's card (card_script)
    const path = resolveCardScriptPath(s, comp, scriptPath);
    if (!path || !exportRef) return null;
    const r = loadKeepingGlobals(path, ["init", "tick", "onMidiMessageInternal", "onMidiMessageExternal", exportRef],
                                 () => globalThis[exportRef]);
    return r.ok && typeof r.result === "function" ? r.result : null;
}

// The keyboard a drawn page asks for (ctx.openTextEntry), Schwung's own
// (text_entry.mjs), its answer given to the page's onTextEntry, either way.
function openCanvasPageTextEntry(s, comp, canvas, req) {
    openTextEntry({
        title: req.title,
        initialText: req.initial,
        onAnnounce: announce,
        onConfirm: (text) => {
            canvasPageHook(s, comp, canvas, "onTextEntry", { text: String(text == null ? "" : text), cancelled: false });
            needsRedraw = true;
        },
        onCancel: () => {
            canvasPageHook(s, comp, canvas, "onTextEntry", { text: null, cancelled: true });
            needsRedraw = true;
        },
    });
    needsRedraw = true;
}

// ---------------------------------------------------------------- the warning card

// shadow_ui.js's message overlay: drawn over whatever is up, answered by a
// button from any screen, never by a turn (the turn that raised it would
// take it down).
let warningActive = false, warningTitle = "", warningLines = [];
function showWarning(title, message) {
    warningTitle = title;
    warningLines = wrapText(message, 18);
    warningActive = true;
    announce(`${title}: ${message}`);
    needsRedraw = true;
}
function dismissWarning() {
    warningActive = false;
    warningTitle = "";
    warningLines = [];
    needsRedraw = true;
}
function maybeDismissWarningFromInput(status, d1, d2) {
    if (!warningActive) return false;
    if ((status & 0xf0) !== 0xb0 || d2 <= 0) return false;
    if (d1 === CC_JOG || (d1 >= KNOB_CC_START && d1 <= KNOB_CC_END)) return false;
    dismissWarning();
    return true;
}

// ---------------------------------------------------------------- the option list

// shadow_ui.js's enum picker: one enum's options from the grid (a held enum
// knob clicked), nothing written until the click, Back a true cancel. The
// jog steps one an option, any knob through Schwung's list knob.
let enumPickerTitle = "", enumPickerOptions = [], enumPickerIndex = 0, enumPickerOpenIndex = 0;
let enumPickerCommit = null, enumPickerReturnToGrid = false, enumPickerKnob = listKnobInit();
function openEnumPicker(o) {
    const options = Array.isArray(o && o.options) ? o.options : [];
    if (!options.length) return false;
    enumPickerTitle = String((o && o.title) || "Options");
    enumPickerOptions = options.map((v) => String(v));
    const i = Math.round(Number(o && o.index));
    enumPickerIndex = isFinite(i) && i >= 0 && i < options.length ? i : 0;
    enumPickerOpenIndex = enumPickerIndex;
    enumPickerCommit = o && typeof o.commit === "function" ? o.commit : null;
    enumPickerReturnToGrid = !!(o && o.returnToGrid);
    enumPickerKnob = listKnobInit();
    setView(VIEWS.ENUM_PICKER);
    needsRedraw = true;
    announce(`${enumPickerTitle}, ${enumPickerOptions[enumPickerIndex]}, ${enumPickerIndex + 1} of ${enumPickerOptions.length}`);
    return true;
}
function closeEnumPicker(commit) {
    const title = enumPickerTitle, chosen = enumPickerOptions[enumPickerIndex], at = enumPickerIndex;
    const write = commit ? enumPickerCommit : null, toGrid = enumPickerReturnToGrid;
    enumPickerCommit = null;
    enumPickerOptions = [];
    enumPickerTitle = "";
    enumPickerIndex = enumPickerOpenIndex = 0;
    enumPickerReturnToGrid = false;
    if (write) write(at);
    if (toGrid && paramPagesActive()) setView(VIEWS.PARAM_PAGES);
    else leaveListEditor(); // Schwung: its list editor, which the grid's hand-off opened; Melodee: the grid
    needsRedraw = true;
    if (commit) announceParameter(title, chosen);
    else announce("Cancelled, " + title);
}
function enumPickerJog(delta) {
    if (!enumPickerOptions.length) return;
    enumPickerIndex = Math.max(0, Math.min(enumPickerOptions.length - 1, enumPickerIndex + delta));
    needsRedraw = true;
    announce(`${enumPickerOptions[enumPickerIndex]}, ${enumPickerIndex + 1} of ${enumPickerOptions.length}`);
}
function drawEnumPicker() {
    clear_screen();
    drawEnumList({ fillRect: fill_rect, print, textWidth: text_width }, {
        title: enumPickerTitle,
        headerRight: "SELECT",
        options: enumPickerOptions,
        index: enumPickerIndex,
        markIndex: enumPickerOpenIndex, // what is set: where Back leaves it
        footer: enumPickerFooterHints(),
    });
}

// ---------------------------------------------------------------- the list editor's part

// A cell the grid cannot edit itself (a string, a file, a drawn editor) is
// handed to shadow_ui.js's list editor: the grid is left, the list editor
// entered on the grid's level and instance, the parameter found and its
// editor opened over it; that editor's exit puts a new grid back on the same
// page. Melodee keeps what those editors read of the list editor (its
// component, level, instance and knob row) and never draws the list itself:
// from the grid, the editors cover it.
let hierSlot = -1, hierComponent = "", hierHierarchy = null, hierChainParams = [], hierLevel = "", hierChildIndex = -1;
let hierKnobs = []; // the knobs' keys: the grid page's row, holes kept
let paramEditorOpenedFromGrid = false, paramEditorReturnPage = "";

function parseJson(text) { try { return text ? JSON.parse(text) : null; } catch (e) { return null; } }
function hierLevelDef() {
    return hierHierarchy && hierHierarchy.levels && hierLevel ? hierHierarchy.levels[hierLevel] || null : null;
}
function buildHierarchyParamKey(key) {
    return `${prefixOf(hierComponent)}:${hierChildKeyFor(hierLevelDef(), hierChildIndex, key)}`;
}
function levelLists(def, bare) {
    return !!def && Array.isArray(def.params)
        && def.params.some((e) => (typeof e === "string" ? e === bare : !!(e && typeof e === "object" && !e.level && e.key === bare)));
}
function findLevelListingParam(bare) {
    const levels = hierHierarchy && hierHierarchy.levels;
    if (!levels) return null;
    for (const [name, def] of Object.entries(levels)) if (levelLists(def, bare)) return name;
    return null;
}
function getMetaOption(meta, key, fallback) {
    if (!meta || typeof meta !== "object") return fallback;
    if (meta[key] !== undefined) return meta[key];
    if (meta.options && !Array.isArray(meta.options) && typeof meta.options === "object" && meta.options[key] !== undefined)
        return meta.options[key];
    return fallback;
}
// A parameter's metadata as the list editor merges it: chain_params over the
// level's own declaration, then the expanded types shadow_ui.js builds
// (note and canvas here; rate and wav_position are not).
const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
function buildNoteParamMeta(meta) {
    const single = String(getMetaOption(meta, "mode", "multi")).toLowerCase() === "single";
    let lo = Math.max(0, Math.floor(parseMetaNumber(getMetaOption(meta, "min_note", meta.min), 0)));
    let hi = Math.min(127, Math.floor(parseMetaNumber(getMetaOption(meta, "max_note", meta.max), single ? 11 : 127)));
    if (hi < lo) [lo, hi] = [hi, lo];
    const options = [], optionLabels = {};
    for (let note = lo; note <= hi; note++) {
        const name = NOTE_NAMES[((note % 12) + 12) % 12];
        options.push(String(note));
        optionLabels[String(note)] = single ? name : `${name}${Math.floor(note / 12) - 1}`;
    }
    return { ...meta, type: "enum", options, option_labels: optionLabels, expanded_type: "note", note_mode: single ? "single" : "multi" };
}
function buildCanvasParamMeta(meta) {
    const shown = String(getMetaOption(meta, "display_value_type", "string")).toLowerCase();
    return {
        ...meta,
        type: "canvas",
        display_value_type: shown === "percent" || shown === "float" || shown === "int" ? shown : "string",
        show_footer: parseMetaBool(getMetaOption(meta, "show_footer", getMetaOption(meta, "showfooter", true))),
        show_value: parseMetaBool(getMetaOption(meta, "show_value", getMetaOption(meta, "showvalue", true))),
        expanded_type: "canvas",
    };
}
function normalizeExpandedParamMeta(key, meta) {
    if (!meta || typeof meta !== "object") return meta;
    const type = String(meta.type || "").toLowerCase();
    if (type === "note") return buildNoteParamMeta(meta);
    if (type === "canvas") return buildCanvasParamMeta(meta);
    return meta;
}
function getParamMetadata(key) {
    const chainMeta = Array.isArray(hierChainParams) ? hierChainParams.find((p) => p && p.key === key) || null : null;
    const def = hierLevelDef();
    const levelMeta = def && Array.isArray(def.params)
        ? def.params.find((p) => p && typeof p === "object" && p.key === key) || null : null;
    return normalizeExpandedParamMeta(key, chainMeta && levelMeta ? { ...levelMeta, ...chainMeta } : chainMeta || levelMeta);
}

// shadow_ui.js's enterHierarchyEditor, as far as these editors need it.
function enterHierarchyEditor(slotIndex, componentKey) {
    hideOverlay(); // dismissOverlayForHierarchyEntry
    pendingHierKnobIndex = -1;
    pendingHierKnobDelta = 0;
    const p = prefixOf(componentKey);
    hierSlot = slotIndex;
    hierComponent = componentKey;
    hierHierarchy = parseJson(getSlotParam(slotIndex, `${p}:ui_hierarchy`));
    hierChainParams = parseJson(getSlotParam(slotIndex, `${p}:chain_params`)) || [];
    hierLevel = hierHierarchy && hierHierarchy.levels && hierHierarchy.levels.root ? "root" : "";
    hierChildIndex = -1;
    const def = hierLevelDef();
    hierKnobs = def && Array.isArray(def.knobs) ? def.knobs.slice() : [];
    invalidateKnobContextCache();
    setView(VIEWS.HIERARCHY_EDITOR);
}
function exitHierarchyEditor() {
    hierSlot = -1;
    hierComponent = "";
    hierHierarchy = null;
    hierChainParams = [];
    hierLevel = "";
    hierChildIndex = -1;
    hierKnobs = [];
    filepathBrowserState = null;
    filepathBrowserParamKey = "";
    invalidateKnobContextCache();
    needsRedraw = true;
}
function returnToParamPagesFromEditor() {
    const s = hierSlot, comp = hierComponent || component, page = paramEditorReturnPage;
    paramEditorOpenedFromGrid = false;
    paramEditorReturnPage = "";
    exitHierarchyEditor();
    enterParamPages(s >= 0 ? s : slot, comp, prefixOf(comp), page, componentParamPagesIo(s >= 0 ? s : slot), null);
    needsRedraw = true;
}
function closeOwnViewEditorToCaller() {
    if (!paramEditorOpenedFromGrid) return false;
    returnToParamPagesFromEditor();
    return true;
}
// Where Schwung's list editor would be shown, which Melodee does not draw:
// the grid instead.
function leaveListEditor() {
    if (closeOwnViewEditorToCaller()) return;
    paramEditorOpenedFromGrid = true;
    returnToParamPagesFromEditor();
}

function openParamEditorFromGrid(slotIndex, fullKey, meta) {
    // The editors Melodee draws: the keyboard, a module's drawn editor, the
    // file browser. Schwung's others are its list editor's edit mode (a
    // wav_position's waveform editor among them), not drawn here: the click
    // leaves the grid as it is.
    if (!["string", "canvas", "filepath"].includes(String((meta && meta.type) || "").toLowerCase())) return;
    const componentKey = paramPagesComponent();
    // What the grid was on, read before it goes.
    const page = currentParamPage();
    const level = page && page.level;
    const childAt = level ? paramPagesChildIndex(level) : -1;
    paramEditorReturnPage = (page && page.name) || "";
    const knobsFromPage = page && Array.isArray(page.keys) ? page.keys.slice() : null;
    const paramPrefix = `${prefixOf(componentKey)}:`;
    const raw = String(fullKey || "");
    let bare = raw.startsWith(paramPrefix) ? raw.slice(paramPrefix.length) : raw.replace(/^[^:]+:/, "");
    exitParamPages();
    enterHierarchyEditor(slotIndex, componentKey);
    if (level && hierHierarchy && hierHierarchy.levels && hierHierarchy.levels[level] && level !== hierLevel) {
        hierLevel = level;
        hierChildIndex = childKeys.hasChildren(hierHierarchy.levels[level]) ? childAt : -1;
    }
    bare = hierGenericKeyFor(hierLevelDef(), hierChildIndex, bare);
    let found = levelLists(hierLevelDef(), bare);
    if (!found) {
        const owner = findLevelListingParam(bare);
        if (owner && owner !== hierLevel) {
            hierLevel = owner;
            hierChildIndex = -1;
            found = levelLists(hierLevelDef(), bare);
        }
    }
    if (!found) {
        // Schwung leaves you in its list editor here, saying so.
        announce((meta && meta.label ? meta.label : "Parameter") + ", opening in list");
        paramEditorOpenedFromGrid = true;
        returnToParamPagesFromEditor();
        return;
    }
    if (knobsFromPage) hierKnobs = knobsFromPage; // the page's knob order, on the level landed on
    invalidateKnobContextCache();
    const liveMeta = getParamMetadata(bare) || meta;
    announce((liveMeta && (liveMeta.name || liveMeta.label)) || bare);
    paramEditorOpenedFromGrid = true;
    openHierarchyParamEditor(bare, liveMeta);
    needsRedraw = true;
}

// One parameter's editor, by its type, as shadow_ui.js opens it from the
// grid (forced open).
function openHierarchyParamEditor(selectedKey, meta) {
    if (meta && meta.type === "string") {
        const fullKey = buildHierarchyParamKey(selectedKey);
        const currentText = getSlotParam(hierSlot, fullKey) || "";
        const s = hierSlot;
        openTextEntry({
            title: meta.name || selectedKey,
            initialText: String(currentText),
            onAnnounce: announce,
            onConfirm: (nextText) => {
                setSlotParam(s, fullKey, String(nextText || ""));
                announceParameter(meta.name || selectedKey, String(nextText || ""));
                needsRedraw = true;
                closeOwnViewEditorToCaller(); // back on the knob page the click came from
            },
            onCancel: () => {
                needsRedraw = true;
                closeOwnViewEditorToCaller();
            },
        });
        return;
    }
    if (meta && meta.type === "canvas") {
        openCanvasPreview(selectedKey, meta);
        needsRedraw = true;
        return;
    }
    if (meta && meta.type === "filepath") {
        if (!openHierarchyFilepathBrowser(selectedKey, meta)) leaveListEditor();
        return;
    }
    if (meta && meta.type === "enum" && Array.isArray(meta.options) && meta.options.length > 0 && meta.options.length !== 2
        && !isTriggerParam(meta) && !isReadoutParam(meta)) {
        const fullKey = buildHierarchyParamKey(selectedKey);
        const currentVal = getSlotParam(hierSlot, fullKey);
        const nameIdx = meta.options.indexOf(currentVal);
        const usesIndex = nameIdx < 0;
        let index = nameIdx;
        if (usesIndex) {
            const parsed = parseInt(currentVal, 10);
            index = !isNaN(parsed) && parsed >= 0 && parsed < meta.options.length ? parsed : 0;
        }
        const s = hierSlot;
        openEnumPicker({
            title: meta.name || meta.label || selectedKey,
            options: meta.options,
            index,
            commit: (i) => setSlotParam(s, fullKey, usesIndex ? String(i) : meta.options[i]),
            returnToGrid: false,
        });
        return;
    }
    // Everything else (a wav_position's editor among them) is the list
    // editor's edit mode, which Melodee does not draw: the grid again.
    leaveListEditor();
}
function isTriggerParam(meta) { return !!(meta && String(meta.access || "").toLowerCase() === "write"); }
function isReadoutParam(meta) { return !!(meta && String(meta.access || "").toLowerCase() === "read"); }
function isTriggerEnumMeta(meta) {
    return !!(meta && meta.type === "enum" && Array.isArray(meta.options) && meta.options.length === 2
              && meta.options[0] === "idle" && meta.options[1] === "trigger");
}

// ---------------------------------------------------------------- values as the list editor shows them

function formatMetaOptionValue(meta, rawValue) {
    if (!meta) return rawValue;
    const lookup = String(rawValue);
    if (meta.option_labels && typeof meta.option_labels === "object"
        && Object.prototype.hasOwnProperty.call(meta.option_labels, lookup)) return meta.option_labels[lookup];
    if (Array.isArray(meta.options)) {
        if (meta.options.indexOf(lookup) >= 0) return lookup;
        const num = Number(lookup);
        if (Number.isFinite(num)) {
            const idx = Math.round(num);
            if (idx >= 0 && idx < meta.options.length) return meta.options[idx];
        }
    }
    return rawValue;
}
function formatWavPositionDisplayValue(rawValue, meta) {
    const num = Number(rawValue);
    if (!Number.isFinite(num)) return rawValue;
    const unit = String((meta && meta.display_unit) || "percent").toLowerCase();
    if (unit === "ms") return `${Math.round(num)} ms`;
    if (unit === "sec" || unit === "s") return `${num.toFixed(3)} s`;
    const lo = parseMetaNumber(meta && meta.min, 0), span = parseMetaNumber(meta && meta.max, 1) - lo;
    if (span <= 0) return "0%";
    return `${Math.max(0, Math.min(100, Math.round(((num - lo) / span) * 100)))}%`;
}
function formatCanvasDisplayValue(rawValue, meta) {
    const mode = String((meta && meta.display_value_type) || "string").toLowerCase();
    if (mode === "string") return String(rawValue || "");
    const num = Number(rawValue);
    if (!Number.isFinite(num)) return String(rawValue || "");
    if (mode === "int") return String(Math.round(num));
    if (mode === "percent") {
        const lo = parseMetaNumber(meta && meta.min, 0), span = parseMetaNumber(meta && meta.max, 1) - lo;
        if (span <= 0) return "0%";
        return `${Math.max(0, Math.min(100, Math.round(((num - lo) / span) * 100)))}%`;
    }
    return num.toFixed(2);
}
function formatParamForSet(val, meta) {
    if (meta && meta.ui_type === "wav_position") {
        if (meta.display_unit === "ms") return Math.round(val).toString();
        return Number(val).toFixed(2); // Schwung's precision follows the step and zoom; the plain case here
    }
    return ufFormatParamForSet(val, meta);
}
function formatParamForOverlay(val, meta) {
    if (meta && meta.ui_type === "wav_position") return formatWavPositionDisplayValue(val, meta);
    if (meta && meta.type === "canvas" && meta.show_value === false) return "";
    if (meta && meta.type === "canvas") return formatCanvasDisplayValue(val, meta);
    if (meta && (meta.type === "enum" || meta.type === "bool") && meta.picker_type && (val === "" || val === null || val === undefined))
        return meta.none_label || "(none)";
    if (meta && (meta.type === "enum" || meta.type === "bool")) return String(formatMetaOptionValue(meta, val));
    if (meta && (meta.unit || meta.display_format)) return ufFormatParamValue(val, meta);
    if (meta && meta.type === "int") return Math.round(val).toString();
    const lo = meta && typeof meta.min === "number" ? meta.min : 0, hi = meta && typeof meta.max === "number" ? meta.max : 1;
    if (lo === 0 && hi >= 1 && hi <= 4) return Math.round(val * 100) + "%";
    return Number(val).toFixed(2);
}
function formatHierDisplayValue(key, val) {
    const meta = getParamMetadata(key);
    if (meta && meta.type === "enum") {
        if (meta.picker_type && (val === "" || val === null || val === undefined)) return meta.none_label || "(none)";
        return formatMetaOptionValue(meta, val);
    }
    if (meta && meta.type === "filepath") {
        if (!val) return "";
        const at = val.lastIndexOf("/");
        return at >= 0 ? val.slice(at + 1) : val;
    }
    if (meta && meta.ui_type === "wav_position") return formatWavPositionDisplayValue(val, meta);
    if (meta && meta.type === "canvas" && meta.show_value === false) return "";
    if (meta && meta.type === "canvas") return formatCanvasDisplayValue(val, meta);
    if (meta && meta.type === "string") return String(val || "");
    const num = parseFloat(val);
    if (isNaN(num)) return val;
    if (meta && (meta.unit || meta.display_format)) return ufFormatParamValue(num, meta);
    if (meta && meta.type === "float") {
        const lo = typeof meta.min === "number" ? meta.min : 0, hi = typeof meta.max === "number" ? meta.max : 1;
        if (lo === 0 && hi === 1) return Math.round(num * 100) + "%";
    }
    if (meta && meta.type === "int") return Math.round(num).toString();
    return num.toFixed(2);
}

// ---------------------------------------------------------------- the file browser

// shadow_ui.js's filepath browser (filepath_browser.mjs's model): the jog
// and any knob move, the click opens a folder or picks the file, Back
// leaves; a module's browser_hooks and live preview as it runs them.
let filepathBrowserState = null, filepathBrowserParamKey = "", filepathBrowserKnob = listKnobInit();
const FILEPATH_BROWSER_FS = {
    readdir(path) {
        const entries = os.readdir(path) || [];
        if (Array.isArray(entries[0])) return entries[0];
        return Array.isArray(entries) ? entries : [];
    },
    stat(path) { return os.stat(path); },
};
function normalizeFilepathHookActions(rawActions, prefix) {
    if (!Array.isArray(rawActions)) return [];
    const out = [];
    for (const action of rawActions) {
        if (!action || typeof action !== "object") continue;
        const rawKey = typeof action.key === "string" ? action.key.trim() : "";
        if (!rawKey) continue;
        out.push({
            key: rawKey.includes(":") ? rawKey : prefix ? `${prefix}:${rawKey}` : rawKey,
            value: action.value === undefined || action.value === null ? "" : String(action.value),
            restore: parseMetaBool(action.restore),
        });
    }
    return out;
}
function buildFilepathBrowserHooks(meta, prefix) {
    const hooks = meta && meta.browser_hooks && typeof meta.browser_hooks === "object" ? meta.browser_hooks : {};
    return {
        onOpen: normalizeFilepathHookActions(hooks.on_open, prefix),
        onPreview: normalizeFilepathHookActions(hooks.on_preview, prefix),
        onCancel: normalizeFilepathHookActions(hooks.on_cancel, prefix),
        onCommit: normalizeFilepathHookActions(hooks.on_commit, prefix),
    };
}
function resolveFilepathHookValue(rawValue, context) {
    const value = rawValue === undefined || rawValue === null ? "" : String(rawValue);
    if (value === "$path" || value === "$selected_path") return context && context.path ? String(context.path) : "";
    if (value === "$filename" || value === "$selected_filename") {
        if (!context || !context.path) return "";
        const path = String(context.path), at = path.lastIndexOf("/");
        return at >= 0 ? path.slice(at + 1) : path;
    }
    return value;
}
function applyFilepathHookActions(state, actions, context) {
    if (!state || !Array.isArray(actions) || !actions.length) return;
    for (const action of actions) {
        if (!action || !action.key) continue;
        if (action.restore && state.hookRestoreValues && !Object.prototype.hasOwnProperty.call(state.hookRestoreValues, action.key)) {
            const prev = getSlotParam(hierSlot, action.key);
            if (prev !== null && prev !== undefined) state.hookRestoreValues[action.key] = String(prev);
        }
        setSlotParam(hierSlot, action.key, resolveFilepathHookValue(action.value, context));
    }
}
function restoreFilepathHookActions(state) {
    if (!state || !state.hookRestoreValues) return;
    for (const [key, prev] of Object.entries(state.hookRestoreValues)) setSlotParam(hierSlot, key, prev);
}
function applyLivePreview(state, selected) {
    if (!state || !state.livePreviewEnabled || !selected || selected.kind !== "file" || !selected.path) return;
    if (selected.path === state.previewCurrentValue || !state.previewParamFullKey) return;
    if (setSlotParam(hierSlot, state.previewParamFullKey, selected.path)) {
        state.previewCurrentValue = selected.path;
        applyFilepathHookActions(state, state.hooksOnPreview, { path: selected.path });
    }
}
function filepathBrowserJog(delta) {
    if (!filepathBrowserState) return;
    moveFilepathBrowserSelection(filepathBrowserState, delta);
    const selected = filepathBrowserState.items[filepathBrowserState.selectedIndex];
    if (filepathBrowserState.livePreviewEnabled && selected && selected.kind === "file" && selected.path) {
        filepathBrowserState.previewPendingPath = selected.path;
        filepathBrowserState.previewPendingTime = Date.now();
    } else if (filepathBrowserState.livePreviewEnabled) {
        filepathBrowserState.previewPendingPath = "";
        filepathBrowserState.previewPendingTime = 0;
    }
    if (selected) announceMenuItem(selected.label || "File", "");
}
function openHierarchyFilepathBrowser(key, meta) {
    hierChainParams = parseJson(getSlotParam(hierSlot, `${prefixOf(hierComponent)}:chain_params`)) || hierChainParams;
    const effectiveMeta = getParamMetadata(key) || meta;
    if (!effectiveMeta || effectiveMeta.type !== "filepath") return false;
    const fullKey = buildHierarchyParamKey(key);
    const currentVal = getSlotParam(hierSlot, fullKey) || "";
    filepathBrowserParamKey = key;
    const st = buildFilepathBrowserState(effectiveMeta, currentVal);
    filepathBrowserState = st;
    st.livePreviewEnabled = parseMetaBool(effectiveMeta.live_preview);
    st.previewOriginalValue = currentVal;
    st.previewCurrentValue = currentVal;
    st.previewCommitted = false;
    st.previewParamFullKey = fullKey;
    st.previewPendingPath = "";
    st.previewPendingTime = 0;
    st.previewSelectedPath = "";
    const hooks = buildFilepathBrowserHooks(effectiveMeta, prefixOf(hierComponent));
    st.hooksOnOpen = hooks.onOpen;
    st.hooksOnPreview = hooks.onPreview;
    st.hooksOnCancel = hooks.onCancel;
    st.hooksOnCommit = hooks.onCommit;
    st.hookRestoreValues = {};
    filepathBrowserKnob = listKnobInit();
    applyFilepathHookActions(st, st.hooksOnOpen, { path: currentVal });
    refreshFilepathBrowser(st, FILEPATH_BROWSER_FS);
    if (st.livePreviewEnabled) applyLivePreview(st, st.items[st.selectedIndex]);
    setView(VIEWS.FILEPATH_BROWSER);
    if (st.items.length) announceMenuItem(st.items[st.selectedIndex].label || "File", "");
    else announce("No files found");
    return true;
}
function closeHierarchyFilepathBrowser() {
    const state = filepathBrowserState;
    if (state) {
        const committed = !!state.previewCommitted;
        if (state.livePreviewEnabled && !committed && state.previewParamFullKey && state.previewCurrentValue !== state.previewOriginalValue)
            setSlotParam(hierSlot, state.previewParamFullKey, state.previewOriginalValue || "");
        if (committed) applyFilepathHookActions(state, state.hooksOnCommit, { path: state.previewSelectedPath || state.previewCurrentValue || "" });
        else applyFilepathHookActions(state, state.hooksOnCancel, { path: state.previewOriginalValue || "" });
        restoreFilepathHookActions(state);
    }
    filepathBrowserState = null;
    filepathBrowserParamKey = "";
    leaveListEditor();
}
function filepathBrowserSelect() {
    if (!filepathBrowserState) { closeHierarchyFilepathBrowser(); return; }
    const st = filepathBrowserState;
    const result = activateFilepathBrowserItem(st);
    if (result.action === "open") {
        refreshFilepathBrowser(st, FILEPATH_BROWSER_FS);
        if (st.livePreviewEnabled) {
            st.previewPendingPath = "";
            st.previewPendingTime = 0;
            applyLivePreview(st, st.items[st.selectedIndex]);
        }
    } else if (result.action === "select") {
        const key = filepathBrowserParamKey || result.key;
        if (st.livePreviewEnabled) {
            st.previewCommitted = true;
            st.previewCurrentValue = result.value || "";
            st.previewPendingPath = "";
            st.previewPendingTime = 0;
        }
        st.previewSelectedPath = result.value || "";
        setSlotParam(hierSlot, buildHierarchyParamKey(key), result.value || "");
        // Schwung then fills a linked wav_position's end marker from the
        // file's length (applyLinkedWavEndDefaultsForFilepath); not here.
        announceParameter(st.title || key, result.filename || result.value || "");
        closeHierarchyFilepathBrowser();
    }
}
function drawFilepathBrowser() {
    clear_screen();
    const state = filepathBrowserState;
    drawHeader(truncateText(state && state.title ? state.title : "File Browser", 24));
    if (!state) {
        print(4, LIST_TOP_Y, "Browser unavailable", 1);
        drawFooter(["Click: return", "Jog: scroll"]);
        return;
    }
    if (state.error) print(4, LIST_TOP_Y, truncateText(state.error, 20), 1);
    if (!state.items || !state.items.length) {
        print(4, LIST_TOP_Y + 10, "No files", 1);
    } else {
        drawMenuList({
            items: state.items,
            selectedIndex: state.selectedIndex,
            listArea: { topY: LIST_TOP_Y, bottomY: FOOTER_RULE_Y },
            getLabel: (item) => item.label,
            getValue: () => "",
        });
    }
    const selected = state.items && state.items.length ? state.items[state.selectedIndex] : null;
    drawFooter([selected && selected.kind === "file" ? "Click: select" : "Click: open", "Jog: scroll"]);
}

// ---------------------------------------------------------------- a file cell turned

// shadow_ui.js's file list for a turned file cell: the files in the current
// file's folder, the knob scrolls them, letting go (or a second still)
// loads the one it is on, Back leaves it as it was.
const FLAT_IDLE_COMMIT_MS = 1000; // shadow_ui.js's LFO_TARGET_KNOB_IDLE_COMMIT_MS
let fileFlatRows = [], fileFlatIndex = 0, fileFlatStored = -1, fileFlatKey = "", fileFlatFolder = "";
let fileFlatKnobState = listKnobInit(), fileFlatKnob = -1, fileFlatHeld = false, fileFlatLastMs = 0, fileFlatMoved = false;
let fileFlatSlot = -1, fileFlatFullKey = "", fileFlatMeta = null;
const flatRefusal = createRefusalLatch(FLAT_IDLE_COMMIT_MS);
function fileFlatEnter(slotIndex, fullKey, knob, held, info) {
    const meta = info && info.meta;
    if (!meta || meta.type !== "filepath" || !info.key) return false;
    const now = Date.now();
    if (flatRefusal.latched(fullKey, now)) return true;
    const current = getSlotParam(slotIndex, fullKey);
    let files = [], st = null;
    if (current !== null) {
        st = buildFilepathBrowserState(meta, current);
        refreshFilepathBrowser(st, FILEPATH_BROWSER_FS);
        files = (st.items || []).filter((it) => it && it.kind === "file");
    }
    const plan = planFileFlatOpen(current, files);
    if (plan.refuse) {
        flatRefusal.refuse(fullKey, now);
        announce(plan.refuse === "empty" ? "No files in this folder" : "File unavailable");
        return true;
    }
    fileFlatRows = files;
    fileFlatStored = plan.stored;
    fileFlatIndex = plan.index;
    fileFlatMoved = false;
    fileFlatKey = info.key;
    fileFlatSlot = slotIndex;
    fileFlatFullKey = String(fullKey || "");
    fileFlatMeta = meta;
    const dir = String(st.currentDir || "").replace(/\/+$/, "");
    fileFlatFolder = dir.slice(dir.lastIndexOf("/") + 1) || meta.name || info.key;
    fileFlatKnobState = listKnobInit();
    fileFlatKnob = knob;
    fileFlatHeld = !!held;
    fileFlatLastMs = Date.now();
    clearParamPagesTouch();
    setView(VIEWS.FILE_FLAT);
    announceMenuItem(fileFlatRows[fileFlatIndex].label + ", " + fileFlatFolder);
    needsRedraw = true;
    return true;
}
function fileFlatMove(n) {
    if (!n || !fileFlatRows.length) return;
    const before = fileFlatIndex;
    fileFlatIndex = Math.max(0, Math.min(fileFlatRows.length - 1, fileFlatIndex + n));
    if (fileFlatIndex !== before) {
        fileFlatMoved = true;
        announceMenuItem(fileFlatRows[fileFlatIndex].label);
    }
    needsRedraw = true;
}
function fileFlatKnobTurn(delta) {
    fileFlatLastMs = Date.now();
    fileFlatMove(listKnobStep(fileFlatKnobState, delta, fileFlatLastMs, fileFlatRows.length));
}
function fileFlatClose() {
    fileFlatKnob = -1;
    fileFlatHeld = false;
    if (paramPagesActive()) setView(VIEWS.PARAM_PAGES);
    needsRedraw = true;
}
function fileFlatCommit() {
    const f = fileFlatPick(fileFlatRows, fileFlatIndex, fileFlatStored, fileFlatMoved);
    if (f) {
        commitParamPagesValue(fileFlatKey, f.path);
        fileFlatAfterCommit(f.path);
        announce("Loaded " + f.label);
    }
    fileFlatClose();
}
function fileFlatAfterCommit(path) { // the browser's on_commit hooks, those not undone at its close
    const key = fileFlatKey, fullKey = fileFlatFullKey;
    const prefix = fullKey.endsWith(":" + key) ? fullKey.slice(0, -(key.length + 1)) : "";
    for (const action of buildFilepathBrowserHooks(fileFlatMeta, prefix).onCommit) {
        if (!action || !action.key || action.restore) continue;
        setSlotParam(fileFlatSlot, action.key, resolveFilepathHookValue(action.value, { path }));
    }
}
function fileFlatCancel() {
    announce("File unchanged");
    fileFlatClose();
}
function fileFlatTick() {
    if (fileFlatKnob < 0) return;
    if (view !== VIEWS.FILE_FLAT) { fileFlatKnob = -1; return; }
    if (!fileFlatHeld && Date.now() - fileFlatLastMs >= FLAT_IDLE_COMMIT_MS) fileFlatCommit();
}
function drawFileFlat() {
    clear_screen();
    drawHeader(truncateText(fileFlatFolder, 22));
    drawMenuList({
        items: fileFlatRows,
        selectedIndex: fileFlatIndex,
        getLabel: (item) => item.label,
        getValue: (item, i) => (i === fileFlatStored ? "*" : ""),
        listArea: { topY: LIST_TOP_Y, bottomY: FOOTER_RULE_Y },
        valueAlignRight: true,
        announce: false,
    });
    drawFooter(["Release: load", "Back: cancel"]);
}

// ---------------------------------------------------------------- a drawn editor dived into

// shadow_ui.js's canvas view: a canvas parameter that is not a page of its
// own, opened from its cell, the module's script owning the whole screen
// (its draw, tick and onMidi hooks); the click or Back closes it unless it
// is enterable, then Back asks it first; Shift + the wheel always leaves.
let canvasParamKey = "", canvasParamMeta = null, canvasRuntime = null, canvasTickCounter = 0;
const CANVAS_HOOK_ABSENT = {};
const DRAW_PATH_HOOKS = new Set(["draw", "tick"]); // their ctx reads and writes nothing
function resetCanvasState() {
    canvasParamKey = "";
    canvasParamMeta = null;
    canvasRuntime = null;
    canvasTickCounter = 0;
}
function parseOverlayScriptSpec(value, fallbackScript) {
    const fallback = typeof fallbackScript === "string" && fallbackScript.trim() ? fallbackScript.trim() : "";
    const rawSpec = typeof value === "string" && value.trim() ? value.trim() : fallback;
    if (!rawSpec) return { scriptRef: "", overlayRef: "" };
    const hash = rawSpec.indexOf("#");
    if (hash < 0) return { scriptRef: rawSpec, overlayRef: "" };
    return { scriptRef: rawSpec.slice(0, hash).trim() || fallback, overlayRef: rawSpec.slice(hash + 1).trim() };
}
function resolveCanvasScriptPath(meta) {
    if (!moduleDir) return { scriptPath: "", overlayRef: "" };
    let scriptSpec = "canvas.js", overlayRef = "";
    const cs = meta ? meta.canvas_script : undefined;
    if (typeof cs === "string") scriptSpec = cs.trim() || "canvas.js";
    else if (cs && typeof cs === "object") {
        scriptSpec = String(cs.script || cs.file || cs.path || "canvas.js");
        for (const name of ["overlay", "target", "entry", "element"])
            if (typeof cs[name] === "string" && cs[name].trim()) { overlayRef = cs[name].trim(); break; }
    }
    for (const name of ["canvas_overlay", "overlay", "canvas_target"])
        if (meta && typeof meta[name] === "string" && meta[name].trim()) { overlayRef = meta[name].trim(); break; }
    const parsed = parseOverlayScriptSpec(scriptSpec, "canvas.js");
    if (!overlayRef && parsed.overlayRef) overlayRef = parsed.overlayRef;
    const scriptPath = parsed.scriptRef.startsWith("/") ? parsed.scriptRef : `${moduleDir}/${parsed.scriptRef}`;
    return { scriptPath: moduleFileExists(scriptPath) ? scriptPath : "", overlayRef };
}
function createCanvasRuntimeContext() {
    const fullCanvasKey = canvasParamKey ? buildHierarchyParamKey(canvasParamKey) : "";
    const prefix = prefixOf(hierComponent);
    const toFullParam = (key) => (!key || typeof key !== "string" ? "" : key.includes(":") ? key : prefix ? `${prefix}:${key}` : key);
    const rt = canvasRuntime;
    return {
        width: SCREEN_WIDTH,
        height: SCREEN_HEIGHT,
        state: canvasRuntime ? canvasRuntime.state : {},
        clear() { clear_screen(); },
        setPixel(x, y, value) { set_pixel(Math.round(x), Math.round(y), value ? 1 : 0); },
        drawRect(x, y, w, h, value) { draw_rect(Math.round(x), Math.round(y), Math.round(w), Math.round(h), value ? 1 : 0); },
        fillRect(x, y, w, h, value) { fill_rect(Math.round(x), Math.round(y), Math.round(w), Math.round(h), value ? 1 : 0); },
        drawLine(x1, y1, x2, y2, value) { draw_line(Math.round(x1), Math.round(y1), Math.round(x2), Math.round(y2), value ? 1 : 0); },
        print(x, y, text, color = 1) { print(Math.round(x), Math.round(y), String(text), color ? 1 : 0); },
        measureText(text) { return text_width(String(text == null ? "" : text)); },
        now() { return Date.now(); },
        random() { return Math.random(); },
        shiftHeld() { return isShiftHeld(); },
        close() { if (rt) rt.closeRequested = true; return true; },
        getValue() { return fullCanvasKey ? getSlotParam(hierSlot, fullCanvasKey) || "" : ""; },
        setValue(value) { return fullCanvasKey ? setSlotParam(hierSlot, fullCanvasKey, String(value)) : false; },
        getParam(key) { const full = toFullParam(key); return full ? getSlotParam(hierSlot, full) : null; },
        setParam(key, value) { const full = toFullParam(key); return full ? setSlotParam(hierSlot, full, String(value)) : false; },
        sourcePath() { return canvasRuntime ? canvasRuntime.scriptPath || "" : ""; },
    };
}
function canvasHookCtx(hookName) {
    const base = canvasRuntime.ctx;
    if (!DRAW_PATH_HOOKS.has(hookName)) return base;
    if (!canvasRuntime.drawCtx) {
        const { getParam, setParam, getValue, setValue, close, ...rest } = base;
        canvasRuntime.drawCtx = rest;
    }
    return canvasRuntime.drawCtx;
}
function canvasOverlayHookResult(hookName, payload) { // one throw and the script is done for
    if (!canvasRuntime || !canvasRuntime.overlay || canvasRuntime.hookDisabled) return CANVAS_HOOK_ABSENT;
    const fn = canvasRuntime.overlay[hookName];
    if (typeof fn !== "function") return CANVAS_HOOK_ABSENT;
    try {
        return fn(canvasHookCtx(hookName), payload || {});
    } catch (e) {
        canvasRuntime.error = `${hookName} error: ${e}`;
        canvasRuntime.hookDisabled = true;
        log("canvas", `canvas overlay disabled after throw in ${hookName}: ${e}`);
        return CANVAS_HOOK_ABSENT;
    }
}
function invokeCanvasOverlayHook(hookName, payload) { return canvasOverlayHookResult(hookName, payload) !== CANVAS_HOOK_ABSENT; }
function canvasIsEnterable() { return !!getMetaOption(canvasParamMeta, "enterable", false); }
function dispatchCanvasMidi(data, source) {
    if (view !== VIEWS.CANVAS) return false;
    const m = Array.isArray(data) ? data.slice() : Array.from(data || []);
    if (!m.length) return true;
    invokeCanvasOverlayHook("onMidi", { source, data: m });
    consumeCanvasCloseRequest();
    return true;
}
function openCanvasPreview(paramKey, meta) {
    resetCanvasState();
    canvasParamKey = paramKey || "";
    canvasParamMeta = meta || null;
    canvasRuntime = {
        moduleId, scriptPath: "", overlayRef: "", overlay: null, state: {}, ctx: null,
        liveKeys: meta && Array.isArray(meta.extra_keys) ? meta.extra_keys.slice(0, 4) : [],
        liveIntervalMs: meta && Number(meta.fullscreen_live_ms) > 0 ? Math.max(50, Number(meta.fullscreen_live_ms)) : 0,
        lastLiveReadMs: 0, liveCursor: 0, liveValues: {}, error: "",
    };
    const spec = resolveCanvasScriptPath(meta);
    canvasRuntime.scriptPath = spec.scriptPath;
    canvasRuntime.overlayRef = spec.overlayRef || "";
    if (canvasRuntime.scriptPath) {
        const loaded = loadCanvasOverlayScript(canvasRuntime.scriptPath, canvasRuntime.overlayRef);
        canvasRuntime.overlay = loaded.overlay;
        canvasRuntime.error = loaded.error || "";
    } else {
        canvasRuntime.error = "No canvas script found";
    }
    canvasRuntime.ctx = createCanvasRuntimeContext();
    invokeCanvasOverlayHook("onOpen", {
        param_key: canvasParamKey, module_id: canvasRuntime.moduleId,
        script_path: canvasRuntime.scriptPath, overlay_ref: canvasRuntime.overlayRef,
    });
    setView(VIEWS.CANVAS);
    hideOverlay();
    announce(`${meta && (meta.label || meta.name) ? meta.label || meta.name : "Canvas"} canvas`);
    needsRedraw = true;
    consumeCanvasCloseRequest();
}
function closeCanvasPreview(cancelled) {
    host_pad_observe(0);
    invokeCanvasOverlayHook("onClose", { cancelled: !!cancelled });
    invokeCanvasOverlayHook("onExit", { cancelled: !!cancelled });
    resetCanvasState();
    leaveListEditor();
    needsRedraw = true;
}
function consumeCanvasCloseRequest() { // a ctx.close() made in the hook that just returned
    if (!canvasRuntime || !canvasRuntime.closeRequested) return false;
    canvasRuntime.closeRequested = false;
    closeCanvasPreview(false);
    announce("Hierarchy Editor");
    needsRedraw = true;
    return true;
}
function tickCanvasLiveValues() { // the extra keys read one a tick, handed over once all have answered
    const rt = canvasRuntime;
    if (!rt || !rt.liveIntervalMs || !rt.liveKeys.length || !rt.ctx) return;
    if (!rt.overlay || rt.hookDisabled || typeof rt.overlay.onValues !== "function") return;
    const now = Date.now();
    if (rt.liveCursor === 0) {
        if (now - rt.lastLiveReadMs < rt.liveIntervalMs) return;
        rt.lastLiveReadMs = now;
        rt.liveValues = {};
    }
    const key = rt.liveKeys[rt.liveCursor];
    rt.liveValues[key] = rt.ctx.getParam(key);
    if (++rt.liveCursor < rt.liveKeys.length) return;
    rt.liveCursor = 0;
    invokeCanvasOverlayHook("onValues", { values: rt.liveValues, nowMs: now });
}
function tickCanvasPreview() {
    if (view !== VIEWS.CANVAS) return;
    const ov = canvasRuntime && canvasRuntime.overlay;
    host_pad_observe(ov && ov.wantsPads ? 1 : 0);
    tickCanvasLiveValues();
    invokeCanvasOverlayHook("tick", {});
    consumeCanvasCloseRequest();
}
function drawCanvasPreview() {
    clear_screen();
    const drew = invokeCanvasOverlayHook("draw", {});
    const title = canvasParamMeta && (canvasParamMeta.label || canvasParamMeta.name)
        ? String(canvasParamMeta.label || canvasParamMeta.name) : "Canvas";
    if (!drew) {
        const message = canvasRuntime && canvasRuntime.error ? canvasRuntime.error : "No module canvas overlay";
        print(Math.max(0, Math.floor((SCREEN_WIDTH - title.length * 5) / 2)), 10, truncateText(title, 24), 1);
        print(3, 29, truncateText(message, 24), 1);
        print(3, 50, canvasIsEnterable() ? "Back: return" : "Click/Back: return", 1);
    }
    const showValue = !canvasParamMeta || canvasParamMeta.show_value !== false;
    let valueText = showValue ? "-" : "";
    if (showValue && canvasParamKey) {
        const rawValue = getSlotParam(hierSlot, buildHierarchyParamKey(canvasParamKey));
        if (rawValue !== null && rawValue !== undefined && rawValue !== "") valueText = formatHierDisplayValue(canvasParamKey, rawValue);
    }
    if (!canvasParamMeta || canvasParamMeta.show_footer !== false)
        drawFooter([truncateText(String(valueText || "-"), 20), truncateText(title, 12)]);
}

// ---------------------------------------------------------------- the entry gate

// shadow_ui.js's openComponentEditor: a module whose contract has not come
// yet (no ui_hierarchy answer, or none and is_loading "1") holds on its
// "Loading..." screen and is asked again on Schwung's cadence; one that
// declares none gets the component editor's preset browser.
let componentLoadHold = null;
function componentEntryReader(s, comp) {
    const p = prefixOf(comp);
    return {
        hierarchy: () => getSlotParam(s, `${p}:ui_hierarchy`),
        module: () => getSlotParam(s, `${p}_module`),
        isLoading: () => getSlotParam(s, `${p}:is_loading`),
    };
}
function openComponentEditor(s, comp, entry) {
    const h = componentLoadHold;
    const attempts = h && h.slot === s && h.componentKey === comp ? h.attempts : 0;
    const decision = decideComponentEntry(componentEntryReader(s, comp), parseJson, attempts);
    if (decision.action === ENTRY_HOLD) {
        if (!(h && h.slot === s && h.componentKey === comp)) {
            componentLoadHold = { slot: s, componentKey: comp, attempts: 0, ticks: 0, entry };
            announce(`${componentLoadHoldLabel()}, loading`);
        }
        setView(VIEWS.COMPONENT_LOADING);
        needsRedraw = true;
        return;
    }
    componentLoadHold = null;
    if (decision.action === ENTRY_ENTER) {
        hideOverlay();
        enterParamPages(s, comp, prefixOf(comp), entry && entry.page || null, componentParamPagesIo(s), null,
                        entry && entry.page ? { enter: !!entry.enter } : undefined);
        return;
    }
    enterComponentEditFallback(s, comp, entry);
}
function componentLoadHoldLabel() { // the module the position holds now (a swap's, while it loads)
    if (!componentLoadHold) return "";
    return positionModule ? getModuleAbbrev(positionModule) : String(componentLoadHold.componentKey || "").toUpperCase();
}
function serviceComponentLoadHold() {
    if (!componentLoadHold || view !== VIEWS.COMPONENT_LOADING) return;
    const h = componentLoadHold;
    if (h.handover) return; // a swap's: the new module's pages are Melodee's to start (maybeReturnToComponentGrid)
    if (++h.ticks < holdProbeIntervalTicks(h.attempts)) return;
    h.ticks = 0;
    h.attempts++;
    openComponentEditor(h.slot, h.componentKey, h.entry);
}
function cancelComponentLoadHold() {
    componentLoadHold = null;
    setView(VIEWS.CHAIN_EDIT);
    needsRedraw = true;
    return "Chain Editor";
}
function drawComponentLoading() {
    clear_screen();
    const label = componentLoadHoldLabel();
    drawHeader(label ? `${label}` : "Module");
    const msg = "Loading...";
    print(Math.max(0, (SCREEN_WIDTH - text_width(msg)) >> 1), 28, msg, 1);
    drawFooter(["Back: exit"]);
}

// The module declares no ui_hierarchy. Schwung runs its own ui_chain.js or
// ui.js when it has one; Melodee runs no module's UI, and shows such a
// module's grid from its chain_params instead. Without one: Schwung's preset
// browser.
let editComponentPresetCount = 0, editComponentPreset = 0, editComponentPresetName = "";
function moduleUiPath() {
    if (!moduleDir) return null;
    let uiPath = `${moduleDir}/ui_chain.js`;
    try {
        const json = std.loadFile(`${moduleDir}/module.json`);
        const match = json ? json.match(/"ui_chain"\s*:\s*"([^"]+)"/) : null;
        if (match && match[1]) uiPath = `${moduleDir}/${match[1]}`;
    } catch (e) { /* none named */ }
    if (moduleFileExists(uiPath)) return uiPath;
    return moduleFileExists(`${moduleDir}/ui.js`) ? `${moduleDir}/ui.js` : null;
}
function enterComponentEditFallback(s, comp, entry) {
    if (moduleUiPath()) {
        enterParamPages(s, comp, prefixOf(comp), entry && entry.page || null, componentParamPagesIo(s), null,
                        entry && entry.page ? { enter: !!entry.enter } : undefined);
        return;
    }
    const p = prefixOf(comp);
    const countStr = getSlotParam(s, `${p}:preset_count`);
    editComponentPresetCount = countStr ? parseInt(countStr) : 0;
    const presetStr = getSlotParam(s, `${p}:preset`);
    editComponentPreset = presetStr ? parseInt(presetStr) : 0;
    editComponentPresetName = getSlotParam(s, `${p}:preset_name`) || "";
    setView(VIEWS.COMPONENT_EDIT);
    needsRedraw = true;
    const name = getSlotParam(s, `${p}:name`) || comp;
    const presetName = editComponentPresetName || `Preset ${editComponentPreset + 1}`;
    if (editComponentPresetCount > 0) announce(`${name}, ${presetName}, Preset ${editComponentPreset + 1} of ${editComponentPresetCount}`);
    else announce(`${name}, No presets`);
}
function changeComponentPreset(delta) {
    if (editComponentPresetCount <= 0) return;
    let next = editComponentPreset + delta;
    if (next < 0) next = editComponentPresetCount - 1;
    if (next >= editComponentPresetCount) next = 0;
    const p = prefixOf(component);
    setSlotParam(slot, `${p}:preset`, String(next));
    editComponentPreset = next;
    editComponentPresetName = getSlotParam(slot, `${p}:preset_name`) || "";
    announce(`${editComponentPresetName || `Preset ${editComponentPreset + 1}`}, Preset ${editComponentPreset + 1} of ${editComponentPresetCount}`);
}
function drawComponentEdit() {
    clear_screen();
    const moduleName = moduleId ? moduleId.toUpperCase() : "Unknown";
    const p = prefixOf(component);
    const owner = moduleId || moduleName;
    const displayName = getSlotParamCached(slot, `${p}:name`, owner) || moduleName;
    const abbrev = moduleId ? getModuleAbbrev(moduleId) : moduleName;
    const bankName = getSlotParamCached(slot, `${p}:bank_name`, owner) || displayName;
    drawHeader(truncateText(`S${slot + 1}: ${abbrev}: ${bankName}`, 24));
    const centerY = 32;
    if (editComponentPresetCount === 0) { // a module still loading may have them now
        const countStr = getSlotParam(slot, `${p}:preset_count`);
        const count = countStr ? parseInt(countStr) : 0;
        if (count > 0) {
            editComponentPresetCount = count;
            const presetStr = getSlotParam(slot, `${p}:preset`);
            editComponentPreset = presetStr ? parseInt(presetStr) : 0;
            editComponentPresetName = getSlotParam(slot, `${p}:preset_name`) || "";
        }
    }
    if (editComponentPresetCount > 0) {
        const number = `${editComponentPreset + 1}/${editComponentPresetCount}`;
        print(Math.floor((SCREEN_WIDTH - number.length * 5) / 2), centerY - 8, number, 1);
        const name = truncateText(editComponentPresetName || "(unnamed)", 22);
        print(Math.floor((SCREEN_WIDTH - name.length * 5) / 2), centerY + 4, name, 1);
        print(4, centerY - 2, "<", 1);
        print(SCREEN_WIDTH - 10, centerY - 2, ">", 1);
    } else {
        const msg = "No presets";
        print(Math.floor((SCREEN_WIDTH - msg.length * 5) / 2), centerY, msg, 1);
    }
    drawFooter(["Back: done", "Jog: preset"]);
}

// ---------------------------------------------------------------- the knobs off the grid

// shadow_ui.js's knob path for the screens that are not the grid: under a
// keyboard opened from a grid cell (the list editor's view) a knob turns the
// grid page's parameter it was on, a touch shows it, in the centred card
// (showOverlay); anywhere else a knob is Schwung's slot knob mapping, which
// Melodee has none of ("S2: Knob 1", "-"). Schwung's trigger knobs (fired on
// a turn) are left alone here.
const knobTouched = new Array(NUM_KNOBS).fill(false);
let pendingHierKnobIndex = -1, pendingHierKnobDelta = 0;
let pendingKnobIndex = -1, pendingKnobDelta = 0, pendingKnobRefresh = false;
const knobValueCache = new Array(NUM_KNOBS).fill(null), knobValueCacheKey = new Array(NUM_KNOBS).fill("");
const physKnobStates = new Map();
let knobContexts = null, knobContextsAt = "";
function invalidateKnobContextCache() { knobContexts = null; }
function buildKnobContextForKnob(k) {
    if (view !== VIEWS.HIERARCHY_EDITOR || k >= hierKnobs.length || !hierKnobs[k]) return null;
    const key = hierKnobs[k], meta = getParamMetadata(key);
    const pluginName = getSlotParam(hierSlot, `${prefixOf(hierComponent)}:name`) || "";
    const displayName = meta && meta.name ? meta.name : key.replace(/_/g, " ");
    return { slot: hierSlot, key, fullKey: buildHierarchyParamKey(key), meta, pluginName, displayName,
             title: `S${hierSlot + 1}: ${pluginName} ${displayName}`, cardName: displayName };
}
function getKnobContext(k) {
    const at = `${view}|${hierSlot}|${hierComponent}|${hierLevel}|${hierChildIndex}`;
    if (!knobContexts || knobContextsAt !== at) {
        knobContexts = [];
        for (let i = 0; i < NUM_KNOBS; i++) knobContexts.push(buildKnobContextForKnob(i));
        knobContextsAt = at;
    }
    return knobContexts[k] || null;
}
function showKnobFeedback(k, name, value) { showOverlay(name, value); } // the card is the chain editor's
function getHierarchyDisplayRawValue(s, fullKey) {
    const base = getSlotParam(s, `${fullKey}:base`);
    return base !== null && base !== undefined ? base : getSlotParam(s, fullKey);
}
function isHierarchyParamModulated(s, fullKey) {
    const modulated = getSlotParam(s, `${fullKey}:modulated`);
    if (modulated === "1") return true;
    if (modulated === "0") return false;
    const base = getSlotParam(s, `${fullKey}:base`);
    if (base === null || base === undefined || base === "") return false;
    const live = getSlotParam(s, fullKey);
    return live !== null && live !== undefined && live !== base;
}
function isEnumMeta(meta) { return !!(meta && (meta.type === "enum" || meta.type === "bool")); }
function showKnobOverlay(k, value) {
    const c = getKnobContext(k);
    if (!c) return false;
    if (c.fullKey) {
        const title = isHierarchyParamModulated(c.slot, c.fullKey) ? `${c.title}~` : c.title;
        let shown;
        if (value !== undefined) {
            if (isEnumMeta(c.meta)) shown = String(formatMetaOptionValue(c.meta, value));
            else if (c.meta && c.meta.type === "canvas") shown = formatCanvasDisplayValue(value, c.meta);
            else if (c.meta && c.meta.type === "string") shown = String(value || "");
            else shown = formatParamForOverlay(value, c.meta);
        } else {
            const current = getHierarchyDisplayRawValue(c.slot, c.fullKey);
            if (isEnumMeta(c.meta)) shown = current !== null && current !== undefined && current !== "" ? formatMetaOptionValue(c.meta, current) : "-";
            else if (c.meta && c.meta.type === "canvas") shown = formatCanvasDisplayValue(current || "", c.meta);
            else if (c.meta && c.meta.type === "string") shown = String(current || "");
            else {
                const num = parseFloat(current);
                shown = !isNaN(num) ? formatParamForOverlay(num, c.meta) : current || "-";
            }
        }
        showKnobFeedback(k, title, shown);
    }
    needsRedraw = true;
    return true;
}
function adjustKnobAndShow(k, delta) {
    const c = getKnobContext(k);
    if (!c) return false;
    if (!c.fullKey) {
        showKnobFeedback(k, `Knob ${k + 1}`, "not mapped");
        needsRedraw = true;
        return true;
    }
    if (pendingHierKnobIndex !== k) { pendingHierKnobIndex = k; pendingHierKnobDelta = delta; }
    else pendingHierKnobDelta += delta;
    needsRedraw = true;
    return true;
}
function getKnobCachedValue(k, c) {
    if (!c || !c.fullKey) return null;
    if (knobValueCacheKey[k] !== c.fullKey) { knobValueCache[k] = null; knobValueCacheKey[k] = c.fullKey; }
    if (knobValueCache[k] !== null) return knobValueCache[k];
    const base = getSlotParam(c.slot, `${c.fullKey}:base`);
    const rawValue = base !== null ? base : getSlotParam(c.slot, c.fullKey);
    if (rawValue === null) return null;
    if (isEnumMeta(c.meta)) knobValueCache[k] = rawValue;
    else { const num = parseFloat(rawValue); knobValueCache[k] = isNaN(num) ? null : num; }
    return knobValueCache[k];
}
function getPhysKnobState(fullKey, current) {
    let st = physKnobStates.get(fullKey);
    if (!st) { st = knobInit(current); physKnobStates.set(fullKey, st); }
    else st.value = current;
    return st;
}
function processPendingHierKnob() {
    if (pendingHierKnobIndex < 0 || pendingHierKnobDelta === 0) {
        if (pendingHierKnobIndex < 0) return; // held, still: the card kept up from the cached value
        const c = getKnobContext(pendingHierKnobIndex);
        if (!c || !c.fullKey || isTriggerParam(c.meta) || isTriggerEnumMeta(c.meta)) return;
        const cached = getKnobCachedValue(pendingHierKnobIndex, c);
        if (cached === null) return;
        if (isEnumMeta(c.meta)) showKnobFeedback(pendingHierKnobIndex, c.title, formatMetaOptionValue(c.meta, cached));
        else if (c.meta && c.meta.type === "canvas") showKnobFeedback(pendingHierKnobIndex, c.title, formatCanvasDisplayValue(String(cached), c.meta));
        else if (c.meta && c.meta.type === "string") showKnobFeedback(pendingHierKnobIndex, c.title, String(cached || ""));
        else showKnobFeedback(pendingHierKnobIndex, c.title, formatParamForOverlay(cached, c.meta));
        needsRedraw = true;
        return;
    }
    const k = pendingHierKnobIndex, delta = pendingHierKnobDelta;
    pendingHierKnobDelta = 0;
    const c = getKnobContext(k);
    if (!c || !c.fullKey || isTriggerParam(c.meta) || isTriggerEnumMeta(c.meta)) return;
    if (isReadoutParam(c.meta)) {
        const cached = getKnobCachedValue(k, c);
        showKnobFeedback(k, c.title, cached === null ? "" : formatParamForOverlay(cached, c.meta));
        needsRedraw = true;
        return;
    }
    const current = getKnobCachedValue(k, c);
    if (current === null) return;
    const meta = c.meta || {};
    if (meta.type === "enum" && Array.isArray(meta.options) && meta.options.length) {
        let at = meta.options.indexOf(current);
        if (at < 0) {
            const parsed = parseInt(current, 10);
            at = !isNaN(parsed) && parsed >= 0 && parsed < meta.options.length ? parsed : 0;
        }
        const next = knobStep(getPhysKnobState(c.fullKey, at), meta, delta, Date.now(), isShiftHeld());
        if (next === at) { showKnobFeedback(k, c.title, formatMetaOptionValue(meta, meta.options[at])); return; }
        const value = meta.options[next];
        const usesIndex = meta.options.indexOf(current) < 0;
        knobValueCache[k] = usesIndex ? String(next) : value;
        setSlotParam(c.slot, c.fullKey, usesIndex ? String(next) : value);
        invalidateKnobContextCache();
        showKnobFeedback(k, c.title, formatMetaOptionValue(meta, value));
        return;
    }
    if (meta.type === "canvas") { showKnobFeedback(k, c.title, formatCanvasDisplayValue(String(current), meta)); return; }
    if (meta.type === "string") { showKnobFeedback(k, c.title, String(current || "")); return; }
    const num = typeof current === "number" ? current : parseFloat(current);
    if (isNaN(num)) return;
    const wavPos = meta.ui_type === "wav_position";
    const next = knobStep(getPhysKnobState(c.fullKey, num), meta, delta, Date.now(), !wavPos && isShiftHeld());
    knobValueCache[k] = next;
    setSlotParam(c.slot, c.fullKey, formatParamForSet(next, meta));
    showKnobFeedback(k, c.title, formatParamForOverlay(next, meta));
    needsRedraw = true;
}
function handleKnobTurn(k, delta) {
    if (pendingKnobIndex !== k) { pendingKnobIndex = k; pendingKnobDelta = delta; }
    else pendingKnobDelta += delta;
    pendingKnobRefresh = true;
    needsRedraw = true;
}
function refreshPendingKnobOverlay() { // the slot's own knob mapping, which the chain host keeps
    if (!pendingKnobRefresh || pendingKnobIndex < 0) return;
    const n = pendingKnobIndex + 1;
    if (pendingKnobDelta !== 0) {
        setSlotParam(slot, `knob_${n}_adjust`, pendingKnobDelta > 0 ? `+${pendingKnobDelta}` : `${pendingKnobDelta}`);
        pendingKnobDelta = 0;
    }
    const name = getSlotParam(slot, `knob_${n}_name`) || `Knob ${n}`;
    const value = getSlotParam(slot, `knob_${n}_value`) || "-";
    showKnobFeedback(pendingKnobIndex, `S${slot + 1}: ${name}`, value);
    pendingKnobRefresh = false;
    pendingKnobIndex = -1;
}

// ---------------------------------------------------------------- the user preset it is on

// shadow_ui.js's record of which user preset a component is on, {name,
// hash}, and the live state blob the last plan read (the header's `*` rides
// on it). Schwung keeps one per slot and component and saves it with the
// slot; here the pages are one module's, so one record, saved with
// Melodee's project (melodee:preset_record).
let userPresetRecord = null, userPresetLiveBlob = null, userPresetRecordRead = false;
function getUserPresetRecord(s, prefix) {
    return s === slot && prefix === prefixOf(component) ? userPresetRecord : null;
}
function setUserPresetRecord(s, prefix, record) {
    if (s !== slot || prefix !== prefixOf(component)) return;
    userPresetRecord = record || null;
    tell("preset_record", userPresetRecord ? JSON.stringify(userPresetRecord) : "");
}
function readUserPresetRecord() { // what the project kept, once
    if (userPresetRecordRead) return;
    userPresetRecordRead = true;
    const kept = parseJson(seam("preset_record"));
    userPresetRecord = kept && kept.name ? { name: String(kept.name), hash: kept.hash || null } : null;
}

// Record the preset as loaded or saved, hashing what the module now reports
// (not the blob handed to it: a module may normalise it on the way in).
function recordUserPresetFromDevice(s, prefix, name) {
    const live = getSlotStateWithRetry(s, prefix + ":state");
    userPresetLiveBlob = live;
    setUserPresetRecord(s, prefix, makeRecord(name, live));
    paramPagesRefreshTrailing();
    needsRedraw = true;
}
// Should the return to My Presets land inside its menu? Back having chosen
// nothing, and Delete, yes; a Load or a Save is done, no (shadow_ui.js's).
let componentGridReturnEnter = true;
function onUserPresetSaved(s, prefix, name) {
    componentGridReturnEnter = false;
    recordUserPresetFromDevice(s, prefix, name);
}
function onUserPresetLoaded(s, prefix, name) {
    componentGridReturnEnter = false;
    recordUserPresetFromDevice(s, prefix, name);
}
function onUserPresetDeleted(s, prefix, name) {
    componentGridReturnEnter = true;
    const rec = getUserPresetRecord(s, prefix);
    if (rec && rec.name === name) setUserPresetRecord(s, prefix, null); // only the one it is on
    paramPagesRefreshTrailing();
    needsRedraw = true;
}

// The `*` after a write through the grid: once the hand settles, the
// trailing pages planned again (one state read), only while the grid is still
// up on the component that wrote.
let userPresetWritePending = false, userPresetWriteAt = 0, userPresetWriteSlot = -1, userPresetWriteComponent = "";
function markComponentParamWrite(s, componentKey) {
    userPresetWritePending = true;
    userPresetWriteAt = Date.now();
    userPresetWriteSlot = s;
    userPresetWriteComponent = componentKey;
}
function tickUserPresetStale() {
    if (!userPresetWritePending) return;
    if (Date.now() - userPresetWriteAt < CONTRACT_SETTLE_MS) return;
    userPresetWritePending = false;
    if (!paramPagesActive()) return;
    if (paramPagesSlot() !== userPresetWriteSlot) return;
    if (paramPagesComponent() !== userPresetWriteComponent) return;
    paramPagesRefreshTrailing();
}
// The header's "S2 > <name>" for a loaded user preset, `*` when changed
// since; null with none (the header shows the module's own then).
function userPresetHeaderMark(s, componentKey) {
    const record = getUserPresetRecord(s, prefixOf(componentKey));
    if (!record || !record.name) return null;
    return { name: record.name, dirty: isModified(record, userPresetLiveBlob) };
}

// ---------------------------------------------------------------- Module Help

// A module's own help.json topics, or null without (cached, misses too).
// Melodee's module folder first (on the Move it is one of Schwung's own),
// then where Schwung's module manager lays modules out.
const MODULE_HELP_BASES = [
    "/data/UserData/schwung/modules",
    "/data/UserData/schwung/modules/sound_generators",
    "/data/UserData/schwung/modules/audio_fx",
    "/data/UserData/schwung/modules/midi_fx",
    "/data/UserData/schwung/modules/tools",
];
const moduleHelpCache = Object.create(null);
function getModuleHelpChildren(id) {
    if (!id) return null;
    if (id.indexOf("/") >= 0 || id.indexOf("..") >= 0) return null;
    if (id in moduleHelpCache) return moduleHelpCache[id];
    const folders = (id.toLowerCase() === moduleId.toLowerCase() && moduleDir ? [moduleDir] : [])
        .concat(MODULE_HELP_BASES.map((base) => onDisk(`${base}/${id}`)));
    let children = null;
    for (const folder of folders) {
        let raw = null;
        try { raw = std.loadFile(`${folder}/help.json`); } catch (e) { /* not here */ }
        if (!raw) continue;
        try { // found: its answer, whatever it says
            const data = JSON.parse(raw);
            if (data && Array.isArray(data.children) && data.children.length > 0) children = data.children;
        } catch (e) { log("shadow", "Bad help.json for " + id + ": " + e); }
        break;
    }
    moduleHelpCache[id] = children;
    return children;
}

// shadow_ui.js's help viewer, hosted by its GLOBAL_SETTINGS view (drawn by
// shadow_ui_settings.mjs): a stack of topic lists, a topic's text scrolled.
// A Module Help session starts with one frame, the module's topics; Back off
// it empties the stack, and the grid comes back on "Module", its menu open.
let helpNavStack = [], helpDetailScrollState = null;
let componentHelpReturnSlot = -1, componentHelpReturnKey = "";
function helpBackTarget(inDetail) {
    const depth = helpNavStack.length;
    if (depth === 0) return "Settings";
    const targetIdx = inDetail ? depth - 1 : depth - 2;
    if (targetIdx < 0) return componentHelpReturnSlot >= 0 ? helpNavStack[0].title : "Settings";
    if (targetIdx === 0 && componentHelpReturnSlot >= 0) return "List";
    return helpNavStack[targetIdx].title;
}
function helpHeaderTitle(frame) {
    if (helpNavStack.length === 1 && componentHelpReturnSlot >= 0) return "Help: " + frame.title;
    return frame.title;
}
function drawHelpList() {
    const frame = helpNavStack[helpNavStack.length - 1];
    drawHeader(helpHeaderTitle(frame));
    drawMenuList({
        items: frame.items,
        selectedIndex: frame.selectedIndex,
        getLabel: (item) => item.title,
        getValue: () => "",
        listArea: { topY: LIST_TOP_Y, bottomY: FOOTER_RULE_Y },
        valueAlignRight: true,
    });
    drawFooter("Back: " + helpBackTarget(false));
}
function drawHelpDetail() {
    const frame = helpNavStack[helpNavStack.length - 1];
    const item = frame.items[frame.selectedIndex];
    drawHeader(item.title);
    if (helpDetailScrollState) {
        drawScrollableText({ state: helpDetailScrollState, topY: LIST_TOP_Y, bottomY: FOOTER_RULE_Y, actionY: -1 });
    }
    drawFooter("Back: " + helpBackTarget(true));
}
function maybeReturnToComponentHelp() {
    if (componentHelpReturnSlot < 0) return false;
    if (helpNavStack.length > 0 || helpDetailScrollState) return false;
    if (isTextEntryActive()) return false;
    const s = componentHelpReturnSlot, componentKey = componentHelpReturnKey;
    componentHelpReturnSlot = -1;
    componentHelpReturnKey = "";
    if (!positionModule) return false;
    enterParamPages(s, componentKey, prefixOf(componentKey), "Module", componentParamPagesIo(s, componentKey), null, { enter: true });
    needsRedraw = true;
    return true;
}
function helpJog(delta) {
    if (helpDetailScrollState) {
        handleScrollableTextJog(helpDetailScrollState, delta);
    } else if (helpNavStack.length > 0) {
        const frame = helpNavStack[helpNavStack.length - 1];
        frame.selectedIndex = Math.max(0, Math.min(frame.items.length - 1, frame.selectedIndex + delta));
        announce(frame.items[frame.selectedIndex].title);
    }
}
function helpSelect() {
    if (helpDetailScrollState) {
        if (isActionSelected(helpDetailScrollState)) {
            helpDetailScrollState = null;
            needsRedraw = true;
            const frame = helpNavStack[helpNavStack.length - 1];
            announce(frame.title + ", " + frame.items[frame.selectedIndex].title);
        }
    } else if (helpNavStack.length > 0) {
        const frame = helpNavStack[helpNavStack.length - 1];
        const item = frame.items[frame.selectedIndex];
        if (item.children && item.children.length > 0) {
            helpNavStack.push({ items: item.children, selectedIndex: 0, title: item.title });
            needsRedraw = true;
            announce(item.title + ", " + item.children[0].title);
        } else if (item.lines && item.lines.length > 0) {
            helpDetailScrollState = createScrollableText({
                lines: item.lines,
                actionLabel: "Back",
                visibleLines: visibleLinesFor(LIST_TOP_Y, FOOTER_RULE_Y),
                onActionSelected: (label) => announce(label),
            });
            needsRedraw = true;
            announce(item.title + ". " + item.lines.join(". "));
        }
    }
}
function helpBack() {
    if (helpDetailScrollState) {
        helpDetailScrollState = null;
        needsRedraw = true;
        const frame = helpNavStack[helpNavStack.length - 1];
        announce(frame.title + ", " + frame.items[frame.selectedIndex].title);
    } else if (helpNavStack.length > 0) {
        helpNavStack.pop();
        needsRedraw = true;
        if (helpNavStack.length > 0) {
            const frame = helpNavStack[helpNavStack.length - 1];
            announce(frame.title + ", " + frame.items[frame.selectedIndex].title);
        }
    } else {
        setView(VIEWS.CHAIN_EDIT); // nothing left to back out of (Schwung: out of Global Settings)
    }
}

// ---------------------------------------------------------------- Add to List

// shadow_ui.js's module lists (module_lists.mjs's model, Schwung's own file,
// shared with Schwung): which lists hold the module, the lists themselves,
// one list's actions. Held for the session and written on every change; a
// file that would not parse is never written over.
const moduleListsIo = {
    readFile: (p) => host_read_file(p),
    writeFile: (p, body) => host_write_file(p, body),
};
let moduleListsState = null, moduleListsCorrupt = false;
let moduleListsSlot = -1, moduleListsKey = "", moduleListsModuleId = "";
let moduleListsMemberIndex = 0, moduleListsEditIndex = 0, moduleListsActionIndex = 0;
let moduleListsTarget = "", moduleListsConfirmDelete = false;
let moduleListsPendingName = null; // a name the keyboard refused, offered again a tick on
function moduleListsLoad() {
    const r = ModuleLists.loadLists(moduleListsIo);
    moduleListsState = r.state;
    moduleListsCorrupt = r.corrupt;
    if (r.corrupt) log("shadow", "module_lists.json unreadable, changes will not persist");
}
function moduleListsSave() {
    if (!moduleListsState || moduleListsCorrupt) return false;
    return ModuleLists.saveLists(moduleListsIo, moduleListsState);
}
function moduleListsEnsureLoaded() { if (!moduleListsState) moduleListsLoad(); }
function moduleListsCountFor(id) { // the Module page row's value
    moduleListsEnsureLoaded();
    return ModuleLists.listsContaining(moduleListsState, id).length;
}
function moduleListsRows() { // never a read: drawn
    if (!moduleListsState) return [];
    const rows = moduleListsState.lists.map((l) => ({
        name: l.name,
        value: ModuleLists.isMember(moduleListsState, l.name, moduleListsModuleId) ? "*" : "",
        kind: "toggle",
    }));
    rows.push({ name: "New List...", value: "", kind: "new" });
    rows.push({ name: "Edit Lists...", value: "", kind: "edit" });
    return rows;
}
function moduleListsClampMemberIndex() {
    const n = moduleListsRows().length;
    moduleListsMemberIndex = Math.max(0, Math.min(n > 0 ? n - 1 : 0, moduleListsMemberIndex));
}
function moduleListsRowLabel(i) {
    const rows = moduleListsRows();
    const r = rows[Math.max(0, Math.min(rows.length - 1, i))];
    if (!r) return "";
    return r.kind === "toggle" ? `${r.name}, ${r.value ? "on" : "off"}` : r.name;
}
function listScreenCtx() { return { fillRect: fill_rect, print, textWidth: text_width }; }
function drawModuleLists() {
    clear_screen();
    drawListScreen(listScreenCtx(), {
        headerLeft: getModuleDisplayName(moduleListsModuleId),
        headerRight: "LISTS",
        entries: moduleListsRows(),
        index: moduleListsMemberIndex,
        footer: [["CLK", "TOGGLE"], ["BACK", "MODULE"]],
    });
}
// The keyboard for New List and Rename; a refused name comes back with what
// was typed, a tick later (the keyboard closes itself after onConfirm).
function moduleListsOpenNameEntry(existing, prefill) {
    openTextEntry({
        title: existing ? "Rename List" : "New List",
        initialText: prefill === undefined ? (existing || "") : prefill,
        onAnnounce: announce,
        onConfirm: (text) => {
            const r = existing ? ModuleLists.renameList(moduleListsState, existing, text)
                               : ModuleLists.createList(moduleListsState, text);
            if (!r.ok) {
                announce(r.err);
                moduleListsPendingName = { existing, text };
                return;
            }
            const saved = moduleListsSave();
            if (existing) moduleListsTarget = String(text).trim();
            announce((existing ? `Renamed to ${text}` : `Created ${text}`) + (saved ? "" : ", save failed"));
            needsRedraw = true;
        },
        onCancel: () => { needsRedraw = true; },
    });
}
function moduleListsTickPendingName() {
    if (!moduleListsPendingName) return;
    const pending = moduleListsPendingName;
    moduleListsPendingName = null;
    moduleListsOpenNameEntry(pending.existing, pending.text);
}
function moduleListsResetSession() {
    moduleListsSlot = -1;
    moduleListsKey = "";
    moduleListsModuleId = "";
    moduleListsEditIndex = 0;
    moduleListsActionIndex = 0;
    moduleListsTarget = "";
    moduleListsConfirmDelete = false;
    moduleListsPendingName = null;
}
// Back out of the lists: the Module page, its menu open (the row clicked).
function exitModuleLists() {
    const s = moduleListsSlot, componentKey = moduleListsKey;
    moduleListsResetSession();
    if (s < 0 || !positionModule) { setView(VIEWS.CHAIN_EDIT); needsRedraw = true; return; }
    enterParamPages(s, componentKey, prefixOf(componentKey), "Module", componentParamPagesIo(s, componentKey), null, { enter: true });
    needsRedraw = true;
}
function moduleListsEditRows() {
    if (!moduleListsState) return [];
    return moduleListsState.lists.map((l) => ({ name: l.name, value: String(l.modules.length) }));
}
function moduleListsEditRowLabel() {
    const r = moduleListsEditRows()[moduleListsEditIndex];
    if (!r) return "";
    return `${r.name}, ${r.value} module${r.value === "1" ? "" : "s"}`;
}
function moduleListsClampEditIndex() {
    const n = moduleListsEditRows().length;
    moduleListsEditIndex = Math.max(0, Math.min(n > 0 ? n - 1 : 0, moduleListsEditIndex));
}
function drawModuleListsEdit() {
    clear_screen();
    drawListScreen(listScreenCtx(), {
        headerLeft: "Edit Lists",
        entries: moduleListsEditRows(),
        index: moduleListsEditIndex,
        footer: [["JOG", "SEL"], ["CLK", "EDIT"], ["BACK", "LISTS"]],
    });
}
function moduleListsActionRows() { // Favorites: Clear alone
    const rows = [];
    if (!ModuleLists.isProtected(moduleListsTarget)) {
        rows.push({ name: "Rename", kind: "rename" });
        rows.push({ name: "Delete", kind: "delete" });
    }
    rows.push({ name: "Clear", kind: "clear" });
    return rows;
}
const CONFIRM_TEXT_W = 112; // a confirm card's line: its 120 less the border, a pixel of air
function drawModuleListsActions() {
    clear_screen();
    const c = listScreenCtx();
    drawListScreen(c, {
        headerLeft: moduleListsTarget,
        entries: moduleListsActionRows(),
        index: moduleListsActionIndex,
        footer: moduleListsConfirmDelete ? [["CLK", "YES"], ["BACK", "NO"]]
            : (moduleListsActionRows().length > 1 ? [["JOG", "SEL"], ["CLK", "DO"], ["BACK", "EDIT"]]
                                                  : [["CLK", "DO"], ["BACK", "EDIT"]]),
    });
    if (moduleListsConfirmDelete) // drawn last, so fed first
        drawConfirmOverlay("Delete List", [fitText(c, moduleListsTarget + "?", CONFIRM_TEXT_W)], "CLK=Yes  BACK=No");
}
function moduleListsSelectAction() {
    if (moduleListsConfirmDelete) {
        moduleListsConfirmDelete = false;
        const name = moduleListsTarget;
        const r = ModuleLists.deleteList(moduleListsState, name);
        const saved = r.ok ? moduleListsSave() : false;
        moduleListsTarget = "";
        moduleListsActionIndex = 0;
        moduleListsClampEditIndex();
        setView(VIEWS.MODULE_LISTS_EDIT);
        announce((r.ok ? "Deleted " + name + (saved ? "" : ", save failed") : (r.err || "Delete failed"))
                 + ". Edit Lists, " + moduleListsEditRowLabel());
        needsRedraw = true;
        return;
    }
    const row = moduleListsActionRows()[moduleListsActionIndex];
    if (!row) return;
    if (row.kind === "rename") {
        moduleListsOpenNameEntry(moduleListsTarget);
    } else if (row.kind === "delete") {
        moduleListsConfirmDelete = true;
        announce("Delete " + moduleListsTarget + "?");
    } else {
        const r = ModuleLists.clearList(moduleListsState, moduleListsTarget);
        if (!r.ok) announce(r.err || "Clear failed");
        else announce("Cleared " + moduleListsTarget + (moduleListsSave() ? "" : ", save failed"));
    }
    needsRedraw = true;
}
function moduleListsActionsBack() {
    if (moduleListsConfirmDelete) {
        moduleListsConfirmDelete = false;
        const row = moduleListsActionRows()[moduleListsActionIndex];
        announce("Cancelled, " + moduleListsTarget + (row ? ", " + row.name : ""));
    } else {
        moduleListsClampEditIndex();
        setView(VIEWS.MODULE_LISTS_EDIT);
        announce("Edit Lists, " + moduleListsEditRowLabel());
    }
    needsRedraw = true;
}
function moduleListsJog(delta) {
    if (view === VIEWS.MODULE_LISTS) {
        moduleListsEnsureLoaded();
        const rows = moduleListsRows();
        moduleListsMemberIndex = Math.max(0, Math.min(rows.length - 1, moduleListsMemberIndex + delta));
        announceMenuItem("List", moduleListsRowLabel(moduleListsMemberIndex));
    } else if (view === VIEWS.MODULE_LISTS_EDIT) {
        moduleListsEnsureLoaded();
        const rows = moduleListsEditRows();
        moduleListsEditIndex = Math.max(0, Math.min(rows.length - 1, moduleListsEditIndex + delta));
        if (rows[moduleListsEditIndex]) announceMenuItem("List", moduleListsEditRowLabel());
    } else if (!moduleListsConfirmDelete) { // the actions: the confirm owns the jog while it is up
        const rows = moduleListsActionRows();
        moduleListsActionIndex = Math.max(0, Math.min(rows.length - 1, moduleListsActionIndex + delta));
        const r = rows[moduleListsActionIndex];
        if (r) announceMenuItem(moduleListsTarget, r.name);
    }
}
function moduleListsSelect() {
    moduleListsEnsureLoaded();
    if (view === VIEWS.MODULE_LISTS) {
        moduleListsClampMemberIndex();
        const row = moduleListsRows()[moduleListsMemberIndex];
        if (!row) return;
        if (row.kind === "toggle") {
            const on = ModuleLists.toggleMembership(moduleListsState, row.name, moduleListsModuleId);
            if (on === null) announce("List unavailable");
            else announce(`${row.name}, ${on ? "added" : "removed"}` + (moduleListsSave() ? "" : ", save failed"));
        } else if (row.kind === "new") {
            moduleListsOpenNameEntry(null);
        } else {
            moduleListsEditIndex = 0;
            setView(VIEWS.MODULE_LISTS_EDIT);
            announce("Edit Lists, " + ((moduleListsState.lists[0] || {}).name || ModuleLists.FAVORITES));
        }
        needsRedraw = true;
    } else if (view === VIEWS.MODULE_LISTS_EDIT) {
        moduleListsClampEditIndex();
        const r = moduleListsEditRows()[moduleListsEditIndex];
        if (!r) return;
        moduleListsTarget = r.name;
        moduleListsActionIndex = 0;
        moduleListsConfirmDelete = false; // never a latch carried into another list
        setView(VIEWS.MODULE_LISTS_ACTIONS);
        announce(`${r.name}, ${(moduleListsActionRows()[0] || {}).name || "Clear"}`);
        needsRedraw = true;
    } else {
        moduleListsSelectAction();
    }
}
function moduleListsBack() {
    if (view === VIEWS.MODULE_LISTS) { exitModuleLists(); return; }
    if (view === VIEWS.MODULE_LISTS_EDIT) {
        moduleListsClampMemberIndex();
        setView(VIEWS.MODULE_LISTS);
        announce("Add to List, " + moduleListsRowLabel(moduleListsMemberIndex));
        needsRedraw = true;
        return;
    }
    moduleListsActionsBack();
}

// ---------------------------------------------------------------- Swap Module

// shadow_ui.js's module picker for a position (chain_editor_chrome.mjs's
// drawChainPicker): List (a module list to filter by), Sort (A-Z or by
// type), None, the modules of the position's kind, "*" on the one loaded.
// The modules are Melodee's (its scan, those it can load); Schwung's
// "[Get more...]" (its catalog) and its Move Left / Move Right (Melodee
// moves an effect with Shift + the wheel on its Device View) are not here.
const PICKER_FILTER_ID = "__list_filter__", PICKER_SORT_ID = "__sort__";
let componentSelectFilter = null, componentSelectSort = "A-Z"; // for this module's pages
let availableModules = [], selectedModuleIndex = 0, selectSlot = 0, selectComponentKey = "";
function pickerIsControlRow(row) {
    const id = row && row.id;
    return id === PICKER_FILTER_ID || id === PICKER_SORT_ID;
}
function scanModulesForType(componentKey) { // None first, then the modules by name
    const expected = componentKey === "synth" ? ["sound_generator"] : /^fx\d+$/.test(componentKey) ? ["audio_fx"] : [];
    const result = [{ id: "", name: "None" }];
    const found = parseJson(seam("modules"));
    for (const json of Array.isArray(found) ? found : []) {
        if (!json || !json.id) continue;
        cacheModuleAbbrev(json);
        if (!expected.includes(json.component_type)) continue;
        if (!result.find((m) => m.id === json.id)) result.push({ id: json.id, name: json.name || json.id, subcategory: json.subcategory || "" });
    }
    const modules = result.slice(1);
    modules.sort((a, b) => compareNames(a.name, b.name));
    return [result[0], ...modules];
}
function pickerApplySort(entries, componentKey) {
    if (componentSelectSort !== "Type") return entries;
    const head = [], mods = [], tail = [];
    for (const m of entries) {
        if (!m) continue;
        if (m.id === "") head.push(m);
        else if (String(m.id).indexOf("__") === 0) tail.push(m);
        else mods.push(m);
    }
    const catalog = ModuleCategories.loadCatalogCategories((path) => std.loadFile(onDisk(path)));
    const grouped = ModuleCategories.groupRowsByCategory(mods, ModuleCategories.taxonomyTypeFor(componentKey), catalog);
    return [...head, ...grouped, ...tail];
}
function pickerRealIds(entries) {
    return (entries || []).filter((m) => m && m.id && String(m.id).indexOf("__") !== 0).map((m) => m.id);
}
function pickerEligibleLists(scanned) {
    moduleListsEnsureLoaded();
    return ModuleLists.listsWithAnyOf(moduleListsState, pickerRealIds(scanned));
}
function pickerApplyFilter(entries, filterName) {
    if (!filterName) return entries;
    moduleListsEnsureLoaded();
    const kept = ModuleLists.filterIds(moduleListsState, pickerRealIds(entries), filterName);
    if (kept === null) return entries;
    const keep = Object.create(null);
    for (const id of kept) keep[id] = true;
    return entries.filter((m) => !m || !m.id || String(m.id).indexOf("__") === 0 || keep[m.id]);
}
function pickerFirstSelectableIndex(entries) {
    for (let i = 0; i < entries.length; i++) {
        const id = entries[i] && entries[i].id;
        if (id === PICKER_FILTER_ID || id === PICKER_SORT_ID) continue;
        if (entries[i] && entries[i].type === "divider") continue;
        return i;
    }
    return 0;
}
function enterComponentSelect(s, componentKey) {
    selectSlot = s;
    selectComponentKey = componentKey;
    availableModules = scanModulesForType(componentKey);
    const eligible = pickerEligibleLists(availableModules);
    let filterReset = false;
    if (componentSelectFilter && eligible.indexOf(componentSelectFilter) < 0) {
        componentSelectFilter = null;
        filterReset = true;
    }
    availableModules = pickerApplyFilter(availableModules, componentSelectFilter);
    availableModules = pickerApplySort(availableModules, componentKey);
    availableModules.unshift({ id: PICKER_SORT_ID, name: "Sort", value: componentSelectSort, clickVerb: "SORT" });
    availableModules.unshift({ id: PICKER_FILTER_ID, name: "List", value: componentSelectFilter || "All", clickVerb: "LIST" });
    const shownIdx = positionModule ? availableModules.findIndex((m) => m.id === positionModule) : -1;
    selectedModuleIndex = shownIdx >= 0 ? shownIdx : pickerFirstSelectableIndex(availableModules);
    setView(VIEWS.COMPONENT_SELECT);
    needsRedraw = true;
    const name = (availableModules[selectedModuleIndex] && availableModules[selectedModuleIndex].name) || "None";
    announce(`Select ${componentLabel(componentKey)}, ${name}`
             + (filterReset ? ", list filter reset to All" : (componentSelectFilter ? `, list ${componentSelectFilter}` : "")));
}
function componentSelectJog(delta) { // over the category headings; a step that could only land on one stays
    const dir = delta > 0 ? 1 : -1;
    let next = Math.max(0, Math.min(availableModules.length - 1, selectedModuleIndex + delta));
    while (next > 0 && next < availableModules.length - 1 && ModuleCategories.isCategoryHeader(availableModules[next])) next += dir;
    if (!ModuleCategories.isCategoryHeader(availableModules[next])) selectedModuleIndex = next;
    if (availableModules.length > 0) {
        const mod = availableModules[selectedModuleIndex];
        announceMenuItem("Module", mod.name || mod.id || "Unknown");
    }
}
function applyComponentSelection() {
    const selected = availableModules[selectedModuleIndex];
    if (selected && selected.id === PICKER_FILTER_ID) { // cycles in place
        componentSelectFilter = ModuleLists.nextFilter(componentSelectFilter, pickerEligibleLists(scanModulesForType(selectComponentKey)));
        enterComponentSelect(selectSlot, selectComponentKey);
        selectedModuleIndex = 0;
        announce("List, " + (componentSelectFilter || "All"));
        return;
    }
    if (selected && selected.id === PICKER_SORT_ID) { // and so does the sort
        componentSelectSort = componentSelectSort === "Type" ? "A-Z" : "Type";
        enterComponentSelect(selectSlot, selectComponentKey);
        selectedModuleIndex = Math.max(0, availableModules.findIndex((m) => m && m.id === PICKER_SORT_ID));
        announce("Sort, " + (componentSelectSort === "Type" ? "by type" : "A to Z"));
        return;
    }
    if (ModuleCategories.isCategoryHeader(selected)) return;
    applyChainComponentPick(selectSlot, selectComponentKey, selected && selected.id ? selected.id : "");
}
// A pick, None included (Remove Module is this too): the record is not the
// new module's; None removes the module, Melodee's way (out to the Device
// View, then gone); another module is loaded once the grid would come back
// (maybeReturnToComponentGrid); the same one stays as it is (Schwung loads
// it again).
function applyChainComponentPick(s, componentKey, picked) {
    if (!picked || picked.toLowerCase() !== String(positionModule).toLowerCase())
        setUserPresetRecord(s, prefixOf(componentKey), null);
    if (!picked) {
        positionModule = "";
        tell("action", "remove");
    } else {
        positionModule = picked;
    }
    setView(VIEWS.CHAIN_EDIT);
    needsRedraw = true;
}
function drawComponentSelect() {
    clear_screen();
    drawChainPicker(listScreenCtx(), {
        headerLeft: `S${selectSlot + 1} > ${componentLabel(selectComponentKey) || "Module"}`,
        entries: availableModules,
        index: selectedModuleIndex,
        currentId: positionModule || null,
        emptyMessage: "No modules available",
    });
}

// ---------------------------------------------------------------- the trailing pages

// shadow_ui.js's moduleMenuEntries: Module Help where the module ships
// topics, Add to List (the count of lists holding it), Swap, Remove; its
// Clear Automation is Schwung's clip lanes, not here; Melodee's Auto Sample
// last, on an instrument's.
function moduleMenuEntries(id) {
    const entries = [];
    if (getModuleHelpChildren(id)) entries.push({ label: "Module Help", action: "module_help" });
    const inLists = moduleListsCountFor(id);
    entries.push({ label: "Add to List", value: inLists > 0 ? String(inLists) : "", action: "module_lists" });
    entries.push({ label: "Swap Module", action: "swap_module" });
    entries.push({ label: "Remove Module", action: "remove_module" });
    if (autoSampleOffered) entries.push({ label: "Auto Sample", action: "melodee:auto_sample" });
    return entries;
}
// shadow_ui.js's componentTrailingMenus: "My Presets" (the Preset row the
// door to the list, Save and Delete with one loaded, Save As) and "Module".
// Planned on entry and after the screens' own writes, never drawn from: the
// state read here is what the header's `*` rides on.
function componentTrailingMenus(s, componentKey, prefix) {
    const id = positionModule;
    if (!id) return [];
    const pages = [];
    if (presetsOffered) {
        const record = getUserPresetRecord(s, prefix);
        const liveBlob = getSlotStateWithRetry(s, prefix + ":state");
        userPresetLiveBlob = liveBlob;
        const hasRecord = !!(record && record.name);
        const presetEntries = [{ label: "Preset", value: presetRowValue(record, liveBlob), action: "up_load" }];
        if (hasRecord) presetEntries.push({ label: "Save", action: "up_save" });
        presetEntries.push({ label: "Save As", action: "up_save_as" });
        if (hasRecord) presetEntries.push({ label: "Delete", action: "up_delete" });
        pages.push({ name: "My Presets", entries: presetEntries });
    }
    pages.push({ name: "Module", entries: moduleMenuEntries(id) });
    return pages;
}
function gridActionOpenedSomething(...conditions) { return conditions.some(Boolean); }
// A hand-off the grid made, waiting for its way back (maybeReturnToComponentGrid).
let componentModalFromGrid = false, componentGridReturnSlot = -1, componentGridReturnKey = "", componentGridReturnModule = "";
// shadow_ui.js's runComponentActionFromGrid, for the rows above.
function runComponentActionFromGrid(s, componentKey, action) {
    const prefix = prefixOf(componentKey);
    const id = positionModule;
    const record = getUserPresetRecord(s, prefix);
    const viewBefore = view;
    let result;
    switch (action) {
    case "up_load": // the list, on the preset it is on
        presetsUi.enterPresetBrowser(s, componentKey, id, prefix, { currentName: record ? record.name : "" });
        result = true;
        break;
    case "up_save": {
        if (!record || !record.name) { result = false; break; }
        if (presetsUi.overwriteUserPreset(s, prefix, id, record.name)) {
            onUserPresetSaved(s, prefix, record.name);
            paramPagesExitMenu(); // in place: the menu closed behind it
            announce(`Saved ${record.name}`);
        } else {
            announce("Save failed");
            needsRedraw = true;
        }
        result = true;
        break;
    }
    case "up_save_as":
        presetsUi.enterPresetSaveAs(s, componentKey, id, prefix);
        result = true;
        break;
    case "up_delete":
        if (!record || !record.name) { result = false; break; }
        presetsUi.enterPresetDeleteConfirm(s, componentKey, id, prefix, record.name);
        result = true;
        break;
    case "module_help": { // the module's own topics; Back off the first frame comes back here
        const children = getModuleHelpChildren(id);
        if (!children) return false;
        exitParamPages();
        componentHelpReturnSlot = s;
        componentHelpReturnKey = componentKey;
        helpDetailScrollState = null;
        helpNavStack = [{ items: children, selectedIndex: 0, title: getModuleDisplayName(id) }];
        setView(VIEWS.GLOBAL_SETTINGS);
        needsRedraw = true;
        announce(helpNavStack[0].title + " Help, " + children[0].title);
        return true;
    }
    case "module_lists": {
        if (!id) return false;
        exitParamPages();
        moduleListsLoad();
        moduleListsSlot = s;
        moduleListsKey = componentKey;
        moduleListsModuleId = id;
        moduleListsMemberIndex = 0;
        moduleListsEditIndex = 0;
        moduleListsActionIndex = 0;
        moduleListsTarget = "";
        moduleListsConfirmDelete = false;
        moduleListsPendingName = null;
        setView(VIEWS.MODULE_LISTS);
        needsRedraw = true;
        announce("Add to List, " + moduleListsRowLabel(0) + (moduleListsCorrupt ? ", lists file unreadable, changes will not save" : ""));
        return true;
    }
    case "swap_module":
        enterComponentSelect(s, componentKey);
        result = true;
        break;
    case "remove_module": // no question: the row is the door
        applyChainComponentPick(s, componentKey, "");
        result = true;
        break;
    case "melodee:auto_sample": // Melodee's sheet over the pages; they come back on Module, its menu open
        tell("action", "auto_sample");
        return true;
    default:
        result = false;
    }
    if (gridActionOpenedSomething(view !== viewBefore)) {
        componentModalFromGrid = true;
        componentGridReturnSlot = s;
        componentGridReturnKey = componentKey;
        componentGridReturnModule = id || "";
    }
    return result;
}
// ...and back to the grid when the screen handed off to is done with (every
// way out of the preset list, the delete question and the picker arrives on
// the chain editor): on "My Presets" while the module is still there; a
// swapped-in module's own pages (Melodee starts them, a new instance; this
// one holds on Schwung's loading screen meanwhile); nothing with none.
function maybeReturnToComponentGrid() {
    if (!componentModalFromGrid) return false;
    if (isTextEntryActive()) return false;
    componentModalFromGrid = false;
    const moduleBefore = componentGridReturnModule, s = componentGridReturnSlot, componentKey = componentGridReturnKey;
    componentGridReturnModule = "";
    componentGridReturnSlot = -1;
    componentGridReturnKey = "";
    if (s !== slot || !positionModule) return false;
    if (moduleBefore && positionModule !== moduleBefore) {
        componentGridReturnEnter = true;
        componentLoadHold = { slot: s, componentKey, attempts: 0, ticks: 0, entry: null, handover: true };
        tell("action", "swap " + positionModule);
        setView(VIEWS.COMPONENT_LOADING);
        announce(`${componentLoadHoldLabel()}, loading`);
        needsRedraw = true;
        return true;
    }
    const enterOnArrival = componentGridReturnEnter;
    componentGridReturnEnter = true;
    enterParamPages(s, componentKey, prefixOf(componentKey), "My Presets", componentParamPagesIo(s, componentKey), null,
                    { enter: enterOnArrival });
    needsRedraw = true;
    return true;
}

// The grid's io: its trailing pages and their rows, and every write it
// makes marked for the `*`.
function componentParamPagesIo(s, componentKey = component) {
    return {
        trailingMenus: () => componentTrailingMenus(s, componentKey, prefixOf(componentKey)),
        runAction: (action) => runComponentActionFromGrid(s, componentKey, action),
        setParam: (key, value) => {
            const ok = setSlotParam(s, key, value);
            markComponentParamWrite(s, componentKey);
            return ok;
        },
        heldStepIsHold: () => false,
        deleteHeld: () => false,
        onValueWritten: () => {},
    };
}

// ---------------------------------------------------------------- ctx, as shadow_ui.js fills it

let muteHeld = false;
Object.defineProperty(ctx, "view", { get() { return view; }, set(v) { view = v; }, enumerable: true });
Object.defineProperty(ctx, "needsRedraw", { get() { return needsRedraw; }, set(v) { needsRedraw = v; }, enumerable: true });
Object.defineProperty(ctx, "selectedSlot", { get() { return slot; }, set() { /* one slot here */ }, enumerable: true });
Object.defineProperty(ctx, "helpNavStack", { get() { return helpNavStack; }, enumerable: true });
Object.defineProperty(ctx, "helpDetailScrollState", { get() { return helpDetailScrollState; }, enumerable: true });
Object.assign(ctx, {
    VIEWS,
    setView,
    getSlotParam,
    setSlotParam,
    getSlotStateWithRetry,
    requestRedraw: () => { needsRedraw = true; },
    evaluateVisibilityCondition,
    // A turned door: Schwung's LFO Target cells are slot settings, never a
    // module's; a file cell is the folder's file list.
    turnParamDoor: (s, fullKey, dir, knob, held, info) => fileFlatEnter(s, fullKey, knob, held, info),
    noteLaneWriteRefusal: () => {},
    isParamModulated: () => false,
    loadCardScript,
    drawCanvasPageBody,
    canvasPageHook,
    getModuleDisplayName,
    getModuleAbbrev,
    userPresetHeaderMark: (s, componentKey) => userPresetHeaderMark(s, componentKey),
    // The user presets' screens (shadow_ui_presets.mjs) report what they did.
    onPresetSaved: (s, prefix, name) => onUserPresetSaved(s, prefix, name),
    onPresetLoaded: (s, prefix, name) => onUserPresetLoaded(s, prefix, name),
    onPresetDeleted: (s, prefix, name) => onUserPresetDeleted(s, prefix, name),
    auditionEnabled: () => previewEnabled,
    // The help viewer's (shadow_ui_settings.mjs draws through these).
    drawHelpList: () => drawHelpList(),
    drawHelpDetail: () => drawHelpDetail(),
    isMuteHeld: () => muteHeld,
    runSlotAction: () => {},
    openEnumPicker: (o) => openEnumPicker(o),
    openParamEditor: (s, fullKey, meta) => openParamEditorFromGrid(s, fullKey, meta),
    showWarning: (...args) => showWarning(...args),
    isTextEntryActive: () => isTextEntryActive(),
    drawTextEntry: () => drawTextEntry(),
    updateFocusedSlot: () => {},
});

// ---------------------------------------------------------------- entering

function enter(entry) {
    // What a screen left behind when Melodee took the pages away: a preset
    // list's audition put back, a hand-off's way back forgotten.
    if (isTextEntryActive()) closeTextEntry();
    if (view === VIEWS.PRESETS) presetsUi.handlePresetsBack();
    componentModalFromGrid = false;
    componentGridReturnEnter = true;
    componentHelpReturnSlot = -1;
    helpNavStack = [];
    helpDetailScrollState = null;
    moduleListsResetSession();
    if (view === VIEWS.CANVAS) invokeCanvasOverlayHook("onExit", { cancelled: true });
    resetCanvasState();
    filepathBrowserState = null;
    filepathBrowserParamKey = "";
    fileFlatKnob = -1;
    enumPickerCommit = null;
    enumPickerOptions = [];
    componentLoadHold = null;
    paramEditorOpenedFromGrid = false;
    paramEditorReturnPage = "";
    pendingHierKnobIndex = pendingKnobIndex = -1;
    pendingHierKnobDelta = pendingKnobDelta = 0;
    pendingKnobRefresh = false;
    knobTouched.fill(false);
    hideOverlay();
    exitHierarchyEditor();
    if (paramPagesActive()) exitParamPages();
    padBlock = -1; // restated by the next tick
    slot = Number(entry.slot) || 0;
    component = String(entry.component || "synth");
    moduleId = String(entry.module || "");
    moduleDir = String(entry.dir || "");
    moduleName = String(entry.name || "");
    moduleAbbrev = String(entry.abbrev || "");
    cacheModuleAbbrev({ id: moduleId, abbrev: moduleAbbrev, name: moduleName });
    positionModule = moduleId;
    presetsOffered = !!entry.presets;
    autoSampleOffered = !!entry.instrument;
    readUserPresetRecord();
    view = VIEWS.CHAIN_EDIT;
    openComponentEditor(slot, component, entry);
    needsRedraw = true;
}
function takeEntry() {
    const text = seam("entry");
    if (!text) return;
    try { enter(JSON.parse(text)); } catch (e) { log("shadow", "entry: " + e + "\n" + (e && e.stack)); }
}
let statesRead = false;
function readStates() {
    if (statesRead) return;
    statesRead = true;
    try {
        const kept = JSON.parse(seam("page_states") || "{}") || {};
        for (const k of Object.keys(kept)) canvasPageStates[k] = typeof kept[k] === "string" ? JSON.parse(kept[k]) : kept[k];
    } catch (e) { canvasPageStates = {}; }
}

// ---------------------------------------------------------------- the tick

const TICK_MS = 1000 / 60; // shadow_ui.c paces its tick to 60 Hz
const MAX_TICKS_PER_FRAME = 8;
let lastTick = 0, redrawCounter = 0;

// The pads are the keyboard's only while a keyboard is up (shadow_ui.js's
// reconcilePadBlock: Melodee runs no module UI that could want them).
function reconcilePadBlock() {
    if (isTextEntryActive()) return;
    host_pad_block(0);
}

// shadow_ui.js's tick, the parts these screens have, in its order; then its
// draw, every other tick unless something asked for one. (Not here: its
// snapshots, scenes, step menu, external surfaces, tools and the rest of
// Schwung's own screens.)
function tick() {
    fileFlatTick();
    reconcilePadBlock();
    presetsUi.tickPresetPreview(); // a preset list's audition, a moment after the cursor stops
    if (view === VIEWS.PARAM_PAGES) tickParamPages();
    tickUserPresetStale();
    const fb = filepathBrowserState;
    if (fb && fb.livePreviewEnabled && fb.previewPendingPath && Date.now() - fb.previewPendingTime >= 150) {
        applyLivePreview(fb, { kind: "file", path: fb.previewPendingPath });
        fb.previewPendingPath = "";
        fb.previewPendingTime = 0;
    }
    moduleListsTickPendingName();
    if (isTextEntryActive() && tickTextEntry()) needsRedraw = true;
    if (tickOverlay()) needsRedraw = true;
    refreshPendingKnobOverlay();
    processPendingHierKnob();
    if (view === VIEWS.CANVAS) {
        tickCanvasPreview();
        canvasTickCounter = (canvasTickCounter || 0) + 1;
        if (canvasTickCounter % 3 === 0) needsRedraw = true; // an animated editor drawn at 20 fps at least
    }
    redrawCounter++;
    if (!needsRedraw && redrawCounter % REDRAW_INTERVAL !== 0) return;
    needsRedraw = false;
    drawView();
}

function drawView() {
    // Back from a screen a trailing page's row handed off to, before the
    // draw (shadow_ui.js's reconciles): the frame that notices is the grid's.
    if (view === VIEWS.GLOBAL_SETTINGS) maybeReturnToComponentHelp();
    if (view === VIEWS.CHAIN_EDIT) maybeReturnToComponentGrid();
    serviceComponentLoadHold(); // a probe that lands changes the view: drawn now
    if (view === VIEWS.CHAIN_EDIT) { tell("exit", ""); return; } // Schwung's chain editor: Melodee's Device View
    try {
        switch (view) {
        case VIEWS.PARAM_PAGES:
            if (!drawParamPages()) { // a page kind the grid hands to the list editor (none in 1.9.4)
                clear_screen();
                print(Math.max(0, (SCREEN_WIDTH - text_width("Loading...")) >> 1), 28, "Loading...", 1);
            }
            break;
        case VIEWS.COMPONENT_LOADING: drawComponentLoading(); break;
        case VIEWS.COMPONENT_EDIT: drawComponentEdit(); break;
        case VIEWS.HIERARCHY_EDITOR: clear_screen(); break; // the list editor, only ever under the keyboard (which clears)
        case VIEWS.CANVAS: drawCanvasPreview(); break;
        case VIEWS.FILEPATH_BROWSER: drawFilepathBrowser(); break;
        case VIEWS.FILE_FLAT: drawFileFlat(); break;
        case VIEWS.ENUM_PICKER: drawEnumPicker(); break;
        case VIEWS.PRESETS: presetsUi.drawPresets(); break;
        case VIEWS.PRESET_DETAIL: presetsUi.drawPresetDetail(); break;
        case VIEWS.GLOBAL_SETTINGS: settingsUi.drawGlobalSettings(); break;
        case VIEWS.COMPONENT_SELECT: drawComponentSelect(); break;
        case VIEWS.MODULE_LISTS: drawModuleLists(); break;
        case VIEWS.MODULE_LISTS_EDIT: drawModuleListsEdit(); break;
        case VIEWS.MODULE_LISTS_ACTIONS: drawModuleListsActions(); break;
        default: break;
        }
        // Over the view, in shadow_ui.js's order: the keyboard, the warning
        // card, the value card. (Its feedback gate, snapshot toast and marks,
        // CC learn footer, scene badge and fader, p-lock mark and step menu
        // are Schwung's own features, not here.)
        if (view !== VIEWS.CANVAS) {
            if (isTextEntryActive()) drawTextEntry();
            if (warningActive) drawMessageOverlay(warningTitle, warningLines);
            drawOverlay();
        }
    } catch (e) {
        // shadow_ui.js says "UI error, recovering" and leaves for its slots;
        // here the way out is the Device View.
        log("shadow", "draw: " + e + "\n" + (e && e.stack));
        clear_screen();
        print(10, 22, "UI error, recovering", 1);
        print(10, 34, String(e).substring(0, 21), 1);
        if (paramPagesActive()) exitParamPages();
        setView(VIEWS.CHAIN_EDIT);
        tell("exit", "");
    }
}

function status() {
    const page = view === VIEWS.PARAM_PAGES ? currentParamPage() : null;
    let footer = [];
    try { footer = view === VIEWS.PARAM_PAGES ? paramPagesFooterHints() || [] : view === VIEWS.ENUM_PICKER ? enumPickerFooterHints() : []; }
    catch (e) { footer = []; }
    let rows = page && Array.isArray(page.entries)
        ? page.entries.map((e) => [String(e.label || ""), typeof e.value === "string" || typeof e.value === "number" ? String(e.value) : ""])
        : [];
    let title = "", index = -1;
    if (view === VIEWS.ENUM_PICKER) {
        title = enumPickerTitle;
        index = enumPickerIndex;
        rows = enumPickerOptions.map((o, i) => [o, i === enumPickerOpenIndex ? "*" : ""]);
    } else if (view === VIEWS.FILEPATH_BROWSER && filepathBrowserState) {
        title = filepathBrowserState.title || "";
        index = filepathBrowserState.selectedIndex;
        rows = (filepathBrowserState.items || []).map((it) => [String(it.label || ""), it.kind || ""]);
    } else if (view === VIEWS.FILE_FLAT) {
        title = fileFlatFolder;
        index = fileFlatIndex;
        rows = fileFlatRows.map((it, i) => [String(it.label || ""), i === fileFlatStored ? "*" : ""]);
    } else if (view === VIEWS.COMPONENT_SELECT) {
        title = `S${selectSlot + 1} > ${componentLabel(selectComponentKey)}`;
        index = selectedModuleIndex;
        rows = availableModules.map((m) => [String((m && (m.type === "divider" ? m.label : m.name || m.id)) || ""),
                                            m && m.value !== undefined ? String(m.value) : m && positionModule && m.id === positionModule ? "*" : ""]);
    } else if (view === VIEWS.MODULE_LISTS || view === VIEWS.MODULE_LISTS_EDIT || view === VIEWS.MODULE_LISTS_ACTIONS) {
        const list = view === VIEWS.MODULE_LISTS ? moduleListsRows() : view === VIEWS.MODULE_LISTS_EDIT ? moduleListsEditRows()
            : moduleListsActionRows();
        title = view === VIEWS.MODULE_LISTS ? getModuleDisplayName(moduleListsModuleId) : view === VIEWS.MODULE_LISTS_EDIT ? "Edit Lists" : moduleListsTarget;
        index = view === VIEWS.MODULE_LISTS ? moduleListsMemberIndex : view === VIEWS.MODULE_LISTS_EDIT ? moduleListsEditIndex : moduleListsActionIndex;
        rows = list.map((r) => [String(r.name), String(r.value || "")]);
    } else if (view === VIEWS.GLOBAL_SETTINGS && helpNavStack.length > 0) {
        const frame = helpNavStack[helpNavStack.length - 1];
        title = helpDetailScrollState ? String(frame.items[frame.selectedIndex].title) : helpHeaderTitle(frame);
        index = helpDetailScrollState ? -1 : frame.selectedIndex;
        rows = helpDetailScrollState ? [] : frame.items.map((item) => [String(item.title || ""), ""]);
    }
    return JSON.stringify({
        view,
        page: (view === VIEWS.PARAM_PAGES && paramPagesPageName()) || "",
        kind: page ? page.kind : "",
        canvas: !!(page && page.canvas),
        picker: view === VIEWS.PARAM_PAGES && paramPagesPickerOpen(),
        typing: isTextEntryActive(),
        text: isTextEntryActive() ? getTextEntryBuffer() : "",
        warning: warningActive,
        title,
        index,
        footer,
        rows,
        said: lastSaid,
    });
}

// ---------------------------------------------------------------- input

// shadow_ui.js's handleJog, handleSelect and handleBack, for these screens.
function handleJog(delta) {
    hideOverlay();
    switch (view) {
    case VIEWS.COMPONENT_EDIT: changeComponentPreset(delta); break;
    case VIEWS.FILEPATH_BROWSER: filepathBrowserJog(delta); break;
    case VIEWS.FILE_FLAT: fileFlatMove(delta > 0 ? 1 : -1); break;
    case VIEWS.ENUM_PICKER: enumPickerJog(delta); break;
    case VIEWS.PRESETS: presetsUi.handlePresetsJog(delta); break;
    case VIEWS.PRESET_DETAIL: presetsUi.handlePresetDetailJog(delta); break;
    case VIEWS.COMPONENT_SELECT: componentSelectJog(delta); break;
    case VIEWS.MODULE_LISTS: case VIEWS.MODULE_LISTS_EDIT: case VIEWS.MODULE_LISTS_ACTIONS: moduleListsJog(delta); break;
    case VIEWS.GLOBAL_SETTINGS: helpJog(delta); break;
    default: break; // a canvas has the turn through its onMidi
    }
    needsRedraw = true;
}
function handleSelect() {
    hideOverlay();
    switch (view) {
    case VIEWS.CANVAS:
        if (canvasIsEnterable()) break;
        closeCanvasPreview(false);
        announce("Hierarchy Editor");
        break;
    case VIEWS.FILEPATH_BROWSER: filepathBrowserSelect(); break;
    case VIEWS.FILE_FLAT: fileFlatCommit(); break;
    case VIEWS.ENUM_PICKER: closeEnumPicker(true); break;
    case VIEWS.PRESETS: presetsUi.handlePresetsSelect(); break;
    case VIEWS.PRESET_DETAIL: presetsUi.handlePresetDetailSelect(); break;
    case VIEWS.COMPONENT_SELECT: {
        const picked = availableModules[selectedModuleIndex];
        if (picked && !pickerIsControlRow(picked) && !ModuleCategories.isCategoryHeader(picked))
            announce(`Loading ${picked.name || picked.id || "module"}`);
        applyComponentSelection();
        break;
    }
    case VIEWS.MODULE_LISTS: case VIEWS.MODULE_LISTS_EDIT: case VIEWS.MODULE_LISTS_ACTIONS: moduleListsSelect(); break;
    case VIEWS.GLOBAL_SETTINGS: helpSelect(); break;
    default: break;
    }
    needsRedraw = true;
}
function handleBack() {
    if (view === VIEWS.FILE_FLAT) { fileFlatCancel(); return; }
    hideOverlay();
    switch (view) {
    case VIEWS.COMPONENT_EDIT:
        setView(VIEWS.CHAIN_EDIT);
        announce("Chain Editor");
        needsRedraw = true;
        break;
    case VIEWS.COMPONENT_LOADING: announce(cancelComponentLoadHold()); break;
    case VIEWS.FILEPATH_BROWSER:
        closeHierarchyFilepathBrowser();
        announce("Parameters");
        needsRedraw = true;
        break;
    case VIEWS.CANVAS:
        closeCanvasPreview(true);
        announce("Hierarchy Editor");
        needsRedraw = true;
        break;
    case VIEWS.HIERARCHY_EDITOR: leaveListEditor(); break; // opened from the grid: back to it
    case VIEWS.ENUM_PICKER: closeEnumPicker(false); break;
    case VIEWS.PRESETS: presetsUi.handlePresetsBack(); break;
    case VIEWS.PRESET_DETAIL: presetsUi.handlePresetDetailBack(); break;
    case VIEWS.COMPONENT_SELECT: // to the chain editor, and from there the grid (maybeReturnToComponentGrid)
        setView(VIEWS.CHAIN_EDIT);
        announce("Chain Editor");
        needsRedraw = true;
        break;
    case VIEWS.MODULE_LISTS: case VIEWS.MODULE_LISTS_EDIT: case VIEWS.MODULE_LISTS_ACTIONS: moduleListsBack(); break;
    case VIEWS.GLOBAL_SETTINGS: helpBack(); break;
    default: break;
    }
}

// What the shim forwards to the shadow UI while it is up (Shift is polled,
// never sent), routed as shadow_ui.js's onMidiMessageInternal routes it for
// these screens: the grid first while no keyboard is over it, a dived-into
// canvas's own exits, the keyboard, the canvas, the warning, then the
// view's jog, click, Back, knobs and knob touches.
function midi(data) {
    const status = data[0], d1 = data[1], d2 = data[2], kind = status & 0xf0;
    if (view === VIEWS.PARAM_PAGES && paramPagesActive() && !isTextEntryActive()) {
        if (maybeDismissWarningFromInput(status, d1, d2)) { needsRedraw = true; return; }
        if (handleParamPagesMidi(data)) { needsRedraw = true; return; }
    }
    if (kind === 0xb0 && d1 === 88) muteHeld = d2 > 0;
    if (view === VIEWS.CANVAS && kind === 0xb0) {
        const enterable = canvasIsEnterable();
        if (d1 === CC_JOG && isShiftHeld() && d2 !== 0) {
            closeCanvasPreview(true); // the escape: and the turn goes on to what is under it
            needsRedraw = true;
        } else if (d1 === CC_CLICK && d2 > 0 && !enterable) {
            closeCanvasPreview(false);
            announce("Hierarchy Editor");
            needsRedraw = true;
            return;
        }
        if (d1 === CC_BACK && d2 > 0) {
            if (enterable) { // the module first: true is "I went up a level"
                const up = canvasOverlayHookResult("handleBack");
                if (consumeCanvasCloseRequest()) return;
                if (up === true) { needsRedraw = true; return; }
            }
            closeCanvasPreview(true);
            announce("Hierarchy Editor");
            needsRedraw = true;
            return;
        }
    }
    if (isTextEntryActive() && handleTextEntryMidi(data)) { needsRedraw = true; return; }
    if (dispatchCanvasMidi(data, "internal")) { needsRedraw = true; return; }
    if (maybeDismissWarningFromInput(status, d1, d2)) return;
    if (kind === 0xb0) {
        if (d1 === CC_JOG) { const delta = decodeDelta(d2); if (delta !== 0) handleJog(delta); return; }
        if (d1 === CC_CLICK && d2 > 0) { handleSelect(); return; }
        if (d1 === CC_BACK && d2 > 0) { handleBack(); return; }
        if (d1 >= KNOB_CC_START && d1 <= KNOB_CC_END) {
            const k = d1 - KNOB_CC_START, delta = decodeDelta(d2);
            // A list up: any knob scrolls it (the grid's own value behind it
            // stays as it was).
            if (view === VIEWS.FILE_FLAT) { fileFlatKnobTurn(delta); return; }
            if (view === VIEWS.ENUM_PICKER) {
                const step = listKnobStep(enumPickerKnob, delta, Date.now(), enumPickerOptions.length);
                if (step) enumPickerJog(step);
                return;
            }
            if (view === VIEWS.FILEPATH_BROWSER) {
                const step = listKnobStep(filepathBrowserKnob, delta, Date.now(), filepathBrowserState ? filepathBrowserState.items.length : 0);
                if (step) filepathBrowserJog(step);
                return;
            }
            if (adjustKnobAndShow(k, delta)) return;
            handleKnobTurn(k, delta);
        }
        return;
    }
    if (kind === 0x90 && d2 > 0 && d1 < NUM_KNOBS) { // a knob touched
        knobTouched[d1] = true;
        if (view === VIEWS.ENUM_PICKER || view === VIEWS.FILEPATH_BROWSER) return; // the list is the answer
        if (view === VIEWS.FILE_FLAT) { if (d1 === fileFlatKnob) fileFlatHeld = true; return; }
        if (showKnobOverlay(d1)) return;
        handleKnobTurn(d1, 0);
        return;
    }
    if (((kind === 0x90 && d2 === 0) || kind === 0x80) && d1 < NUM_KNOBS) { // let go
        knobTouched[d1] = false;
        if (fileFlatKnob === d1 && view === VIEWS.FILE_FLAT) { fileFlatCommit(); return; } // letting go loads
        if (pendingHierKnobIndex === d1) {
            processPendingHierKnob();
            pendingHierKnobIndex = -1;
            pendingHierKnobDelta = 0;
        }
        if (pendingKnobIndex === d1 && pendingKnobDelta !== 0) refreshPendingKnobOverlay();
    }
}

// ---------------------------------------------------------------- Melodee's hooks

globalThis.canvas_overlay = {
    // A frame: Schwung's ticks since the last, each drawing when Schwung's
    // would; the screen is what the last draw left.
    drawPage(c) {
        melo = c;
        readStates();
        takeEntry();
        const now = Date.now();
        if (!lastTick) lastTick = now - TICK_MS;
        let n = Math.min(MAX_TICKS_PER_FRAME, Math.floor((now - lastTick) / TICK_MS));
        lastTick = n >= MAX_TICKS_PER_FRAME ? now : lastTick + n * TICK_MS;
        while (n-- > 0) {
            try { tick(); } catch (e) { log("shadow", "tick: " + e + "\n" + (e && e.stack)); } // the next tick comes anyway
        }
        keepStates();
        tell("status", status());
    },
    // A control, raw, in Schwung's numbers.
    onMidi(c, msg) {
        melo = c;
        readStates();
        takeEntry();
        if (msg && msg.data) {
            try { midi([msg.data[0], msg.data[1], msg.data[2]]); } catch (e) { log("shadow", "midi: " + e + "\n" + (e && e.stack)); }
        }
        keepStates();
        tell("status", status());
    },
};
