//! Compile and run a file against its test cases, with time and memory limits, like an online judge.
//!
//! Builds go to <temp>/nppdev-run/<hash of the source path>; unchanged code is not rebuilt.
//! C++ that includes <bits/stdc++.h> uses a precompiled header (prepared once by `warm_up`).

use serde::{Deserialize, Serialize};
use std::collections::hash_map::DefaultHasher;
use std::ffi::OsString;
use std::hash::{Hash, Hasher};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter};

use crate::tools;

static CANCEL: AtomicBool = AtomicBool::new(false);
const MAX_OUTPUT: usize = 4 << 20;
const JS_PRELUDE: &str = include_str!("js-prelude.js");

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TestCase {
    pub input: String,
    pub expected: Option<String>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RunRequest {
    pub path: String,
    pub tests: Vec<TestCase>,
    pub time_limit_ms: u64,
    pub memory_limit_mb: u64,
    /// Run only these test indexes (all when empty).
    #[serde(default)]
    pub only: Vec<usize>,
}

#[derive(Serialize, Clone, Default)]
#[serde(rename_all = "camelCase")]
pub struct TestResult {
    pub index: usize,
    /// AC (matches expected) · WA · RE · TLE · MLE · OK (ran; no expected output to compare)
    pub verdict: String,
    pub output: String,
    pub stderr: String,
    pub time_ms: u64,
    pub memory_kb: u64,
    pub message: Option<String>,
}

#[derive(Serialize, Clone, Default)]
#[serde(rename_all = "camelCase")]
pub struct CompileResult {
    pub ok: bool,
    pub output: String,
    pub cached: bool,
    pub ms: u64,
    pub command: String,
}

#[derive(Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct RunResult {
    pub compile: CompileResult,
    pub tests: Vec<TestResult>,
    /// A tool this file needs isn't installed: its key (gpp, javac, python, node, go, rustc, kotlinc, csc).
    pub missing: Option<String>,
    pub error: Option<String>,
}

/// A command that doesn't flash a console window on Windows.
pub fn quiet_command(program: impl AsRef<std::ffi::OsStr>) -> Command {
    #[allow(unused_mut)]
    let mut c = Command::new(program);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        c.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    c
}

fn hash<T: Hash>(t: &T) -> String {
    let mut h = DefaultHasher::new();
    t.hash(&mut h);
    format!("{:016x}", h.finish())
}

fn run_root() -> PathBuf {
    std::env::temp_dir().join("nppdev-run")
}

fn mtime(p: &Path) -> u64 {
    std::fs::metadata(p).and_then(|m| m.modified()).map(|t| t.duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_secs()).unwrap_or(0)
}

/// PATH with a tool's folder first (compiled programs need its DLLs on Windows).
fn path_with(dir: &Path) -> OsString {
    let mut v = vec![dir.to_path_buf()];
    if let Some(p) = std::env::var_os("PATH") { v.extend(std::env::split_paths(&p)); }
    std::env::join_paths(v).unwrap_or_default()
}

// ---------------------------------------------------------------- languages

struct Prepared {
    run: Vec<OsString>,
    env: Vec<(String, OsString)>,
}

enum Lang { C, Cpp, Java, Python, Js, Go, Rust, Kotlin, CSharp }

fn lang_of(path: &Path) -> Option<Lang> {
    let ext = path.extension()?.to_string_lossy().to_lowercase();
    Some(match ext.as_str() {
        "c" => Lang::C,
        "cpp" | "cc" | "cxx" | "c++" => Lang::Cpp,
        "java" => Lang::Java,
        "py" | "pyw" => Lang::Python,
        "js" | "mjs" | "cjs" => Lang::Js,
        "go" => Lang::Go,
        "rs" => Lang::Rust,
        "kt" | "kts" => Lang::Kotlin,
        "cs" => Lang::CSharp,
        _ => return None,
    })
}

