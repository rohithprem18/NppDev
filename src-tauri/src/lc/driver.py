# NppDev's LeetCode runner for Python: runs a file that only has `class Solution` (no input
# reading) like LeetCode does. Each line of the test input is one argument, in LeetCode's
# format ([1,2,3], "abc", 5, true); the result is printed the same way.
import sys, json, inspect

sys.setrecursionlimit(1 << 20)
user_path = sys.argv[1]

# what LeetCode's Python environment already has, so starter code like List[int] works
import typing, collections, heapq, bisect, math, itertools, functools, re, string, random, operator
from typing import *
from collections import *
from heapq import *
from bisect import *
from math import *
from functools import *
from itertools import *


class ListNode:
    def __init__(self, val=0, next=None):
        self.val = val
        self.next = next


class TreeNode:
    def __init__(self, val=0, left=None, right=None):
        self.val = val
        self.left = left
        self.right = right


g = {k: v for k, v in globals().items() if not k.startswith("__")}
g.update({"__name__": "__nppdev__", "__file__": user_path})
with open(user_path, encoding="utf-8") as f:
    code = compile(f.read(), user_path, "exec")
exec(code, g)

LN = g.get("ListNode", ListNode)
TN = g.get("TreeNode", TreeNode)


def make_list(a):
    head = cur = None
    for v in a or []:
        node = LN(v)
        if head is None:
            head = cur = node
        else:
            cur.next = node
            cur = node
    return head


def make_tree(a):
    if not a or a[0] is None:
        return None
    root = TN(a[0])
    q, i = [root], 1
    for node in q:
        if i >= len(a):
            break
        if a[i] is not None:
            node.left = TN(a[i])
            q.append(node.left)
        i += 1
        if i < len(a) and a[i] is not None:
            node.right = TN(a[i])
            q.append(node.right)
        i += 1
    return root


def convert(v, ann):
    t = str(ann)
    if "ListNode" in t:
        if v and isinstance(v[0], list):
            return [make_list(x) for x in v]
        return make_list(v)
    if "TreeNode" in t:
        return make_tree(v)
    return v


def plain(v):
    if isinstance(v, LN) or (hasattr(v, "val") and hasattr(v, "next")):
        out = []
        while v is not None:
            out.append(plain(v.val))
            v = v.next
        return out
    if isinstance(v, TN) or (hasattr(v, "val") and hasattr(v, "left")):
        out, q = [], [v]
        for node in q:
            if node is None:
                out.append(None)
                continue
            out.append(plain(node.val))
            q.append(node.left)
            q.append(node.right)
        while out and out[-1] is None:
            out.pop()
        return out
    if isinstance(v, (list, tuple, set, frozenset, collections.deque)):
        return [plain(x) for x in v]
    if isinstance(v, dict):
        return {str(k): plain(x) for k, x in v.items()}
    return v


def show(v):
    v = plain(v)
    if isinstance(v, float):
        return "%.5f" % v
    return json.dumps(v, separators=(",", ":"), ensure_ascii=False)


Sol = g.get("Solution")
if Sol is None:
    sys.exit("NppDev: no `class Solution` found. Add a main that reads the input, or write the LeetCode class.")
methods = [m for m, fn in inspect.getmembers(Sol, inspect.isfunction) if not m.startswith("_")]
if not methods:
    sys.exit("NppDev: class Solution has no method to call.")
methods.sort(key=lambda m: getattr(Sol, m).__code__.co_firstlineno)
name = methods[0]
sig = inspect.signature(getattr(Sol, name))
params = list(sig.parameters.values())[1:]
lines = [l for l in sys.stdin.read().splitlines() if l.strip()]
n = len(params)
if n and len(lines) < n:
    sys.exit(f"NppDev: {name}() takes {n} argument(s), one per input line; the input has {len(lines)}.")
step = n or 1
for at in range(0, max(len(lines) - n, 0) + 1, step):
    args = [convert(json.loads(lines[at + j]), params[j].annotation) for j in range(n)]
    result = getattr(Sol(), name)(*args)
    print(show(args[0] if result is None and args else result))
    if n == 0:
        break
