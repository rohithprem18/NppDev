// Snippets: NppDev's competitive-programming set, plus your own in snippets.json
// (VS Code's format: { "Name": { "scope": "cpp,python", "prefix": "...", "body": [...], "description": "..." } }).
import { invoke } from "@tauri-apps/api/core";
import { monaco } from "./editor";
import { api } from "./api";

interface Snip { prefix: string; body: string; description: string; scope: string[] }

const B = (scope: string, prefix: string, description: string, body: string): Snip => ({ scope: scope.split(","), prefix, description, body });

const BUILT_IN: Snip[] = [
  // C++
  B("cpp", "cp", "Competitive programming template", "#include <bits/stdc++.h>\nusing namespace std;\nusing ll = long long;\n\nvoid solve() {\n\t$0\n}\n\nint main() {\n\tios::sync_with_stdio(false);\n\tcin.tie(nullptr);\n\tint t = 1;\n\tcin >> t;\n\twhile (t--) solve();\n\treturn 0;\n}"),
  B("cpp", "fastio", "Fast input/output", "ios::sync_with_stdio(false);\ncin.tie(nullptr);"),
  B("cpp", "fori", "for i in [0, n)", "for (int ${1:i} = 0; $1 < ${2:n}; $1++) {\n\t$0\n}"),
  B("cpp", "forr", "for i from n-1 down to 0", "for (int ${1:i} = ${2:n} - 1; $1 >= 0; $1--) {\n\t$0\n}"),
  B("cpp", "tc", "Read t test cases", "int t;\ncin >> t;\nwhile (t--) {\n\t$0\n}"),
  B("cpp", "vin", "Read a vector", "vector<${1:int}> ${2:a}(${3:n});\nfor (auto &x : $2) cin >> x;"),
  B("cpp", "all", "v.begin(), v.end()", "${1:v}.begin(), $1.end()"),
  B("cpp", "binpow", "Fast power modulo m", "long long binpow(long long a, long long b, long long m) {\n\tlong long r = 1;\n\ta %= m;\n\twhile (b > 0) {\n\t\tif (b & 1) r = r * a % m;\n\t\ta = a * a % m;\n\t\tb >>= 1;\n\t}\n\treturn r;\n}"),
  B("cpp", "sieve", "Sieve of Eratosthenes", "vector<bool> is_prime(${1:N} + 1, true);\nis_prime[0] = is_prime[1] = false;\nfor (int i = 2; 1LL * i * i <= $1; i++)\n\tif (is_prime[i])\n\t\tfor (int j = i * i; j <= $1; j += i) is_prime[j] = false;"),
  B("cpp", "dsu", "Disjoint set union", "struct DSU {\n\tvector<int> p, sz;\n\tDSU(int n) : p(n), sz(n, 1) { iota(p.begin(), p.end(), 0); }\n\tint find(int x) { return p[x] == x ? x : p[x] = find(p[x]); }\n\tbool unite(int a, int b) {\n\t\ta = find(a), b = find(b);\n\t\tif (a == b) return false;\n\t\tif (sz[a] < sz[b]) swap(a, b);\n\t\tp[b] = a;\n\t\tsz[a] += sz[b];\n\t\treturn true;\n\t}\n};"),
  B("cpp", "segtree", "Segment tree (sum, point update)", "struct SegTree {\n\tint n;\n\tvector<long long> t;\n\tSegTree(int n) : n(n), t(2 * n) {}\n\tvoid update(int i, long long v) {\n\t\tfor (t[i += n] = v; i > 1; i >>= 1) t[i >> 1] = t[i] + t[i ^ 1];\n\t}\n\tlong long query(int l, int r) { // [l, r)\n\t\tlong long res = 0;\n\t\tfor (l += n, r += n; l < r; l >>= 1, r >>= 1) {\n\t\t\tif (l & 1) res += t[l++];\n\t\t\tif (r & 1) res += t[--r];\n\t\t}\n\t\treturn res;\n\t}\n};"),
  B("cpp", "dijkstra", "Dijkstra shortest paths", "vector<long long> dist(${1:n}, LLONG_MAX);\npriority_queue<pair<long long, int>, vector<pair<long long, int>>, greater<>> pq;\ndist[${2:src}] = 0;\npq.push({0, $2});\nwhile (!pq.empty()) {\n\tauto [d, u] = pq.top();\n\tpq.pop();\n\tif (d > dist[u]) continue;\n\tfor (auto [v, w] : ${3:adj}[u])\n\t\tif (dist[u] + w < dist[v]) {\n\t\t\tdist[v] = dist[u] + w;\n\t\t\tpq.push({dist[v], v});\n\t\t}\n}"),
  B("cpp", "bfs", "Breadth-first search", "vector<int> dist(${1:n}, -1);\nqueue<int> q;\ndist[${2:src}] = 0;\nq.push($2);\nwhile (!q.empty()) {\n\tint u = q.front();\n\tq.pop();\n\tfor (int v : ${3:adj}[u])\n\t\tif (dist[v] == -1) {\n\t\t\tdist[v] = dist[u] + 1;\n\t\t\tq.push(v);\n\t\t}\n}"),
  B("cpp", "debug", "Debug print (only on your PC)", "#ifndef ONLINE_JUDGE\n#define dbg(x) cerr << #x << \" = \" << (x) << endl\n#else\n#define dbg(x)\n#endif"),
  // C
  B("c", "fori", "for i in [0, n)", "for (int ${1:i} = 0; $1 < ${2:n}; $1++) {\n\t$0\n}"),
  // Python
  B("python", "cp", "Competitive programming template", "import sys\ninput = sys.stdin.readline\n\n\ndef solve():\n    $0\n\n\nfor _ in range(int(input())):\n    solve()"),
  B("python", "ints", "Read a line of integers", "${1:a} = list(map(int, input().split()))"),
  B("python", "nm", "Read two integers", "${1:n}, ${2:m} = map(int, input().split())"),
  B("python", "fastio", "Read all input at once", "import sys\ndata = sys.stdin.buffer.read().split()\nptr = 0"),
  B("python", "dsu", "Disjoint set union", "parent = list(range(${1:n}))\n\n\ndef find(x):\n    while parent[x] != x:\n        parent[x] = parent[parent[x]]\n        x = parent[x]\n    return x\n\n\ndef union(a, b):\n    a, b = find(a), find(b)\n    if a != b:\n        parent[b] = a"),
  B("python", "rec", "Raise the recursion limit", "import sys\nsys.setrecursionlimit(1 << 25)"),
  // Java
  B("java", "fastreader", "Fast input reader", "static class FastReader {\n\tBufferedReader br = new BufferedReader(new InputStreamReader(System.in));\n\tStringTokenizer st;\n\tString next() throws IOException {\n\t\twhile (st == null || !st.hasMoreTokens()) st = new StringTokenizer(br.readLine());\n\t\treturn st.nextToken();\n\t}\n\tint nextInt() throws IOException { return Integer.parseInt(next()); }\n\tlong nextLong() throws IOException { return Long.parseLong(next()); }\n}"),
  B("java", "sout", "System.out.println", "System.out.println($0);"),
  B("java", "fori", "for i in [0, n)", "for (int ${1:i} = 0; $1 < ${2:n}; $1++) {\n\t$0\n}"),
  // JavaScript (NppDev's input helpers)
  B("javascript", "ints", "Read a line of numbers", "const ${1:a} = nextInts();"),
  B("javascript", "tc", "Read t test cases", "let t = nextInt();\nwhile (t--) {\n\t$0\n}"),
  // Go / Rust / Kotlin / C#
  B("go", "fori", "for i in [0, n)", "for ${1:i} := 0; $1 < ${2:n}; $1++ {\n\t$0\n}"),
  B("rust", "read", "Read the next number", "let ${1:n}: ${2:usize} = it.next().unwrap().parse().unwrap();"),
  B("kotlin", "readints", "Read a line of integers", "val ${1:a} = readLine()!!.split(\" \").map { it.toInt() }"),
  B("csharp", "readints", "Read a line of integers", "var ${1:a} = Console.ReadLine().Split(' ', StringSplitOptions.RemoveEmptyEntries).Select(int.Parse).ToArray();"),
];

