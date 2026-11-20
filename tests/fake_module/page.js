// SPDX-License-Identifier: AGPL-3.0-only
// The stand-in module's own page, for the tests: Schwung's canvas page
// contract (docs/CANVAS_PAGES.md) in a few lines. A turn sets the level, a
// push goes a level down and asks for text, Back comes up a level at a time,
// and the page draws a block, the level as a dot, and proof that QuickJS's
// std module is there.
import * as std from 'std';

globalThis.canvas_overlay = {
  onMidi(ctx, msg) {
    const d = msg.data;
    if (d[1] === 14) ctx.setParam('level', d[2] < 64 ? '0.9' : '0.1');
    if (d[1] === 3 && d[2] > 0) {
      ctx.state.depth = (ctx.state.depth || 0) + 1;
      ctx.openTextEntry({ title: 'Search', initial: 'ab' });
    }
  },
  onTextEntry(ctx, p) {
    if (p && !p.cancelled) ctx.setParam('searched', p.text);
  },
  handleBack(ctx) {
    if ((ctx.state.depth || 0) > 0) { ctx.state.depth -= 1; return true; }
    return false;
  },
  drawPage(ctx, info) {
    ctx.fillRect(0, 0, 10, 10, 1);
    const level = parseFloat((info.values && info.values.level) || '0');
    ctx.setPixel(Math.round(level * 100), 30, 1);
    if (typeof std.getenv === 'function') ctx.fillRect(0, 40, 3, 3, 1);
    ctx.print(20, 0, 'BROWSE', 1);
  },
};
