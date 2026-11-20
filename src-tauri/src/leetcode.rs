//! LeetCode-style files: only `class Solution` (or a JS function), no code that reads input.
//! NppDev runs them like LeetCode does, with a hidden runner from src/lc/: each line of a test's
//! input is one argument, the method's result is printed in LeetCode's format.

use regex_lite::Regex;

pub const PY_DRIVER: &str = include_str!("lc/driver.py");
pub const JS_DRIVER: &str = include_str!("lc/driver.js");
pub const CPP_PRE: &str = include_str!("lc/pre.h");
pub const CPP_MAIN: &str = include_str!("lc/main.h");
pub const JAVA_MAIN: &str = include_str!("lc/NppDevMain.java");

/// The source without comments and string contents, so that commented-out starter code
/// (`// class ListNode`, `/** ... */`) and text in strings don't count.
pub fn code_only(src: &str, hash_comments: bool) -> String {
    let mut out = String::with_capacity(src.len());
    let b: Vec<char> = src.chars().collect();
    let mut i = 0;
    while i < b.len() {
        let c = b[i];
        let next = b.get(i + 1).copied().unwrap_or('\0');
        if !hash_comments && c == '/' && next == '/' || hash_comments && c == '#' {
            while i < b.len() && b[i] != '\n' { i += 1; }
        } else if !hash_comments && c == '/' && next == '*' {
            i += 2;
            while i < b.len() && !(b[i] == '*' && b.get(i + 1) == Some(&'/')) { if b[i] == '\n' { out.push('\n'); } i += 1; }
            i += 2;
        } else if c == '"' || c == '\'' && !hash_comments {
            out.push(c);
            i += 1;
            while i < b.len() && b[i] != c && b[i] != '\n' { if b[i] == '\\' { i += 1; } i += 1; }
            out.push(c);
            i += 1;
        } else {
            out.push(c);
            i += 1;
        }
    }
    out
}

/// Should this file run through the LeetCode runner? (`lang`: cpp, java, python, javascript)
pub fn detect(lang: &str, src: &str) -> bool {
    let has = |code: &str, p: &str| Regex::new(p).unwrap().is_match(code);
    match lang {
        "cpp" => { let c = code_only(src, false); has(&c, r"\bclass\s+Solution\b") && !has(&c, r"\bmain\s*\(") }
        "java" => { let c = code_only(src, false); has(&c, r"\bclass\s+Solution\b") && !has(&c, r"\bstatic\s+void\s+main\s*\(") }
        "python" => {
            let c = code_only(src, true);
            has(&c, r"(?m)^class\s+Solution\b") && !has(&c, r"\binput\s*\(|sys\.stdin|__main__|open\s*\(\s*0")
        }
        "javascript" => {
            // LeetCode's JS starter: a JSDoc block with @param/@return right before the function
            has(src, r"@(param|return)\b") && !has(&code_only(src, false), r"\b(next|nextInt|nextInts|nextWords|readline)\s*\(|\binput\b|process\.stdin|readFileSync")
        }
        _ => false,
    }
}

/// Name of the method LeetCode calls: the first method of class Solution (C++ and Java).
pub fn method_name(src: &str) -> Option<String> {
    let code = code_only(src, false);
    let at = Regex::new(r"\bclass\s+Solution\b").unwrap().find(&code)?.end();
    let body = &code[at..];
    let re = Regex::new(r"([A-Za-z_][\w:<>,\[\]\s\*&]*?)\b([A-Za-z_]\w*)\s*\(([^()]|\([^()]*\))*\)\s*(const\s*)?(throws[\w\s,.]*)?\{").unwrap();
    for c in re.captures_iter(body) {
        let name = &c[2];
        let before = c[1].trim();
        if matches!(name, "Solution" | "if" | "for" | "while" | "switch" | "catch" | "return" | "sizeof") { continue; }
        if before.is_empty() || before.ends_with("new") || before.ends_with("return") { continue; }
        return Some(name.to_string());
    }
    None
}

/// Does the user's code already define ListNode / TreeNode (then the runner doesn't)?
pub fn defines(src: &str, ty: &str) -> bool {
    Regex::new(&format!(r"\b(struct|class)\s+{ty}\b")).unwrap().is_match(&code_only(src, false))
}

#[cfg(test)]
mod t {
    use super::*;
    #[test]
    fn finds() {
        let cpp = "/**\n * struct ListNode { int val; };\n */\nclass Solution {\npublic:\n    vector<vector<int>> threeSum(vector<int>& nums) {\n        return {};\n    }\n};";
        assert!(detect("cpp", cpp));
        assert_eq!(method_name(cpp).as_deref(), Some("threeSum"));
        assert!(!defines(cpp, "ListNode"));
        let java = "class Solution {\n    public List<List<Integer>> threeSum(int[] nums) {\n        if (nums.length == 0) return null;\n        return null;\n    }\n}";
        assert!(detect("java", java));
        assert_eq!(method_name(java).as_deref(), Some("threeSum"));
        assert!(!detect("cpp", "#include <iostream>\nint main(){}"));
        assert!(detect("python", "class Solution:\n    def threeSum(self, nums):\n        return []\n"));
        assert!(!detect("python", "n = int(input())\nprint(n)\n"));
        assert!(detect("javascript", "/**\n * @param {number[]} nums\n * @return {number[][]}\n */\nvar threeSum = function(nums) {};"));
    }
}