const EXAMPLE = `{
  // Your own snippets, for every language or only some ("scope"). Type the prefix in the editor.
  // "body" is the text; $1, $2 are places Tab jumps to, $0 is where the cursor ends.
  "Print a pair": {
    "scope": "cpp",
    "prefix": "pp",
    "body": ["cout << \${1:a} << ' ' << \${2:b} << '\\\\n';"],
    "description": "Print two values"
  }
}
`;

let user: Snip[] = [];
let userPath: string | null = null;
const LANGS = ["c", "cpp", "java", "python", "javascript", "typescript", "go", "rust", "kotlin", "csharp"];

/** JSON with // comments and trailing commas, like VS Code's settings files. */
function parseJsonc(text: string): unknown {
  const noComments = text.replace(/("(?:[^"\\]|\\.)*")|\/\/[^\n]*|\/\*[\s\S]*?\*\//g, (_m, str) => str ?? "");
  return JSON.parse(noComments.replace(/,(\s*[}\]])/g, "$1"));
}

export async function loadUserSnippets(): Promise<string | null> {
  try {
    userPath = await invoke<string>("user_snippets_path");
    if (!(await api.pathExists(userPath))) await api.writeText(userPath, EXAMPLE);
    const data = parseJsonc(await api.readText(userPath)) as Record<string, { scope?: string; prefix?: string | string[]; body?: string | string[]; description?: string }>;
    user = [];
    for (const [name, s] of Object.entries(data ?? {})) {
      if (!s?.prefix || !s.body) continue;
      const body = Array.isArray(s.body) ? s.body.join("\n") : s.body;
      for (const prefix of Array.isArray(s.prefix) ? s.prefix : [s.prefix]) {
        user.push({ prefix, body, description: s.description || name, scope: s.scope ? s.scope.split(",").map((x) => x.trim()) : ["*"] });
      }
    }
    return null;
  } catch (e) {
    user = [];
    return `Your snippets file has an error: ${e instanceof Error ? e.message : e}`;
  }
}

