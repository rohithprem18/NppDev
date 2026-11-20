// NppDev's LeetCode runner for JavaScript: runs a file that only has the LeetCode function
// (`var twoSum = function(nums, target) {...}`) like LeetCode does. Each input line is one
// argument in LeetCode's format; the result is printed the same way. Argument types come from
// the JSDoc above the function (@param {ListNode} head, @return {TreeNode}).
"use strict";
const fs = require("fs");
const vm = require("vm");
const userPath = process.argv[2];

function ListNode(val, next) { this.val = val === undefined ? 0 : val; this.next = next === undefined ? null : next; }
function TreeNode(val, left, right) { this.val = val === undefined ? 0 : val; this.left = left === undefined ? null : left; this.right = right === undefined ? null : right; }
globalThis.ListNode = ListNode;
globalThis.TreeNode = TreeNode;

const src = fs.readFileSync(userPath, "utf8");
vm.runInThisContext(src, { filename: userPath });

const fnRe = /(?:var|let|const)\s+([A-Za-z_$][\w$]*)\s*=\s*(?:async\s*)?(?:function\b|\([^)]*\)\s*=>|[A-Za-z_$][\w$]*\s*=>)|function\s+([A-Za-z_$][\w$]*)\s*\(/g;
let match, name = null, at = 0;
while ((match = fnRe.exec(src))) {
  const n = match[1] || match[2];
  if (n === "ListNode" || n === "TreeNode") continue;
  name = n; at = match.index; break;
}
if (!name) { console.error("NppDev: no LeetCode function found (like `var twoSum = function(nums, target)`)."); process.exit(1); }
const fn = vm.runInThisContext(name);

// types from the JSDoc block right before the function
const before = src.slice(0, at);
const doc = before.slice(before.lastIndexOf("/**"));
const types = [...doc.matchAll(/@param\s*\{([^}]*)\}/g)].map((m) => m[1]);

const makeList = (a) => { let head = null, cur = null; for (const v of a || []) { const n = new ListNode(v); if (!head) head = cur = n; else { cur.next = n; cur = n; } } return head; };
const makeTree = (a) => {
  if (!a || !a.length || a[0] === null) return null;
  const root = new TreeNode(a[0]); const q = [root]; let i = 1;
  for (let k = 0; k < q.length && i < a.length; k++) {
    const node = q[k];
    if (i < a.length && a[i] !== null) { node.left = new TreeNode(a[i]); q.push(node.left); } i++;
    if (i < a.length && a[i] !== null) { node.right = new TreeNode(a[i]); q.push(node.right); } i++;
  }
  return root;
};
const convert = (v, t) => {
  if (!t) return v;
  if (t.includes("ListNode")) return t.includes("[]") ? v.map(makeList) : makeList(v);
  if (t.includes("TreeNode")) return makeTree(v);
  return v;
};
const plain = (v) => {
  if (v instanceof ListNode || (v && typeof v === "object" && "val" in v && "next" in v)) { const out = []; for (let n = v; n; n = n.next) out.push(plain(n.val)); return out; }
  if (v instanceof TreeNode || (v && typeof v === "object" && "val" in v && "left" in v)) {
    const out = [], q = [v];
    for (let k = 0; k < q.length; k++) { const n = q[k]; if (!n) { out.push(null); continue; } out.push(plain(n.val)); q.push(n.left, n.right); }
    while (out.length && out[out.length - 1] === null) out.pop();
    return out;
  }
  if (Array.isArray(v)) return v.map(plain);
  if (v instanceof Set) return [...v].map(plain);
  return v;
};
const show = (v) => { v = plain(v); if (typeof v === "number" && !Number.isInteger(v)) return v.toFixed(5); return JSON.stringify(v === undefined ? null : v); };

const lines = fs.readFileSync(0, "utf8").split(/\r?\n/).filter((l) => l.trim());
const n = fn.length || types.length;
if (n && lines.length < n) { console.error(`NppDev: ${name}() takes ${n} argument(s), one per input line; the input has ${lines.length}.`); process.exit(1); }
for (let k = 0; k + n <= lines.length; k += n || 1) {
  const args = [];
  for (let j = 0; j < n; j++) args.push(convert(JSON.parse(lines[k + j]), types[j]));
  const result = fn(...args);
  console.log(show(result === undefined && args.length ? args[0] : result));
  if (!n) break;
}
