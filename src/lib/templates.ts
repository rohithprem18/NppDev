// Starting code for new solution files (problem import). `NAME` is the class name where one is needed.

export interface Lang {
  id: string;
  name: string;
  ext: string;
  /** Needs a class named like the file (Java, Kotlin is fine either way, C#). */
  classFile?: boolean;
}

export const LANGS: Lang[] = [
  { id: "cpp", name: "C++", ext: "cpp" },
  { id: "java", name: "Java", ext: "java", classFile: true },
  { id: "python", name: "Python", ext: "py" },
  { id: "javascript", name: "JavaScript", ext: "js" },
  { id: "c", name: "C", ext: "c" },
  { id: "go", name: "Go", ext: "go" },
  { id: "rust", name: "Rust", ext: "rs" },
  { id: "kotlin", name: "Kotlin", ext: "kt" },
  { id: "csharp", name: "C#", ext: "cs", classFile: true },
];

const T: Record<string, string> = {
  cpp: `#include <bits/stdc++.h>
using namespace std;

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    $0
    return 0;
}
`,
  java: `import java.util.*;
import java.io.*;

public class NAME {
    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringTokenizer st = new StringTokenizer(br.readLine());
        PrintWriter out = new PrintWriter(new BufferedWriter(new OutputStreamWriter(System.out)));

        $0
        out.flush();
    }
}
`,
  python: `import sys
input = sys.stdin.readline


def main():
    $0


main()
`,
  javascript: `// next(), nextInt(), nextInts(), nextWords() and input (all lines) read the Input box.

$0
`,
  c: `#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    $0
    return 0;
}
`,
  go: `package main

import (
\t"bufio"
\t"fmt"
\t"os"
)

func main() {
\tin := bufio.NewReader(os.Stdin)
\tout := bufio.NewWriter(os.Stdout)
\tdefer out.Flush()

\t$0
\t_ = in
\tfmt.Fprintln(out)
}
`,
  rust: `use std::io::{self, Read, Write};

fn main() {
    let mut s = String::new();
    io::stdin().read_to_string(&mut s).unwrap();
    let mut it = s.split_ascii_whitespace();
    let mut out = io::BufWriter::new(io::stdout().lock());

    $0
    let _ = (&mut it, &mut out);
    out.flush().unwrap();
}
`,
  kotlin: `import java.io.*
import java.util.*

fun main() {
    val br = BufferedReader(InputStreamReader(System.\`in\`))

    $0
}
`,
  csharp: `using System;
using System.Collections.Generic;
using System.Linq;

class NAME
{
    static void Main()
    {
        $0
    }
}
`,
};

/** "A. Watermelon" -> "A_Watermelon"; class-file languages get a name that is a valid identifier. */
export function fileStem(problemName: string, lang: Lang): string {
  let s = problemName.normalize("NFKD").replace(/[^\w\s-]/g, " ").trim().replace(/[\s-]+/g, "_").replace(/_+/g, "_").slice(0, 60) || "Solution";
  if (lang.classFile) {
    s = s.replace(/-/g, "_");
    if (/^\d/.test(s)) s = "P" + s;
  }
  return s;
}

function header(comment: string, lines: string[]): string {
  return lines.map((l) => (l ? `${comment} ${l}` : comment)).join("\n") + "\n";
}

/** The new file's text: a header with the problem name and link, then a template or the site's starter code. */
export function solutionText(lang: Lang, stem: string, problem: { name: string; url?: string; group?: string; functionBased?: boolean; starter?: Record<string, string> }): string {
  const comment = lang.id === "python" ? "#" : "//";
  const info = [problem.name, problem.url ?? ""].filter(Boolean);
  const starter = problem.starter?.[lang.id];
  if (starter) {
    const note = problem.functionBased
      ? ["", "Write the function below. The Run panel's tests hold the example arguments, one per line;", "to run them here, add a main that reads them and prints the result."]
      : [];
    return header(comment, [...info, ...note]) + "\n" + starter.replace(/\r\n/g, "\n") + "\n";
  }
  const body = (T[lang.id] ?? "$0\n").replace(/NAME/g, stem).replace("$0", "");
  return header(comment, info) + "\n" + body;
}