/// C++ that needs C++20 (library or language). Everything else builds as C++17, which loads
/// <bits/stdc++.h> about twice as fast.
fn needs_cpp20(src: &str) -> bool {
    const MARKS: &[&str] = &["<ranges>", "<format>", "<span>", "<numbers>", "<concepts>", "<compare>", "<bit>", "<coroutine>",
        "ranges::", "views::", "std::format", "std::span", "<=>", "concept ", "requires ", "consteval", "constinit", "co_await",
        "co_yield", "co_return", "char8_t", "[[likely]]", "[[unlikely]]", "popcount(", "bit_width(", "bit_ceil(", "bit_floor(",
        "countl_zero(", "countr_zero(", "has_single_bit(", "midpoint(", "lerp(", ".contains(", ".starts_with(", ".ends_with(",
        "std::erase(", "std::erase_if(", "std::ssize(", "std::to_array("];
    MARKS.iter().any(|m| src.contains(m))
}

fn cpp_flags(cpp20: bool) -> Vec<String> {
    vec!["-O2".into(), "-DONLINE_JUDGE".into(), format!("-std={}", if cpp20 { "gnu++20" } else { "gnu++17" }), "-fdiagnostics-color=never".into()]
}

fn pch_dir(gpp: &Path, flags: &[String]) -> PathBuf {
    run_root().join("pch").join(hash(&(gpp.to_string_lossy(), mtime(gpp), flags.join(" "))))
}

/// Prepare the precompiled <bits/stdc++.h> (C++17 and C++20) in the background, once per compiler.
#[tauri::command]
pub async fn warm_up() {
    tauri::async_runtime::spawn_blocking(|| {
        let Some(gpp) = tools::find("gpp") else { return };
        for cpp20 in [false, true] {
            let flags = cpp_flags(cpp20);
            let dir = pch_dir(&gpp, &flags);
            let gch = dir.join("bits").join("stdc++.h.gch");
            if gch.exists() { continue; }
            let _ = std::fs::create_dir_all(gch.parent().unwrap());
            let header = dir.join("pch-source.h");
            let _ = std::fs::write(&header, "#include <bits/stdc++.h>\n");
            let tmp = gch.with_extension(format!("{}.tmp", std::process::id()));
            let ok = quiet_command(&gpp).args(&flags).arg("-x").arg("c++-header").arg(&header).arg("-o").arg(&tmp)
                .env("PATH", path_with(gpp.parent().unwrap())).stdout(Stdio::null()).stderr(Stdio::null())
                .status().map(|s| s.success()).unwrap_or(false);
            if ok { let _ = std::fs::rename(&tmp, &gch); } else { let _ = std::fs::remove_file(&tmp); }
        }
    }).await.ok();
}

