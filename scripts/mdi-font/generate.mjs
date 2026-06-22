/*
 * Generate an LVGL font subset of Material Design Icons.
 *
 * Reads the icon names from icons.txt, resolves each to its MDI codepoint via
 * the @mdi/font metadata, then:
 *   1. runs lv_font_conv to emit src/ui/fonts/mdi_font.c (the LVGL font), and
 *   2. emits src/ui/mdi_icons.h / mdi_icons.cpp — name<->glyph lookup so C++
 *      code can resolve HA's `attributes.icon` ("mdi:lightbulb") to a glyph.
 *
 * Run from scripts/mdi-font:  npm install && npm run gen
 */
import { readFileSync, writeFileSync, mkdirSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, resolve, join } from "node:path";
import { execFileSync } from "node:child_process";

const __dirname = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(__dirname, "..", "..");

// Render size of the icon font. Bigger = crisper tiles but more flash.
const FONT_SIZE = 40;
const BPP = 4; // anti-aliasing depth

const ttf = resolve(
  __dirname,
  "node_modules/@mdi/font/fonts/materialdesignicons-webfont.ttf"
);
const variablesScss = resolve(
  __dirname,
  "node_modules/@mdi/font/scss/_variables.scss"
);

const outFontDir = join(repoRoot, "src", "ui", "fonts");
const outFontC = join(outFontDir, "mdi_font.c");
const outHeader = join(repoRoot, "src", "ui", "mdi_icons.h");
const outImpl = join(repoRoot, "src", "ui", "mdi_icons.cpp");

// --- read requested icon names ------------------------------------------------
const requested = readFileSync(join(__dirname, "icons.txt"), "utf8")
  .split(/\r?\n/)
  .map((l) => l.trim())
  .filter((l) => l && !l.startsWith("#"));

// --- parse name -> codepoint map from @mdi/font ------------------------------
// _variables.scss holds:  "lightbulb": F0335,
const scss = readFileSync(variablesScss, "utf8");
const map = new Map();
for (const m of scss.matchAll(/"([a-z0-9-]+)":\s*([0-9A-Fa-f]{4,6})/g)) {
  map.set(m[1], parseInt(m[2], 16));
}

const icons = [];
const missing = [];
for (const name of requested) {
  const cp = map.get(name);
  if (cp == null) {
    missing.push(name);
    continue;
  }
  icons.push({ name, cp });
}
if (missing.length) {
  console.error("Unknown MDI names (skipped): " + missing.join(", "));
}
if (!icons.length) {
  console.error("No valid icons resolved — aborting.");
  process.exit(1);
}

// --- run lv_font_conv --------------------------------------------------------
mkdirSync(outFontDir, { recursive: true });
const ranges = icons.map((i) => "0x" + i.cp.toString(16).toUpperCase());
const lvArgs = [
  "lv_font_conv",
  "--font", ttf,
  "--size", String(FONT_SIZE),
  "--bpp", String(BPP),
  "--format", "lvgl",
  "--lv-font-name", "mdi_font",
  "--no-compress",
  "-o", outFontC,
  "-r", ranges.join(","),
];
console.log(`Converting ${icons.length} glyphs at ${FONT_SIZE}px...`);
execFileSync("npx", lvArgs, { stdio: "inherit", cwd: __dirname, shell: process.platform === "win32" });

// Larger MDI subset for the on-screen quick-settings tiles (same glyphs, bigger
// render) so settings icons read clearly without enlarging the dashboard font.
const BIG_FONT_SIZE = 64;
const BIG_NAMES = ["rotate-right", "screen-rotation"];
const bigIcons = BIG_NAMES.map((n) => ({ name: n, cp: map.get(n) })).filter((i) => i.cp != null);
if (bigIcons.length) {
  const bigRanges = bigIcons.map((i) => "0x" + i.cp.toString(16).toUpperCase());
  const bigArgs = [
    "lv_font_conv",
    "--font", ttf,
    "--size", String(BIG_FONT_SIZE),
    "--bpp", String(BPP),
    "--format", "lvgl",
    "--lv-font-name", "mdi_font_lg",
    "--no-compress",
    "-o", join(outFontDir, "mdi_font_lg.c"),
    "-r", bigRanges.join(","),
  ];
  console.log(`Converting ${bigIcons.length} large glyphs at ${BIG_FONT_SIZE}px...`);
  execFileSync("npx", bigArgs, { stdio: "inherit", cwd: __dirname, shell: process.platform === "win32" });
}

// --- UTF-8 encode a codepoint ------------------------------------------------
function utf8Bytes(cp) {
  const b = Buffer.from(String.fromCodePoint(cp), "utf8");
  return [...b];
}
function cEscaped(cp) {
  return utf8Bytes(cp).map((x) => "\\x" + x.toString(16).toUpperCase().padStart(2, "0")).join("");
}
function macroName(name) {
  return "MDI_" + name.toUpperCase().replace(/-/g, "_");
}

