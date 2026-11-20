// Per-extension editor language and icon (src/assets/file-icons, from Material Icon Theme).

interface FileType {
  lang: string;
  icon: string;
}

const C = (lang: string, icon: string): FileType => ({ lang, icon });

const TYPES: Record<string, FileType> = {
  c: C("c", "c"),
  h: C("cpp", "h"),
  cpp: C("cpp", "cpp"),
  cc: C("cpp", "cpp"),
  cxx: C("cpp", "cpp"),
  hpp: C("cpp", "hpp"),
  java: C("java", "java"),
  py: C("python", "python"),
  pyw: C("python", "python"),
  js: C("javascript", "javascript"),
  mjs: C("javascript", "javascript"),
  cjs: C("javascript", "javascript"),
  ts: C("typescript", "typescript"),
  go: C("go", "go"),
  rs: C("rust", "rust"),
  kt: C("kotlin", "kotlin"),
  kts: C("kotlin", "kotlin"),
  cs: C("csharp", "csharp"),
  md: C("markdown", "markdown"),
  json: C("json", "json"),
  txt: C("plaintext", "document"),
  in: C("plaintext", "document"),
  out: C("plaintext", "document"),
  ini: C("ini", "settings"),
  toml: C("ini", "settings"),
  cfg: C("ini", "settings"),
  sh: C("shell", "console"),
  bash: C("shell", "console"),
  ps1: C("powershell", "powershell"),
  bat: C("bat", "console"),
  cmd: C("bat", "console"),
  xml: C("xml", "xml"),
  yml: C("yaml", "yaml"),
  yaml: C("yaml", "yaml"),
  html: C("html", "html"),
  css: C("css", "css"),
  png: C("plaintext", "image"),
  jpg: C("plaintext", "image"),
  jpeg: C("plaintext", "image"),
  gif: C("plaintext", "image"),
  svg: C("xml", "image"),
  pdf: C("plaintext", "pdf"),
  sql: C("sql", "database"),
  db: C("plaintext", "database"),
};

const PLAIN = C("plaintext", "document");

export function fileType(name: string): FileType {
  if (/^\.git(ignore|attributes|modules)$/i.test(name)) return C("plaintext", "git");
  const dot = name.lastIndexOf(".");
  return dot < 0 ? PLAIN : TYPES[name.slice(dot + 1).toLowerCase()] ?? PLAIN;
}

/** Can NppDev run this file? */
export const RUNNABLE = new Set(["c", "cpp", "java", "python", "javascript", "go", "rust", "kotlin", "csharp", "sql"]);

export const LANGUAGE_NAMES: Record<string, string> = {
  c: "C", cpp: "C++", java: "Java", python: "Python", javascript: "JavaScript", typescript: "TypeScript",
  go: "Go", rust: "Rust", kotlin: "Kotlin", csharp: "C#", markdown: "Markdown", json: "JSON", plaintext: "Plain Text",
  ini: "INI", sql: "SQL", shell: "Shell", powershell: "PowerShell", xml: "XML", yaml: "YAML", html: "HTML", css: "CSS", bat: "Batch",
};