/// Compile when needed. Err(Some(key)) = tool missing; Err(None) = compile error (in `out`).
fn prepare(path: &Path, out: &mut CompileResult) -> Result<Prepared, Option<String>> {
    let lang = lang_of(path).ok_or(None)?;
    let dir = run_root().join(hash(&path.to_string_lossy().to_lowercase()));
    std::fs::create_dir_all(&dir).map_err(|_| None)?;
    let src = std::fs::read_to_string(path).unwrap_or_default();
    let stem = path.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_else(|| "main".into());
    let need = |key: &'static str| tools::find(key).ok_or(Some(key.to_string()));
    let exe = |name: &str| dir.join(format!("{name}{}", if cfg!(windows) { ".exe" } else { "" }));

    // compile with `cmd` into `artifact` unless the same source + compiler + flags was built before
    let build = |out: &mut CompileResult, tool: &Path, args: Vec<OsString>, artifact: &Path, key_extra: &str, envpath: Option<&Path>| -> Result<(), Option<String>> {
        let key = hash(&(&src, tool.to_string_lossy(), mtime(tool), &args, key_extra));
        let key_file = dir.join(".build-key");
        if artifact.exists() && std::fs::read_to_string(&key_file).ok().as_deref() == Some(key.as_str()) {
            out.ok = true;
            out.cached = true;
            return Ok(());
        }
        let _ = std::fs::remove_file(&key_file);
        out.command = format!("{} {}", tool.file_name().unwrap_or_default().to_string_lossy(), args.iter().map(|a| a.to_string_lossy().into_owned()).collect::<Vec<_>>().join(" "));
        let start = Instant::now();
        let mut c = quiet_command(tool);
        c.args(&args).current_dir(path.parent().unwrap_or(Path::new(".")));
        if let Some(d) = envpath { c.env("PATH", path_with(d)); }
        let o = c.output().map_err(|_| Some(String::new()))?;
        out.ms = start.elapsed().as_millis() as u64;
        out.output = format!("{}{}", String::from_utf8_lossy(&o.stdout), String::from_utf8_lossy(&o.stderr)).trim_end().to_string();
        out.ok = o.status.success() && artifact.exists();
        if !out.ok { return Err(None); }
        let _ = std::fs::write(&key_file, key);
        Ok(())
    };

    match lang {
        Lang::C | Lang::Cpp => {
            let cpp = matches!(lang, Lang::Cpp);
            let tool = if cpp { need("gpp")? } else { need("gcc").or_else(|_| need("gpp"))? };
            let bin = tool.parent().unwrap().to_path_buf();
            let target = exe(&stem);
            let mut flags = if cpp { cpp_flags(needs_cpp20(&src)) } else { vec!["-O2".into(), "-DONLINE_JUDGE".into(), "-fdiagnostics-color=never".into()] };
            if cpp && src.contains("bits/stdc++.h") {
                let pch = pch_dir(&tool, &flags);
                if pch.join("bits").join("stdc++.h.gch").exists() { flags.extend(["-I".into(), pch.to_string_lossy().into_owned()]); }
            }
            let mut args: Vec<OsString> = flags.iter().map(OsString::from).collect();
            args.push(path.as_os_str().into());
            args.extend(["-o".into(), target.clone().into_os_string()]);
            if !cpp { args.push("-lm".into()); }
            build(out, &tool, args, &target, "", Some(&bin))?;
            Ok(Prepared { run: vec![target.into_os_string()], env: vec![("PATH".into(), path_with(&bin))] })
        }
        Lang::Java => {
            let javac = need("javac")?;
            let java = tools::find("java").unwrap_or_else(|| javac.with_file_name(format!("java{}", if cfg!(windows) { ".exe" } else { "" })));
            let classes = dir.join("classes");
            let marker = classes.join(".built");
            let pkg = src.lines().map(str::trim).find_map(|l| l.strip_prefix("package ").map(|p| p.trim_end_matches(';').trim().to_string()));
            let main = match &pkg { Some(p) => format!("{p}.{stem}"), None => stem.clone() };
            let key = hash(&(&src, javac.to_string_lossy(), mtime(&javac)));
            if marker.exists() && std::fs::read_to_string(dir.join(".build-key")).ok().as_deref() == Some(key.as_str()) {
                out.ok = true;
                out.cached = true;
            } else {
                let _ = std::fs::remove_dir_all(&classes);
                let _ = std::fs::create_dir_all(&classes);
                let start = Instant::now();
                out.command = format!("javac -d classes {}", path.file_name().unwrap().to_string_lossy());
                let o = quiet_command(&javac).args(["-J-XX:TieredStopAtLevel=1", "-J-XX:+UseSerialGC", "-encoding", "UTF-8", "-d"]).arg(&classes).arg(path)
                    .current_dir(path.parent().unwrap()).output().map_err(|_| Some("javac".to_string()))?;
                out.ms = start.elapsed().as_millis() as u64;
                out.output = format!("{}{}", String::from_utf8_lossy(&o.stdout), String::from_utf8_lossy(&o.stderr)).trim_end().to_string();
                out.ok = o.status.success();
                if !out.ok { return Err(None); }
                let _ = std::fs::write(&marker, "");
                let _ = std::fs::write(dir.join(".build-key"), key);
            }
            Ok(Prepared {
                run: vec![java.into_os_string(), "-Xss256m".into(), "-Xms32m".into(), "-XX:+UseSerialGC".into(), "-XX:TieredStopAtLevel=1".into(), "-Dstdout.encoding=UTF-8".into(), "-cp".into(), classes.into_os_string(), main.into()],
                env: vec![],
            })
        }
        Lang::Python => {
            let py = need("python")?;
            out.ok = true;
            out.cached = true;
            Ok(Prepared { run: vec![py.into_os_string(), "-X".into(), "utf8".into(), path.as_os_str().into()], env: vec![("PYTHONIOENCODING".into(), "utf-8".into())] })
        }
        Lang::Js => {
            let node = need("node")?;
            let prelude = dir.join("nppdev-prelude.js");
            if std::fs::read_to_string(&prelude).ok().as_deref() != Some(JS_PRELUDE) { let _ = std::fs::write(&prelude, JS_PRELUDE); }
            out.ok = true;
            out.cached = true;
            Ok(Prepared { run: vec![node.into_os_string(), "--stack-size=65500".into(), "--require".into(), prelude.into_os_string(), path.as_os_str().into()], env: vec![] })
        }
        Lang::Go => {
            let go = need("go")?;
            let target = exe(&stem);
            build(out, &go, vec!["build".into(), "-o".into(), target.clone().into_os_string(), path.as_os_str().into()], &target, "", None)?;
            Ok(Prepared { run: vec![target.into_os_string()], env: vec![] })
        }
        Lang::Rust => {
            let rustc = need("rustc")?;
            let target = exe(&stem);
            build(out, &rustc, vec!["-O".into(), "--edition".into(), "2021".into(), "-o".into(), target.clone().into_os_string(), path.as_os_str().into()], &target, "", None)?;
            Ok(Prepared { run: vec![target.into_os_string()], env: vec![] })
        }
        Lang::Kotlin => {
            let kotlinc = need("kotlinc")?;
            let java = tools::find("java").ok_or(Some("javac".to_string()))?;
            let jar = dir.join(format!("{stem}.jar"));
            build(out, &kotlinc, vec![path.as_os_str().into(), "-include-runtime".into(), "-d".into(), jar.clone().into_os_string()], &jar, "", None)?;
            Ok(Prepared { run: vec![java.into_os_string(), "-Xss256m".into(), "-Xms32m".into(), "-jar".into(), jar.into_os_string()], env: vec![] })
        }
        Lang::CSharp => {
            let csc = need("csc")?;
            let target = dir.join(format!("{stem}.exe"));
            let out_flag = if cfg!(windows) { format!("/out:{}", target.display()) } else { format!("-out:{}", target.display()) };
            let mut args: Vec<OsString> = if cfg!(windows) { vec!["/nologo".into(), "/optimize+".into()] } else { vec!["-optimize+".into()] };
            args.push(out_flag.into());
            args.push(path.as_os_str().into());
            build(out, &csc, args, &target, "", None)?;
            if cfg!(windows) { Ok(Prepared { run: vec![target.into_os_string()], env: vec![] }) }
            else { Ok(Prepared { run: vec!["mono".into(), target.into_os_string()], env: vec![] }) }
        }
    }
}