// --- emit header -------------------------------------------------------------
let h = `/*
 * Material Design Icons for the LVGL UI — GENERATED, do not edit by hand.
 * Regenerate via scripts/mdi-font (npm run gen). Source of truth: icons.txt.
 *
 * Each MDI_* macro is the UTF-8 glyph string for use with the mdi_font font.
 */
#ifndef UI_MDI_ICONS_H
#define UI_MDI_ICONS_H

#include <lvgl.h>

LV_FONT_DECLARE(mdi_font);
LV_FONT_DECLARE(mdi_font_lg);  /* larger subset for on-screen settings tiles */

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve an MDI name ("lightbulb", with or without a leading "mdi:") to its
 * glyph string, or nullptr if that icon isn't bundled in the font. */
const char *mdiGlyph(const char *name);

#ifdef __cplusplus
}
#endif

`;
for (const { name, cp } of icons) {
  h += `#define ${macroName(name)} "${cEscaped(cp)}" /* mdi:${name} U+${cp
    .toString(16)
    .toUpperCase()} */\n`;
}
h += `\n#endif /* UI_MDI_ICONS_H */\n`;
writeFileSync(outHeader, h);

// --- emit impl (lookup table) ------------------------------------------------
let c = `/* GENERATED, do not edit by hand — see scripts/mdi-font. */
#include "mdi_icons.h"

#include <string.h>

namespace {
struct MdiEntry { const char *name; const char *glyph; };
const MdiEntry kIcons[] = {
`;
for (const { name } of icons) {
  c += `    {"${name}", ${macroName(name)}},\n`;
}
c += `};
} // namespace

const char *mdiGlyph(const char *name) {
    if (!name) return nullptr;
    if (strncmp(name, "mdi:", 4) == 0) name += 4; // tolerate HA's "mdi:" prefix
    for (const auto &e : kIcons) {
        if (strcmp(e.name, name) == 0) return e.glyph;
    }
    return nullptr;
}
`;
writeFileSync(outImpl, c);

console.log(`OK: ${icons.length} icons -> ${outFontC}`);
console.log(`     header -> ${outHeader}`);
console.log(`     impl   -> ${outImpl}`);

// --- emit web SVG sprite (same icons.txt -> device font AND web preview) ------
// The web editor injects this sprite into its DOM so <use href="#mdi-<name>">
// renders real MDI icons offline (AP mode, no CDN), recolored via currentColor.
const svgDir = resolve(__dirname, "node_modules/@mdi/svg/svg");
const outSprite = join(repoRoot, "web", "src", "mdi-sprite.svg");
let symbols = "";
let spriteCount = 0;
for (const { name } of icons) {
  const svgPath = join(svgDir, `${name}.svg`);
  if (!existsSync(svgPath)) {
    console.error(`No @mdi/svg file for: ${name} (sprite skipped)`);
    continue;
  }
  const svg = readFileSync(svgPath, "utf8");
  const m = svg.match(/\bd="([^"]+)"/);
  if (!m) {
    console.error(`No path data in svg for: ${name}`);
    continue;
  }
  symbols += `<symbol id="mdi-${name}" viewBox="0 0 24 24"><path fill="currentColor" d="${m[1]}"/></symbol>`;
  spriteCount++;
}
const sprite =
  `<svg xmlns="http://www.w3.org/2000/svg" style="display:none">${symbols}</svg>\n`;
writeFileSync(outSprite, sprite);
console.log(`     web sprite -> ${outSprite} (${spriteCount} icons)`);

// --- emit accented Montserrat text fonts (French range) ----------------------
// The built-in lv_font_montserrat_* only cover ASCII, so accented entity names
// (é, è, à, ç, °, ·, …) render as boxes on screen. Generate our own subsets from
// the bundled Montserrat TTF covering ASCII + Latin-1 + a few French extras.
const montTtf = resolve(__dirname, "Montserrat-Medium.ttf");
const TEXT_SIZES = [14, 18, 24, 28, 32, 40];
// ASCII, Latin-1 Supplement (accents, °, ·, « »), Œ/œ, curly quotes, euro.
const TEXT_RANGE = "0x20-0x7F,0xA0-0xFF,0x152-0x153,0x2018-0x201F,0x20AC";
let textHeader = `/*
 * Accented Montserrat text fonts for the LVGL UI — GENERATED, do not edit.
 * Regenerate via scripts/mdi-font (npm run gen). Covers ASCII + French accents
 * so entity names render correctly (the built-in montserrat fonts are ASCII).
 */
#ifndef UI_TEXT_FONTS_H
#define UI_TEXT_FONTS_H

#include <lvgl.h>

`;
for (const sz of TEXT_SIZES) {
  const name = `montserrat_fr_${sz}`;
  const out = join(outFontDir, `${name}.c`);
  const args = [
    "lv_font_conv",
    "--font", montTtf,
    "--size", String(sz),
    "--bpp", String(BPP),
    "--format", "lvgl",
    "--lv-font-name", name,
    "--no-compress",
    "-o", out,
    "-r", TEXT_RANGE,
  ];
  console.log(`Converting Montserrat ${sz}px (FR range)...`);
  execFileSync("npx", args, { stdio: "inherit", cwd: __dirname, shell: process.platform === "win32" });
  textHeader += `LV_FONT_DECLARE(${name});\n`;
}
textHeader += `\n#endif /* UI_TEXT_FONTS_H */\n`;
writeFileSync(join(repoRoot, "src", "ui", "text_fonts.h"), textHeader);
console.log(`     text fonts -> src/ui/fonts/montserrat_fr_*.c + src/ui/text_fonts.h`);
