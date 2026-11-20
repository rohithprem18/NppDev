// Per-extension language, icon glyph and colour (the Explorer and tabs use them).

interface FileType {
  lang: string;
  glyph: string;
  color: string;
}

const C = (lang: string, glyph: string, color: string): FileType => ({ lang, glyph, color });

const TYPES: Record<string, FileType> = {
  c: C("c", "C", "#519aba"),
  h: C("cpp", "h", "#a074c4"),
  cpp: C("cpp", "C+", "#a074c4"),
  cc: C("cpp", "C+", "#a074c4"),
  cxx: C("cpp", "C+", "#a074c4"),
  hpp: C("cpp", "h", "#a074c4"),
  java: C("java", "J", "#cc3e44"),
  py: C("python", "Py", "#4b8bbe"),
  js: C("javascript", "JS", "#cbcb41"),
  mjs: C("javascript", "JS", "#cbcb41"),
  cjs: C("javascript", "JS", "#cbcb41"),
  ts: C("typescript", "TS", "#519aba"),
  go: C("go", "Go", "#519aba"),
  rs: C("rust", "R", "#e37933"),
  kt: C("kotlin", "K", "#a074c4"),
  kts: C("kotlin", "K", "#a074c4"),
  cs: C("csharp", "C#", "#59a05e"),
  md: C("markdown", "M", "#519aba"),
  json: C("json", "{}", "#cbcb41"),
  txt: C("plaintext", "≡", "#8b949e"),
  in: C("plaintext", "≡", "#8b949e"),
  out: C("plaintext", "≡", "#8b949e"),
  ini: C("ini", "⚙", "#6d8086"),
  toml: C("ini", "⚙", "#6d8086"),
  sh: C("shell", ">_", "#89a03d"),
  ps1: C("powershell", ">_", "#519aba"),
};

const PLAIN = C("plaintext", "", "#8b949e");

export function fileType(name: string): FileType {
  const dot = name.lastIndexOf(".");
  return dot < 0 ? PLAIN : TYPES[name.slice(dot + 1).toLowerCase()] ?? PLAIN;
}

/** Can NppDev run this file (Run panel, M2)? */
export const RUNNABLE = new Set(["c", "cpp", "java", "python", "javascript", "go", "rust", "kotlin", "csharp"]);

export const LANGUAGE_NAMES: Record<string, string> = {
  c: "C", cpp: "C++", java: "Java", python: "Python", javascript: "JavaScript", typescript: "TypeScript",
  go: "Go", rust: "Rust", kotlin: "Kotlin", csharp: "C#", markdown: "Markdown", json: "JSON", plaintext: "Plain Text",
  ini: "INI", shell: "Shell", powershell: "PowerShell",
};