// ---------------------------------------------------------------- running one test

/// Judge's comparison: line by line, ignoring trailing spaces and blank lines at the end.
fn same_answer(got: &str, want: &str) -> bool {
    normalize(got) == normalize(want)
}

fn normalize(text: &str) -> Vec<String> {
    let mut v: Vec<String> = text.replace("\r\n", "\n").split('\n').map(|l| l.trim_end().to_string()).collect();
    while v.last().is_some_and(|l| l.is_empty()) { v.pop(); }
    v
}

#[cfg(windows)]
fn crash_reason(code: i32) -> Option<&'static str> {
    Some(match code as u32 {
        0xC0000005 => "access violation: a bad pointer or an array index out of range",
        0xC00000FD => "stack overflow: infinite or too deep recursion",
        0xC0000094 => "integer division by zero",
        0xC0000374 => "heap corruption: a bad free or a buffer overflow",
        0xC0000409 => "stack buffer overrun",
        0xC000001D => "illegal instruction",
        0xC0000017 => "out of memory",
        _ => return None,
    })
}

fn run_one(prep: &Prepared, cwd: &Path, index: usize, test: &TestCase, tl_ms: u64, ml_mb: u64) -> TestResult {
    let mut r = TestResult { index, ..Default::default() };
    let mut c = quiet_command(&prep.run[0]);
    c.args(&prep.run[1..]).current_dir(cwd).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped());
    for (k, v) in &prep.env { c.env(k, v); }
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        // address-space cap as a safety net. Not for the Java VM: it reserves (not uses) far more
        // address space than the limit and wouldn't start; Java is judged on memory really used.
        let jvm = Path::new(&prep.run[0]).file_stem().is_some_and(|s| s.to_string_lossy().starts_with("java"));
        let limit = if jvm { libc::RLIM_INFINITY } else { ((ml_mb.max(16) * 2 + 256) << 20) as libc::rlim_t };
        unsafe {
            c.pre_exec(move || {
                let rl = libc::rlimit { rlim_cur: limit, rlim_max: limit };
                libc::setrlimit(libc::RLIMIT_AS, &rl);
                let st = libc::rlimit { rlim_cur: libc::RLIM_INFINITY, rlim_max: libc::RLIM_INFINITY };
                libc::setrlimit(libc::RLIMIT_STACK, &st);
                Ok(())
            });
        }
    }
    let mut child = match c.spawn() {
        Ok(ch) => ch,
        Err(e) => { r.verdict = "RE".into(); r.message = Some(format!("Could not start the program: {e}")); return r; }
    };
    let start = Instant::now();
    #[cfg(windows)]
    let job = win::Job::new(ml_mb).and_then(|j| j.assign(&child).then_some(j));

    // feed stdin and collect stdout / stderr on their own threads, so big outputs don't block
    let input = test.input.clone();
    let mut stdin = child.stdin.take();
    let writer = std::thread::spawn(move || { if let Some(s) = stdin.as_mut() { let _ = s.write_all(input.as_bytes()); } });
    let reader = |mut s: Box<dyn Read + Send>| std::thread::spawn(move || {
        let mut buf = Vec::new();
        let mut chunk = [0u8; 65536];
        loop {
            match s.read(&mut chunk) { Ok(0) | Err(_) => break, Ok(n) => if buf.len() < MAX_OUTPUT { buf.extend_from_slice(&chunk[..n.min(MAX_OUTPUT - buf.len())]); } }
        }
        buf
    });
    let out_t = reader(Box::new(child.stdout.take().unwrap()));
    let err_t = reader(Box::new(child.stderr.take().unwrap()));

    let limit = Duration::from_millis(tl_ms);
    let mut timed_out = false;
    #[allow(unused_mut, unused_assignments)]
    let mut peak_kb = 0u64;
    let status: Option<i32>;
    #[cfg(unix)]
    let mut signal: Option<i32> = None;
    loop {
        #[cfg(windows)]
        let done = match child.try_wait() { Ok(Some(s)) => Some(s.code().unwrap_or(-1)), _ => None };
        #[cfg(unix)]
        let done = {
            let mut st = 0;
            let mut ru: libc::rusage = unsafe { std::mem::zeroed() };
            let pid = unsafe { libc::wait4(child.id() as i32, &mut st, libc::WNOHANG, &mut ru) };
            if pid > 0 {
                peak_kb = ru.ru_maxrss as u64;
                if libc::WIFSIGNALED(st) { signal = Some(libc::WTERMSIG(st)); Some(-1) } else { Some(libc::WEXITSTATUS(st)) }
            } else { None }
        };
        if let Some(code) = done { status = Some(code); break; }
        if start.elapsed() > limit || CANCEL.load(Ordering::Relaxed) {
            timed_out = start.elapsed() > limit;
            #[cfg(windows)]
            { if let Some(j) = &job { j.kill(); } let _ = child.kill(); let _ = child.wait(); }
            #[cfg(unix)]
            { let _ = child.kill(); let mut st = 0; unsafe { libc::waitpid(child.id() as i32, &mut st, 0); } }
            status = None;
            break;
        }
        std::thread::sleep(Duration::from_millis(if start.elapsed() < Duration::from_millis(50) { 1 } else { 4 }));
    }
    r.time_ms = start.elapsed().as_millis() as u64;
    #[cfg(windows)]
    {
        // judged on the peak working set (memory really used), like most judges; a JVM commits
        // far more than it touches, so committed memory would turn every Java run into MLE
        peak_kb = win::peak_kb(&child);
        if peak_kb == 0 { peak_kb = job.as_ref().map(|j| j.peak_kb()).unwrap_or(0); }
    }
    r.memory_kb = peak_kb;
    let _ = writer.join();
    let stdout = out_t.join().unwrap_or_default();
    let stderr = err_t.join().unwrap_or_default();
    r.output = String::from_utf8_lossy(&stdout).into_owned();
    r.stderr = String::from_utf8_lossy(&stderr).into_owned();
    if stdout.len() >= MAX_OUTPUT { r.message = Some("Output is longer than 4 MB; the rest isn't shown.".into()); }

    #[allow(unused_mut)]
    let mut over_memory = ml_mb > 0 && peak_kb > ml_mb * 1024;
    #[cfg(windows)]
    if job.as_ref().is_some_and(|j| j.memory_hit()) { over_memory = true; }
    // out of memory shows up as a crash on Linux (no job object): the runtime says so in stderr
    if status != Some(0) && ["bad_alloc", "MemoryError", "OutOfMemoryError", "out of memory"].iter().any(|m| r.stderr.contains(m)) { over_memory = true; }
    r.verdict = if CANCEL.load(Ordering::Relaxed) && !timed_out {
        r.message = Some("Stopped.".into());
        "STOPPED".into()
    } else if timed_out {
        "TLE".into()
    } else if over_memory {
        r.message = Some(format!("Used more than the {ml_mb} MB memory limit."));
        "MLE".into()
    } else {
        let code = status.unwrap_or(-1);
        #[cfg(unix)]
        let crashed = code != 0 || signal.is_some();
        #[cfg(windows)]
        let crashed = code != 0;
        if crashed {
            #[cfg(windows)]
            { r.message = Some(match crash_reason(code) { Some(why) => format!("Exit code {code:#x}: {why}"), None => format!("Exit code {code}") }); }
            #[cfg(unix)]
            { r.message = Some(match signal { Some(11) => "Segmentation fault: a bad pointer or an array index out of range".into(), Some(8) => "Floating point exception (division by zero?)".into(), Some(6) => "Aborted".into(), Some(9) => "Killed (out of memory?)".into(), Some(s) => format!("Killed by signal {s}"), None => format!("Exit code {code}") }); }
            "RE".into()
        } else {
            match &test.expected {
                Some(e) if !e.trim().is_empty() => if same_answer(&r.output, e) { "AC".into() } else { "WA".into() },
                _ => "OK".into(),
            }
        }
    };
    r
}

