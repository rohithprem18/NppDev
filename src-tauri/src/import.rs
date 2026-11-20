//! Problem import: the Competitive Companion browser extension (it POSTs the problem to us), or a
//! problem URL from Codeforces, AtCoder, CodeChef, LeetCode, NeetCode, GeeksforGeeks, HackerRank
//! or HackerEarth, read from the site's page or public API.

use regex_lite::Regex;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::io::{BufRead, BufReader, Read, Write};
use std::net::TcpListener;
use std::time::Duration;
use tauri::{AppHandle, Emitter};

#[derive(Serialize, Deserialize, Clone, Default, Debug)]
#[serde(rename_all = "camelCase", default)]
pub struct Sample {
    pub input: String,
    pub output: String,
}

/// Same shape as Competitive Companion's JSON, plus a few extras for function-style sites.
#[derive(Serialize, Deserialize, Clone, Default, Debug)]
#[serde(rename_all = "camelCase", default)]
pub struct Problem {
    pub name: String,
    pub group: String,
    pub url: String,
    pub tests: Vec<Sample>,
    /// milliseconds
    pub time_limit: u64,
    /// megabytes
    pub memory_limit: u64,
    pub interactive: bool,
    /// LeetCode / GfG style: you write a function, not a whole program.
    pub function_based: bool,
    /// Starter code by NppDev language id (cpp, java, python, javascript, go, rust, kotlin, csharp, c).
    pub starter: BTreeMap<String, String>,
}

// ---------------------------------------------------------------- Competitive Companion

/// Ports Competitive Companion sends to by default; the first free one is used.
const PORTS: &[u16] = &[27121, 10043, 10045, 10042, 6174, 4244, 1327];