export const userSnippetsPath = () => userPath;

/** Open snippets.json in a tab (the app reloads it when it's saved). */
export async function openUserSnippets() {
  await loadUserSnippets();
  const { useApp } = await import("../store");
  if (userPath) await useApp.getState().openFile(userPath);
}

let registered = false;
/** Offer snippets in the suggestions of every language. */
export function registerSnippets() {
  if (registered) return;
  registered = true;
  for (const lang of LANGS) {
    monaco.languages.registerCompletionItemProvider(lang, {
      provideCompletionItems(model, position) {
        const word = model.getWordUntilPosition(position);
        const range = new monaco.Range(position.lineNumber, word.startColumn, position.lineNumber, word.endColumn);
        const list = [...BUILT_IN, ...user].filter((s) => s.scope.includes("*") || s.scope.includes(lang) || (lang === "c" && s.scope.includes("cpp") && false));
        return {
          suggestions: list.map((s) => ({
            label: { label: s.prefix, description: s.description },
            kind: monaco.languages.CompletionItemKind.Snippet,
            insertText: s.body,
            insertTextRules: monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet,
            documentation: { value: "```\n" + s.body.replace(/\$\{\d+:([^}]*)\}/g, "$1").replace(/\$\d/g, "") + "\n```" },
            detail: s.description,
            range,
            sortText: "~" + s.prefix, // after the language server's own items with the same name
          })),
        };
      },
    });
  }
}