#[cfg(windows)]
mod win {
    use std::os::windows::io::AsRawHandle;
    use windows_sys::Win32::Foundation::{CloseHandle, HANDLE, INVALID_HANDLE_VALUE};
    use windows_sys::Win32::System::IO::{CreateIoCompletionPort, GetQueuedCompletionStatus};
    use windows_sys::Win32::System::JobObjects::*;
    use windows_sys::Win32::System::ProcessStatus::{GetProcessMemoryInfo, PROCESS_MEMORY_COUNTERS};

    /// A job object: kills the program's whole process tree on TLE, caps runaway memory, and
    /// reports the peak memory.
    pub struct Job(HANDLE, HANDLE);
    unsafe impl Send for Job {}

    const JOB_OBJECT_MSG_JOB_MEMORY_LIMIT: u32 = 10;

    impl Job {
        pub fn new(ml_mb: u64) -> Option<Job> {
            unsafe {
                let h = CreateJobObjectW(std::ptr::null(), std::ptr::null());
                if h.is_null() { return None; }
                let mut info: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = std::mem::zeroed();
                info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if ml_mb > 0 {
                    info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_MEMORY;
                    info.JobMemoryLimit = ((ml_mb * 2 + 256) << 20) as usize; // a safety net; MLE is judged from the peak
                }
                SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info as *const _ as *const _, std::mem::size_of_val(&info) as u32);
                // the job reports on this port when an allocation fails because of the limit
                let port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, std::ptr::null_mut(), 0, 1);
                if !port.is_null() {
                    let assoc = JOBOBJECT_ASSOCIATE_COMPLETION_PORT { CompletionKey: std::ptr::null_mut(), CompletionPort: port };
                    SetInformationJobObject(h, JobObjectAssociateCompletionPortInformation, &assoc as *const _ as *const _, std::mem::size_of_val(&assoc) as u32);
                }
                Some(Job(h, port))
            }
        }
        pub fn assign(&self, child: &std::process::Child) -> bool {
            unsafe { AssignProcessToJobObject(self.0, child.as_raw_handle() as HANDLE) != 0 }
        }
        pub fn kill(&self) {
            unsafe { TerminateJobObject(self.0, 1); }
        }
        /// Did the program hit the memory cap (an allocation failed)?
        pub fn memory_hit(&self) -> bool {
            if self.1.is_null() { return false; }
            unsafe {
                let (mut code, mut key, mut ov) = (0u32, 0usize, std::ptr::null_mut());
                while GetQueuedCompletionStatus(self.1, &mut code, &mut key, &mut ov, 0) != 0 {
                    if code == JOB_OBJECT_MSG_JOB_MEMORY_LIMIT { return true; }
                }
                false
            }
        }
        pub fn peak_kb(&self) -> u64 {
            unsafe {
                let mut info: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = std::mem::zeroed();
                let ok = QueryInformationJobObject(self.0, JobObjectExtendedLimitInformation, &mut info as *mut _ as *mut _, std::mem::size_of_val(&info) as u32, std::ptr::null_mut());
                if ok != 0 { (info.PeakProcessMemoryUsed / 1024) as u64 } else { 0 }
            }
        }
    }
    impl Drop for Job {
        fn drop(&mut self) { unsafe { CloseHandle(self.0); if !self.1.is_null() { CloseHandle(self.1); } } }
    }

    /// Peak working set of the (finished) program.
    pub fn peak_kb(child: &std::process::Child) -> u64 {
        unsafe {
            let mut pmc: PROCESS_MEMORY_COUNTERS = std::mem::zeroed();
            pmc.cb = std::mem::size_of::<PROCESS_MEMORY_COUNTERS>() as u32;
            if GetProcessMemoryInfo(child.as_raw_handle() as HANDLE, &mut pmc, pmc.cb) != 0 { (pmc.PeakWorkingSetSize / 1024) as u64 } else { 0 }
        }
    }
}

