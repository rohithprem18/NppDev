// Loaded by NppDev's Run (node --require) before a .js file, so the file itself needs no
// input-reading code. Gives every program, reading the Input box (stdin) only when first used:
//
//   input          all lines of the input, as an array of strings
//   next()         the next line ("" after the last one)
//   nextInt()      the next line as a number          nextInts()  the next line as an array of numbers
//   nextWords()    the next line split on spaces
//
// A program that declares its own `input`, `next`, ... (or reads stdin itself) is not affected.
"use strict";
let lines = null, at = 0;
function all() {
    if (lines === null) {
        let text = "";
        try { text = require("fs").readFileSync(0, "utf8"); } catch (e) { text = ""; }
        lines = text.split(/\r?\n/);
        if (lines.length && lines[lines.length - 1] === "") lines.pop();
    }
    return lines;
}
const helpers = {
    next: () => { const l = all(); return at < l.length ? l[at++] : ""; },
    nextInt: () => Number(helpers.next().trim()),
    nextInts: () => helpers.next().trim().split(/\s+/).filter(Boolean).map(Number),
    nextWords: () => helpers.next().trim().split(/\s+/).filter(Boolean),
};
Object.defineProperty(globalThis, "input", { get: all, configurable: true, enumerable: false });
for (const [name, fn] of Object.entries(helpers)) {
    Object.defineProperty(globalThis, name, { value: fn, writable: true, configurable: true, enumerable: false });
}
