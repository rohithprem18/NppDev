#!/usr/bin/env node
// Fails if any <button> in src/components has neither an aria-label nor
// visible text content (i.e. icon-only buttons a screen reader would announce
// as just "button"). Run with: node scripts/check-aria-labels.mjs
import { readdirSync, readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const componentsDir = join(root, "src", "components");

/** Strip JSX expressions ({...}, balanced) and child tags, leaving text. */
function visibleText(jsx) {
  let s = jsx;
  let prev;
  do {
    prev = s;
    // Innermost JSX expressions: drop them, but keep any string literals in
    // them ({back ? `Query in x` : "Copy SELECT"} still renders text).
    s = s.replace(/\{([^{}]*)\}/g, (_, expr) => {
      const strings = [...expr.matchAll(/["'`]([^"'`]*)["'`]/g)].map((m) => m[1]);
      return " " + strings.join(" ") + " ";
    });
  } while (s !== prev);
  s = s.replace(/<\/?[A-Za-z][^>]*>/g, " "); // child tags
  return s.replace(/\s+/g, " ").trim();
}

const problems = [];
for (const file of readdirSync(componentsDir).filter((f) => f.endsWith(".tsx"))) {
  const path = join(componentsDir, file);
  const src = readFileSync(path, "utf8");
  const lines = src.split("\n");
  // Find each <button ...> ... </button> (buttons here never nest).
  const buttonRe = /<button\b([^>]*)>([\s\S]*?)<\/button>/g;
  let m;
  while ((m = buttonRe.exec(src)) !== null) {
    const attrs = m[1];
    const body = m[2];
    const line = src.slice(0, m.index).split("\n").length;
    if (/\baria-label\s*=/.test(attrs)) continue;
    if (visibleText(body).length > 0) continue; // has visible text: that is the accessible name
    problems.push(`${file}:${line} icon-only <button> has no aria-label (attrs: ${attrs.trim().slice(0, 80) || "none"})`);
  }
  // Every raw <svg inside a button should be hidden from assistive tech.
  const svgRe = /<button\b([^>]*)>([\s\S]*?)<\/button>/g;
  while ((m = svgRe.exec(src)) !== null) {
    const line = src.slice(0, m.index).split("\n").length;
    const svgs = m[2].match(/<svg\b[^>]*>/g) || [];
    for (const svg of svgs) {
      if (!/aria-hidden\s*=\s*"true"/.test(svg)) {
        problems.push(`${file}:${line} <svg> inside a button lacks aria-hidden="true"`);
      }
    }
  }
  void lines;
}

if (problems.length > 0) {
  console.error("aria-label check failed:\n  " + problems.join("\n  "));
  process.exit(1);
}
console.log(`aria-label check passed: every <button> in ${componentsDir} has an accessible name`);