// ---------------------------------------------------------------- commands

#[derive(Serialize, Clone)]
#[serde(rename_all = "camelCase")]
struct Progress {
    phase: &'static str,
    index: Option<usize>,
    result: Option<TestResult>,
}

/// Compile (when needed) and run the tests one by one; each result is also sent as a "run-progress" event.
/// A .sql file: run on the built-in SQLite with a fresh sample database; same verdicts as programs.
fn run_sql(path: &Path, req: &RunRequest) -> RunResult {
    let mut res = RunResult::default();
    res.compile.ok = true;
    res.compile.cached = true;
    let src = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => { res.error = Some(format!("Can't read the file: {e}")); return res; }
    };
    let expected = req.tests.first().and_then(|t| t.expected.clone());
    let start = Instant::now();
    let o = crate::sql::run(&src, Duration::from_millis(req.time_limit_ms.max(100)), &CANCEL);
    let mut r = TestResult { index: 0, output: o.output, time_ms: start.elapsed().as_millis() as u64, ..Default::default() };
    r.verdict = if o.timed_out {
        "TLE".into()
    } else if let Some(err) = o.error {
        r.message = Some(err);
        "RE".into()
    } else {
        match &expected {
            Some(e) if !e.trim().is_empty() => if same_answer(&r.output, e) { "AC".into() } else { "WA".into() },
            _ => "OK".into(),
        }
    };
    res.tests.push(r);
    res
}