/// Start listening for Competitive Companion in the background. Returns the port, if any was free.
pub fn listen(app: AppHandle) -> Option<u16> {
    let (listener, port) = PORTS.iter().find_map(|&p| TcpListener::bind(("127.0.0.1", p)).ok().map(|l| (l, p)))?;
    std::thread::spawn(move || {
        for stream in listener.incoming().flatten() {
            let _ = stream.set_read_timeout(Some(Duration::from_secs(5)));
            let mut reader = BufReader::new(&stream);
            let mut len = 0usize;
            let mut line = String::new();
            let mut first = true;
            let mut is_post = false;
            loop {
                line.clear();
                if reader.read_line(&mut line).unwrap_or(0) == 0 { break; }
                if first { is_post = line.starts_with("POST"); first = false; }
                let l = line.trim_end();
                if l.is_empty() { break; }
                if let Some((k, v)) = l.split_once(':') {
                    if k.eq_ignore_ascii_case("content-length") { len = v.trim().parse().unwrap_or(0); }
                }
            }
            let mut body = vec![0u8; len.min(16 << 20)];
            let ok = is_post && reader.read_exact(&mut body).is_ok();
            let mut s = &stream;
            let _ = s.write_all(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            if !ok { continue; }
            if let Ok(mut p) = serde_json::from_slice::<Problem>(&body) {
                if p.time_limit == 0 { p.time_limit = 2000; }
                if p.memory_limit == 0 { p.memory_limit = 256; }
                let _ = app.emit("problem-received", p);
            }
        }
    });
    Some(port)
}

#[tauri::command]
pub fn companion_port(state: tauri::State<'_, CompanionPort>) -> Option<u16> {
    state.0
}

pub struct CompanionPort(pub Option<u16>);

// ---------------------------------------------------------------- helpers

const UA: &str = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36";

fn agent() -> ureq::Agent {
    ureq::Agent::config_builder()
        .timeout_global(Some(Duration::from_secs(20)))
        .user_agent(UA)
        .build()
        .into()
}

fn get(url: &str) -> Result<String, String> {
    match agent().get(url).header("Accept", "text/html,application/json,*/*").header("Accept-Language", "en").call() {
        Ok(mut r) => r.body_mut().with_config().limit(32 << 20).read_to_string().map_err(|e| e.to_string()),
        // some sites (Codeforces behind Cloudflare) turn away this client but not curl, which
        // ships with Windows 10+ and Ubuntu
        Err(ureq::Error::StatusCode(403 | 503)) => curl(url).ok_or_else(|| net_error(url, ureq::Error::StatusCode(403))),
        Err(e) => Err(net_error(url, e)),
    }
}

fn curl(url: &str) -> Option<String> {
    let o = crate::runner::quiet_command("curl").args(["-sfL", "--compressed", "--max-time", "20", "-A", UA, "-H", "Accept-Language: en", url]).output().ok()?;
    if !o.status.success() { return None; }
    Some(String::from_utf8_lossy(&o.stdout).into_owned())
}

fn post_json(url: &str, body: &serde_json::Value) -> Result<String, String> {
    let mut r = agent().post(url).header("Content-Type", "application/json").header("Referer", url).send(body.to_string()).map_err(|e| net_error(url, e))?;
    r.body_mut().with_config().limit(32 << 20).read_to_string().map_err(|e| e.to_string())
}

fn net_error(url: &str, e: ureq::Error) -> String {
    let host = url.split('/').nth(2).unwrap_or(url);
    match e {
        ureq::Error::StatusCode(404) => "Problem not found. Check the link.".into(),
        ureq::Error::StatusCode(403) | ureq::Error::StatusCode(503) => format!("{host} refused the request (it may be checking for bots). Use the Competitive Companion extension instead."),
        ureq::Error::StatusCode(c) => format!("{host} answered with error {c}."),
        _ => format!("Could not reach {host}. Check your internet connection."),
    }
}

fn re(p: &str) -> Regex { Regex::new(p).unwrap() }

fn decode_entities(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut rest = s;
    while let Some(i) = rest.find('&') {
        out.push_str(&rest[..i]);
        rest = &rest[i..];
        let end = rest[..rest.len().min(12)].find(';');
        let Some(end) = end else { out.push('&'); rest = &rest[1..]; continue };
        let ent = &rest[1..end];
        let ch = match ent {
            "lt" => Some('<'), "gt" => Some('>'), "amp" => Some('&'), "quot" => Some('"'), "apos" => Some('\''), "nbsp" => Some(' '),
            "le" => Some('≤'), "ge" => Some('≥'), "ne" => Some('≠'), "times" => Some('×'),
            _ if ent.starts_with("#x") || ent.starts_with("#X") => u32::from_str_radix(&ent[2..], 16).ok().and_then(char::from_u32),
            _ if ent.starts_with('#') => ent[1..].parse().ok().and_then(char::from_u32),
            _ => None,
        };
        match ch { Some(c) => { out.push(c); rest = &rest[end + 1..]; } None => { out.push('&'); rest = &rest[1..]; } }
    }
    out.push_str(rest);
    out
}

/// HTML of a <pre> (or similar) block to plain text: <br> and line <div>s become new lines.
fn html_text(s: &str) -> String {
    let s = re(r"(?i)<br\s*/?>").replace_all(s, "\n");
    let s = re(r"(?i)</(div|p|li)>").replace_all(&s, "\n");
    let s = re(r"<[^>]+>").replace_all(&s, "");
    decode_entities(&s)
}

/// Tidy a sample: \r\n -> \n, no trailing spaces, exactly one final newline.
fn tidy(s: &str) -> String {
    let lines: Vec<&str> = s.split('\n').map(|l| l.trim_end_matches(['\r', ' ', '\t'])).collect();
    let mut start = 0;
    while start < lines.len() && lines[start].is_empty() { start += 1; }
    let mut end = lines.len();
    while end > start && lines[end - 1].is_empty() { end -= 1; }
    if start == end { return String::new(); }
    lines[start..end].join("\n") + "\n"
}

fn num(s: &str) -> Option<f64> {
    re(r"[0-9]+(\.[0-9]+)?").find(s).and_then(|m| m.as_str().parse().ok())
}

// ---------------------------------------------------------------- sites

fn codeforces(url: &str) -> Result<Problem, String> {
    let page = get(url)?;
    if page.contains("Just a moment") && !page.contains("problem-statement") { return Err("Codeforces is checking for bots right now. Use the Competitive Companion extension instead.".into()); }
    let mut p = Problem { url: url.into(), group: "Codeforces".into(), ..Default::default() };
    if let Some(c) = re(r#"<div class="header"><div class="title">([^<]*)</div>"#).captures(&page) { p.name = decode_entities(&c[1]); }
    if let Some(c) = re(r#"time limit per test</div>([^<]*)<"#).captures(&page) { p.time_limit = (num(&c[1]).unwrap_or(2.0) * 1000.0) as u64; }
    if let Some(c) = re(r#"memory limit per test</div>([^<]*)<"#).captures(&page) { p.memory_limit = num(&c[1]).unwrap_or(256.0) as u64; }
    if let Some(c) = re(r#"<a [^>]*href="/(?:contest|gym)/\d+"[^>]*>([^<]+)</a>"#).captures(&page) {
        let contest = decode_entities(c[1].trim());
        p.group = if contest.starts_with("Codeforces") { contest } else { format!("Codeforces - {contest}") };
    }
    let ins: Vec<String> = re(r#"(?s)<div class="input">.*?<pre[^>]*>(.*?)</pre>"#).captures_iter(&page).map(|c| tidy(&html_text(&c[1]))).collect();
    let outs: Vec<String> = re(r#"(?s)<div class="output">.*?<pre[^>]*>(.*?)</pre>"#).captures_iter(&page).map(|c| tidy(&html_text(&c[1]))).collect();
    p.tests = ins.into_iter().zip(outs).map(|(input, output)| Sample { input, output }).collect();
    p.interactive = page.contains("This is an interactive problem");
    Ok(p)
}

fn atcoder(url: &str) -> Result<Problem, String> {
    let page = get(url)?;
    let mut p = Problem { url: url.into(), group: "AtCoder".into(), time_limit: 2000, memory_limit: 1024, ..Default::default() };
    if let Some(c) = re(r#"<title>([^<]*)</title>"#).captures(&page) { p.name = decode_entities(c[1].trim()); }
    if let Some(c) = re(r#"<a class="contest-title"[^>]*>([^<]*)</a>"#).captures(&page) { p.group = format!("AtCoder - {}", decode_entities(c[1].trim())); }
    if let Some(c) = re(r#"Time Limit: ([0-9.]+) sec"#).captures(&page) { p.time_limit = (c[1].parse::<f64>().unwrap_or(2.0) * 1000.0) as u64; }
    if let Some(c) = re(r#"Memory Limit: ([0-9]+) MiB"#).captures(&page) { p.memory_limit = c[1].parse().unwrap_or(1024); }
    // the English statement when there is one (the Japanese one comes first)
    let body = page.find(r#"<span class="lang-en">"#).map(|i| &page[i..]).unwrap_or(&page);
    let ins: Vec<String> = re(r#"(?s)<h3>Sample Input \d+</h3>\s*<pre[^>]*>(.*?)</pre>"#).captures_iter(body).map(|c| tidy(&html_text(&c[1]))).collect();
    let outs: Vec<String> = re(r#"(?s)<h3>Sample Output \d+</h3>\s*<pre[^>]*>(.*?)</pre>"#).captures_iter(body).map(|c| tidy(&html_text(&c[1]))).collect();
    p.tests = ins.into_iter().zip(outs).map(|(input, output)| Sample { input, output }).collect();
    Ok(p)
}

fn codechef(url: &str) -> Result<Problem, String> {
    let parts: Vec<&str> = url.split(['?', '#']).next().unwrap().trim_end_matches('/').split('/').collect();
    let i = parts.iter().position(|&s| s == "problems").ok_or("Not a CodeChef problem link.")?;
    let code = parts.get(i + 1).ok_or("Not a CodeChef problem link.")?;
    let contest = if i >= 1 && parts[i - 1] != "www.codechef.com" && parts[i - 1] != "codechef.com" { parts[i - 1] } else { "PRACTICE" };
    let j: serde_json::Value = serde_json::from_str(&get(&format!("https://www.codechef.com/api/contests/{contest}/problems/{code}"))?).map_err(|_| "CodeChef sent an unexpected answer.")?;
    if j["status"] == "error" { return Err("Problem not found. Check the link.".into()); }
    let mut p = Problem { url: url.into(), group: format!("CodeChef - {contest}"), memory_limit: 256, ..Default::default() };
    p.name = j["problem_name"].as_str().unwrap_or(code).to_string();
    let tl = j["max_timelimit"].as_f64().or_else(|| j["max_timelimit"].as_str().and_then(|s| s.parse().ok())).unwrap_or(1.0);
    p.time_limit = (tl * 1000.0) as u64;
    if let Some(arr) = j["problemComponents"]["sampleTestCases"].as_array() {
        p.tests = arr.iter().filter(|t| t["isDeleted"] != true).map(|t| Sample { input: tidy(t["input"].as_str().unwrap_or("")), output: tidy(t["output"].as_str().unwrap_or("")) }).collect();
    }
    if p.tests.is_empty() {
        // older problems: samples only in the statement body
        let body = j["body"].as_str().unwrap_or("");
        let blocks: Vec<String> = re(r"(?s)```\s*\n(.*?)```").captures_iter(body).map(|c| tidy(&c[1])).collect();
        p.tests = blocks.chunks(2).filter(|c| c.len() == 2).map(|c| Sample { input: c[0].clone(), output: c[1].clone() }).collect();
    }
    Ok(p)
}

const LC_LANGS: &[(&str, &str)] = &[("cpp", "cpp"), ("java", "java"), ("python3", "python"), ("javascript", "javascript"), ("golang", "go"), ("rust", "rust"), ("kotlin", "kotlin"), ("csharp", "csharp"), ("c", "c")];

fn leetcode_slug(slug: &str, url: &str, group: &str) -> Result<Problem, String> {
    let q = serde_json::json!({
        "query": "query q($s: String!) { question(titleSlug: $s) { title questionFrontendId exampleTestcases content metaData codeSnippets { langSlug code } } }",
        "variables": { "s": slug },
    });
    let j: serde_json::Value = serde_json::from_str(&post_json("https://leetcode.com/graphql", &q)?).map_err(|_| "LeetCode sent an unexpected answer.")?;
    let qn = &j["data"]["question"];
    if qn.is_null() { return Err("Problem not found. Check the link.".into()); }
    let mut p = Problem { url: url.into(), group: group.into(), time_limit: 2000, memory_limit: 256, function_based: true, ..Default::default() };
    p.name = format!("{}. {}", qn["questionFrontendId"].as_str().unwrap_or(""), qn["title"].as_str().unwrap_or(slug));
    let meta: serde_json::Value = serde_json::from_str(qn["metaData"].as_str().unwrap_or("{}")).unwrap_or_default();
    let nparams = meta["params"].as_array().map(|a| a.len()).unwrap_or(1).max(1);
    let lines: Vec<&str> = qn["exampleTestcases"].as_str().unwrap_or("").lines().collect();
    let content = qn["content"].as_str().unwrap_or("");
    let outs: Vec<String> = re(r"(?s)<strong>Output:?</strong>:?\s*(.*?)(?:\n|<strong>|</pre>|</p>)")
        .captures_iter(content).map(|c| tidy(&html_text(&c[1]))).collect();
    for (i, chunk) in lines.chunks(nparams).enumerate() {
        p.tests.push(Sample { input: tidy(&chunk.join("\n")), output: outs.get(i).cloned().unwrap_or_default() });
    }
    if let Some(snips) = qn["codeSnippets"].as_array() {
        for s in snips {
            if let Some(&(_, id)) = LC_LANGS.iter().find(|(slug, _)| s["langSlug"] == *slug) {
                p.starter.insert(id.into(), s["code"].as_str().unwrap_or("").to_string());
            }
        }
    }
    Ok(p)
}

fn leetcode(url: &str) -> Result<Problem, String> {
    let slug = re(r"/problems/([a-z0-9-]+)").captures(url).map(|c| c[1].to_string()).ok_or("Not a LeetCode problem link.")?;
    leetcode_slug(&slug, url, "LeetCode")
}

/// NeetCode problems are LeetCode problems under another name; the site's bundle maps one to the other.
fn neetcode(url: &str) -> Result<Problem, String> {
    let slug = re(r"/problems/([a-z0-9-]+)").captures(url).map(|c| c[1].to_string()).ok_or("Not a NeetCode problem link.")?;
    let home = get("https://neetcode.io/")?;
    let bundle = re(r#"src="(main[.-][a-z0-9]+\.js)""#).captures(&home).map(|c| c[1].to_string()).ok_or("NeetCode changed its site; use the LeetCode link of this problem instead.")?;
    let js = get(&format!("https://neetcode.io/{bundle}"))?;
    let pat = format!(r#"link:"([a-z0-9-]+)/"[^{{}}]*?ncLink:"{}/""#, regex_lite::escape(&slug));
    let lc = re(&pat).captures(&js).map(|c| c[1].to_string()).unwrap_or(slug);
    leetcode_slug(&lc, url, "NeetCode")
}

fn gfg(url: &str) -> Result<Problem, String> {
    let slug = re(r"/problems/([^/?#]+)").captures(url).map(|c| c[1].to_string()).ok_or("Not a GeeksforGeeks problem link.")?;
    let j: serde_json::Value = serde_json::from_str(&get(&format!("https://practiceapi.geeksforgeeks.org/api/vr/problems/{slug}/"))?).map_err(|_| "GeeksforGeeks sent an unexpected answer.")?;
    let r = &j["results"];
    if r.is_null() { return Err("Problem not found. Check the link.".into()); }
    let mut p = Problem { url: url.into(), group: "GeeksforGeeks".into(), time_limit: 2000, memory_limit: 256, function_based: true, ..Default::default() };
    p.name = r["problem_name"].as_str().unwrap_or(&slug).to_string();
    let q = r["problem_question"].as_str().unwrap_or("");
    let outs: Vec<String> = re(r"(?s)<strong>\s*Output\s*:?\s*</strong>:?(.*?)(?:<strong>|</pre>|\n)").captures_iter(q).map(|c| tidy(&html_text(&c[1]))).collect();
    let ins: Vec<String> = re(r"(?s)<strong>\s*Input\s*:?\s*</strong>:?(.*?)(?:<strong>|</pre>|\n)").captures_iter(q).map(|c| tidy(&html_text(&c[1]))).collect();
    // the judge's own input for the first example (fields separated by &!//!&)
    let driver = r["test_cases"].as_str().map(|s| tidy(&s.replace("&!//!&", "\n")));
    for (i, out) in outs.iter().enumerate() {
        let input = if i == 0 { driver.clone().unwrap_or_else(|| ins.first().cloned().unwrap_or_default()) } else { ins.get(i).cloned().unwrap_or_default() };
        p.tests.push(Sample { input, output: out.clone() });
    }
    Ok(p)
}

/// HackerRank often annotates samples as a table: "STDIN   Function" / "-----   --------" /
/// "6       ar[] size n = 6". Keep only the STDIN column.
fn strip_stdin_notes(s: &str) -> String {
    let mut lines = s.lines();
    let (Some(head), Some(rule)) = (lines.next(), lines.next()) else { return s.to_string() };
    let col = match head.find("Function") { Some(c) if head.trim_start().starts_with("STDIN") && rule.trim_start().starts_with('-') => c, _ => return s.to_string() };
    let rows: Vec<String> = lines.map(|l| l.char_indices().nth(col).map(|(i, _)| &l[..i]).unwrap_or(l).trim_end().to_string()).collect();
    tidy(&rows.join("\n"))
}

fn hackerrank(url: &str) -> Result<Problem, String> {
    let c = re(r"hackerrank\.com/(?:contests/([^/]+)/)?challenges/([^/?#]+)").captures(url).ok_or("Not a HackerRank challenge link.")?;
    let contest = c.get(1).map(|m| m.as_str()).unwrap_or("master");
    let slug = &c[2];
    let j: serde_json::Value = serde_json::from_str(&get(&format!("https://www.hackerrank.com/rest/contests/{contest}/challenges/{slug}"))?).map_err(|_| "HackerRank sent an unexpected answer.")?;
    let m = &j["model"];
    if m.is_null() { return Err("Challenge not found. Check the link.".into()); }
    let mut p = Problem { url: url.into(), group: "HackerRank".into(), time_limit: 2000, memory_limit: 256, ..Default::default() };
    p.name = m["name"].as_str().unwrap_or(slug).to_string();
    let body = m["body_html"].as_str().unwrap_or("");
    let ins: Vec<String> = re(r"(?s)challenge_sample_input_body.*?<pre[^>]*>(.*?)</pre>").captures_iter(body).map(|c| strip_stdin_notes(&tidy(&html_text(&c[1])))).collect();
    let outs: Vec<String> = re(r"(?s)challenge_sample_output_body.*?<pre[^>]*>(.*?)</pre>").captures_iter(body).map(|c| tidy(&html_text(&c[1]))).collect();
    p.tests = ins.into_iter().zip(outs).map(|(input, output)| Sample { input, output }).collect();
    Ok(p)
}

fn hackerearth(url: &str) -> Result<Problem, String> {
    let page = get(url)?;
    // the data is in the Next.js payload: JS string literals holding JSON
    let mut data = String::new();
    for c in re(r#"self\.__next_f\.push\(\[1,("(?:[^"\\]|\\.)*")\]\)"#).captures_iter(&page) {
        if let Ok(s) = serde_json::from_str::<String>(&c[1]) { data.push_str(&s); }
    }
    let field = |name: &str| -> Option<String> {
        let c = re(&format!(r#""{name}":("(?:[^"\\]|\\.)*")"#)).captures(&data)?;
        serde_json::from_str::<String>(&c[1]).ok()
    };
    let numf = |name: &str| re(&format!(r#""{name}":([0-9.]+)"#)).captures(&data).and_then(|c| c[1].parse::<f64>().ok());
    let input = field("sample_input").ok_or("Couldn't find the samples on this HackerEarth page. Use the Competitive Companion extension instead.")?;
    let mut p = Problem { url: url.into(), group: "HackerEarth".into(), ..Default::default() };
    p.name = field("title").unwrap_or_else(|| "HackerEarth problem".into());
    p.time_limit = (numf("time_limit").unwrap_or(1.0) * 1000.0) as u64;
    p.memory_limit = numf("memory_limit").unwrap_or(256.0) as u64;
    p.tests = vec![Sample { input: tidy(&input), output: tidy(&field("sample_output").unwrap_or_default()) }];
    Ok(p)
}

/// Read a problem from its URL.
pub fn fetch(url: &str) -> Result<Problem, String> {
    let url = url.trim();
    let url = if url.starts_with("http") { url.to_string() } else { format!("https://{url}") };
    let host = url.split('/').nth(2).unwrap_or("").to_lowercase();
    let mut p = if host.ends_with("codeforces.com") { codeforces(&url) }
        else if host.ends_with("atcoder.jp") { atcoder(&url) }
        else if host.ends_with("codechef.com") { codechef(&url) }
        else if host.ends_with("leetcode.com") || host.ends_with("leetcode.cn") { leetcode(&url) }
        else if host.ends_with("neetcode.io") { neetcode(&url) }
        else if host.ends_with("geeksforgeeks.org") { gfg(&url) }
        else if host.ends_with("hackerrank.com") { hackerrank(&url) }
        else if host.ends_with("hackerearth.com") { hackerearth(&url) }
        else { return Err("NppDev can import from Codeforces, AtCoder, CodeChef, LeetCode, NeetCode, GeeksforGeeks, HackerRank and HackerEarth. For other sites, use the Competitive Companion extension.".into()) }?;
    if p.name.trim().is_empty() { p.name = "Problem".into(); }
    if p.time_limit == 0 { p.time_limit = 2000; }
    if p.memory_limit == 0 { p.memory_limit = 256; }
    if p.tests.is_empty() && !p.function_based { return Err("No sample tests were found on that page.".into()); }
    Ok(p)
}

#[tauri::command]
pub async fn import_problem(url: String) -> Result<Problem, String> {
    tauri::async_runtime::spawn_blocking(move || fetch(&url)).await.map_err(|e| e.to_string())?
}

#[cfg(test)]
mod live {
    // Hits the real sites: cargo test --lib import -- --ignored --nocapture
    #[test]
    #[ignore]
    fn sites() {
        let urls = [
            "https://codeforces.com/contest/4/problem/A",
            "https://codeforces.com/problemset/problem/1/A",
            "https://atcoder.jp/contests/abc300/tasks/abc300_a",
            "https://www.codechef.com/problems/FLOW001",
            "https://www.codechef.com/START100D/problems/SURPLUS",
            "https://leetcode.com/problems/two-sum/",
            "https://neetcode.io/problems/two-integer-sum",
            "https://www.geeksforgeeks.org/problems/subarray-with-given-sum-1587115621/1",
            "https://www.hackerrank.com/challenges/simple-array-sum/problem",
            "https://www.hackerearth.com/practice/basic-programming/input-output/basics-of-input-output/practice-problems/algorithm/roy-and-profile-picture/",
        ];
        let mut failed = vec![];
        for u in urls {
            match super::fetch(u) {
                Ok(p) => {
                    println!("OK  {} | {} | {} ms {} MB | {} tests | starter {:?}", p.name, p.group, p.time_limit, p.memory_limit, p.tests.len(), p.starter.keys().collect::<Vec<_>>());
                    if let Some(t) = p.tests.first() { println!("    in  {:?}\n    out {:?}", t.input.chars().take(80).collect::<String>(), t.output.chars().take(80).collect::<String>()); }
                    if p.tests.is_empty() { failed.push(u); }
                }
                Err(e) => { println!("ERR {u}: {e}"); failed.push(u); }
            }
        }
        assert!(failed.is_empty(), "{failed:?}");
    }
}