#[tauri::command]
pub async fn run_tests(app: AppHandle, req: RunRequest) -> RunResult {
    CANCEL.store(false, Ordering::Relaxed);
    tauri::async_runtime::spawn_blocking(move || {
        let path = PathBuf::from(&req.path);
        let mut res = RunResult::default();
        if path.extension().is_some_and(|e| e.eq_ignore_ascii_case("sql")) {
            return run_sql(&path, &req);
        }
        if lang_of(&path).is_none() {
            res.error = Some("NppDev can run C, C++, Java, Python, JavaScript, Go, Rust, Kotlin, C# and SQL files.".into());
            return res;
        }
        let _ = app.emit("run-progress", Progress { phase: "compile", index: None, result: None });
        let prep = match prepare(&path, &mut res.compile) {
            Ok(p) => p,
            Err(Some(key)) if !key.is_empty() => { res.missing = Some(key); return res; }
            Err(_) => { if res.compile.output.is_empty() { res.compile.output = "The compiler could not be started.".into(); } return res; }
        };
        let cwd = path.parent().unwrap_or(Path::new(".")).to_path_buf();
        let tests: Vec<(usize, &TestCase)> = req.tests.iter().enumerate().filter(|(i, _)| req.only.is_empty() || req.only.contains(i)).collect();
        let empty = TestCase { input: String::new(), expected: None };
        let tests = if tests.is_empty() { vec![(0, &empty)] } else { tests };
        for (i, t) in tests {
            if CANCEL.load(Ordering::Relaxed) { break; }
            let _ = app.emit("run-progress", Progress { phase: "test", index: Some(i), result: None });
            let r = run_one(&prep, &cwd, i, t, req.time_limit_ms.max(100), req.memory_limit_mb);
            let _ = app.emit("run-progress", Progress { phase: "result", index: Some(i), result: Some(r.clone()) });
            res.tests.push(r);
        }
        res
    }).await.unwrap_or_default()
}

#[tauri::command]
pub fn stop_run() {
    CANCEL.store(true, Ordering::Relaxed);
}

#[cfg(test)]
mod judge_tests {
    use super::*;

    fn judge(name: &str, code: &str, input: &str, expected: &str, tl: u64, ml: u64) -> (String, TestResult) {
        let dir = std::env::temp_dir().join("nppdev-judge-tests");
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join(name);
        std::fs::write(&path, code).unwrap();
        let mut c = CompileResult::default();
        match prepare(&path, &mut c) {
            Ok(p) => {
                let t = TestCase { input: input.into(), expected: Some(expected.into()) };
                let r = run_one(&p, &dir, 0, &t, tl, ml);
                (r.verdict.clone(), r)
            }
            Err(Some(k)) => (format!("MISSING {k}"), TestResult::default()),
            Err(None) => ("CE".into(), TestResult { message: Some(c.output), ..Default::default() }),
        }
    }

    #[test]
    fn verdicts() {
        let cases: &[(&str, &str, &str, &str, u64, u64, &str)] = &[
            ("sum.cpp", "#include <bits/stdc++.h>\nint main(){long long a,b;std::cin>>a>>b;std::cout<<a+b<<std::endl;}", "2 3\n", "5", 2000, 256, "AC"),
            ("wa.cpp", "#include <iostream>\nint main(){std::cout<<42<<std::endl;}", "", "41", 2000, 256, "WA"),
            ("re.cpp", "#include <vector>\nint main(){int*p=nullptr;*p=1;return 0;}", "", "", 2000, 256, "RE"),
            ("tle.cpp", "int main(){volatile long long x=0;for(;;)x++;}", "", "", 500, 256, "TLE"),
            ("mle.cpp", "#include <vector>\n#include <iostream>\nint main(){std::vector<int> v(100000000,1);long long s=0;for(int x:v)s+=x;std::cout<<s;}", "", "100000000", 5000, 64, "MLE"),
            ("ce.cpp", "int main(){ return x; }", "", "", 2000, 256, "CE"),
            ("c20.cpp", "#include <bits/stdc++.h>\nint main(){std::vector<int> v{3,1,2};std::ranges::sort(v);for(int x:v)std::cout<<x<<' ';}", "", "1 2 3", 3000, 256, "AC"),
            ("Main.java", "import java.util.*;\npublic class Main{public static void main(String[] a){Scanner s=new Scanner(System.in);System.out.println(s.nextInt()*2);}}", "21", "42", 5000, 256, "AC"),
            ("echo.py", "print(int(input())**2)", "12", "144", 3000, 256, "AC"),
            ("div.py", "print(1//0)", "", "", 3000, 256, "RE"),
            ("add.rs", "use std::io::*;
fn main(){let mut s=String::new();stdin().read_to_string(&mut s).unwrap();let v:Vec<i64>=s.split_whitespace().map(|x|x.parse().unwrap()).collect();println!(\"{}\",v.iter().sum::<i64>());}", "1 2 3", "6", 5000, 256, "AC"),
            ("Prog.cs", "using System;
class P{static void Main(){var p=Console.ReadLine().Split(' ');Console.WriteLine(int.Parse(p[0])*int.Parse(p[1]));}}", "6 7", "42", 5000, 256, "AC"),
            ("read.js", "const n = +readline(); print(n + 1);", "9", "10", 3000, 256, "AC"),
        ];
        let mut bad = vec![];
        for &(name, code, input, exp, tl, ml, want) in cases {
            let (got, r) = judge(name, code, input, exp, tl, ml);
            println!("{name:10} {got:8} want {want:4} {:5} ms {:7} KB  {:?}", r.time_ms, r.memory_kb, r.message.as_deref().map(|m| m.chars().take(90).collect::<String>()));
            if got != want && !got.starts_with("MISSING") { bad.push(name); }
        }
        assert!(bad.is_empty(), "wrong verdicts: {bad:?}");
    }
}
