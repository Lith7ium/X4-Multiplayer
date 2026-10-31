//! `x4mp-save`: analyze, trim and verify X4 savegames (offline; read-only on inputs).

use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::time::Instant;

use x4mp_save::index::{build_index, build_index_with, SaveIndex};
use x4mp_save::io::{open_input, ThreadedGzWriter};
use x4mp_save::trim::{plan, summarize, write_trimmed, ScriptRefs, TrimOptions};
use x4mp_save::{analyze, peak_rss_bytes, verify};

const USAGE: &str = "\
usage:
  x4mp-save analyze <save> [--keep-owner <faction>] [--top N]
  x4mp-save trim <save> --sector <macro> [--sector <macro>...] [--neighbours N]
                 --keep-owner <faction> -o <out.xml.gz> [--script-refs weak|strong] [--client] [--top N]
                 [--hard] [--drop-class <class>...] [--prune-md <Script>:<$var>...] [--masstraffic-off]
                 [--space-overrides god=0,job=0,factionlogic=0 [--override-class sector,cluster,galaxy]]
  x4mp-save verify <save> [--baseline <original save>] [--strict]

Inputs are only read. <save> may be gzipped (.xml.gz) or plain XML.";

const FLAGS: &[&str] = &["--strict", "--client", "--hard", "--masstraffic-off"];

type Res<T> = Result<T, Box<dyn std::error::Error>>;

struct Args {
    positional: Vec<String>,
    opts: Vec<(String, String)>,
}

impl Args {
    fn parse(raw: &[String]) -> Res<Args> {
        let mut positional = Vec::new();
        let mut opts = Vec::new();
        let mut it = raw.iter();
        while let Some(a) = it.next() {
            if FLAGS.contains(&a.as_str()) {
                opts.push((a.trim_start_matches('-').to_string(), String::new()));
            } else if let Some(name) = a.strip_prefix("--").or_else(|| a.strip_prefix('-').filter(|n| *n == "o")) {
                let (name, val) = match name.split_once('=') {
                    Some((n, v)) => (n.to_string(), v.to_string()),
                    None => (name.to_string(), it.next().ok_or(format!("missing value for {a}"))?.clone()),
                };
                opts.push((name, val));
            } else {
                positional.push(a.clone());
            }
        }
        Ok(Args { positional, opts })
    }
    fn one(&self, k: &str) -> Option<&str> {
        self.opts.iter().rev().find(|(n, _)| n == k).map(|(_, v)| v.as_str())
    }
    fn flag(&self, k: &str) -> bool {
        self.opts.iter().any(|(n, _)| n == k)
    }
    fn all(&self, k: &str) -> Vec<String> {
        self.opts.iter().filter(|(n, _)| n == k).map(|(_, v)| v.clone()).collect()
    }
    fn check(&self, allowed: &[&str]) -> Res<()> {
        for (n, _) in &self.opts {
            if !allowed.contains(&n.as_str()) {
                return Err(format!("unknown option --{n}").into());
            }
        }
        Ok(())
    }
}

fn index_file(path: &Path) -> Res<SaveIndex> {
    Ok(build_index(open_input(path)?)?)
}

fn mem_line() -> String {
    match peak_rss_bytes() {
        Some(b) => format!("{:.0} MB", b as f64 / 1e6),
        None => "n/a on this OS".into(),
    }
}

fn file_header(path: &Path, idx: &SaveIndex) -> String {
    let size = std::fs::metadata(path).map(|m| m.len()).unwrap_or(0);
    format!(
        "file: {} bytes on disk, {} bytes XML, {} elements, {} attributes; index pass {:.2} s",
        size,
        idx.stats.bytes,
        idx.stats.elements,
        idx.stats.attributes,
        idx.stats.elapsed.as_secs_f64()
    )
}

fn cmd_analyze(a: &Args) -> Res<ExitCode> {
    a.check(&["keep-owner", "top"])?;
    let path = PathBuf::from(a.positional.get(1).ok_or("analyze: missing <save>")?);
    let top: usize = a.one("top").map(str::parse).transpose()?.unwrap_or(40);
    let owner = a.one("keep-owner").unwrap_or("player");
    let idx = index_file(&path)?;
    let t = Instant::now();
    let an = analyze::analyze(&idx, owner);
    println!("{}", file_header(&path, &idx));
    print!("{}", an.render(&idx, top));
    println!("\nanalysis {:.2} s; peak memory {}", t.elapsed().as_secs_f64(), mem_line());
    Ok(ExitCode::SUCCESS)
}

fn cmd_trim(a: &Args) -> Res<ExitCode> {
    a.check(&[
        "sector",
        "neighbours",
        "neighbors",
        "keep-owner",
        "o",
        "out",
        "top",
        "script-refs",
        "client",
        "hard",
        "drop-class",
        "prune-md",
        "masstraffic-off",
        "space-overrides",
        "override-class",
    ])?;
    let input = PathBuf::from(a.positional.get(1).ok_or("trim: missing <save>")?);
    let out = PathBuf::from(a.one("o").or(a.one("out")).ok_or("trim: missing -o <out>")?);
    let sectors = a.all("sector");
    if sectors.is_empty() {
        return Err("trim: at least one --sector <macro> is required".into());
    }
    let keep_owner = a.one("keep-owner").ok_or("trim: missing --keep-owner <faction>")?.to_string();
    let neighbours: u32 = a.one("neighbours").or(a.one("neighbors")).map(str::parse).transpose()?.unwrap_or(1);
    let top: usize = a.one("top").map(str::parse).transpose()?.unwrap_or(15);
    if out.exists() && std::fs::canonicalize(&out)? == std::fs::canonicalize(&input)? {
        return Err("trim: output must not overwrite the input".into());
    }
    let t0 = Instant::now();
    let drop_classes: Vec<String> =
        a.all("drop-class").iter().flat_map(|v| v.split(',')).map(|s| s.trim().to_string()).filter(|s| !s.is_empty()).collect();
    let idx = build_index_with(open_input(&input)?, &drop_classes)?;
    let script_refs: ScriptRefs = a.one("script-refs").unwrap_or("weak").parse()?;
    let client = a.flag("client");
    let hard = a.flag("hard");
    let masstraffic_off = a.flag("masstraffic-off");
    let mut prune_md = Vec::new();
    for v in a.all("prune-md") {
        let (s, var) = v.split_once(':').ok_or(format!("--prune-md expects <Script>:<$var>, not '{v}'"))?;
        prune_md.push((s.to_string(), var.to_string()));
    }
    let mut space_overrides = Vec::new();
    for kv in a.one("space-overrides").unwrap_or("").split(',').filter(|s| !s.is_empty()) {
        let (k, v) = kv.split_once('=').ok_or(format!("--space-overrides expects attr=value, not '{kv}'"))?;
        space_overrides.push((k.trim().to_string(), v.trim().to_string()));
    }
    let override_classes: Vec<String> =
        a.one("override-class").unwrap_or("sector").split(',').map(|s| s.trim().to_string()).filter(|s| !s.is_empty()).collect();
    let opts = TrimOptions {
        sectors,
        neighbours,
        keep_owner,
        script_refs,
        client,
        hard,
        prune_md,
        masstraffic_off,
        space_overrides,
        override_classes,
    };
    let t1 = Instant::now();
    let p = plan(&idx, &opts)?;
    let plan_time = t1.elapsed();
    let sum = summarize(&idx, &p);

    let tmp = out.with_extension("tmp");
    let t2 = Instant::now();
    let gz = ThreadedGzWriter::create(&tmp)?;
    let (st, gz) = write_trimmed(&idx, &p, open_input(&input)?, gz)?;
    gz.finish()?;
    std::fs::rename(&tmp, &out)?;
    let write_time = t2.elapsed();

    let in_size = std::fs::metadata(&input)?.len();
    let out_size = std::fs::metadata(&out)?.len();
    println!("{}", file_header(&input, &idx));
    println!("detail sectors (hops): ");
    for (s, d) in &p.detail_sectors {
        println!("  {d}  {}", idx.sector_macro(*s));
    }
    println!(
        "dynamic units: {} total, {} kept, {} dropped",
        sum.dynamic_total,
        sum.dynamic_kept,
        sum.dynamic_total - sum.dynamic_kept
    );
    println!("script values: {:?}", opts.script_refs);
    println!("dropped by class (units / their components):");
    let mut dc: Vec<_> = sum.dropped_by_class.iter().collect();
    dc.sort_by_key(|x| std::cmp::Reverse(x.1 .0));
    for (k, (u, c)) in dc {
        println!("  {k:<24} {u:>7} {c:>9}");
    }
    println!(
        "dropped: {} components, {} ai entities, {} script pool entries; pruned {} listeners, {} group members",
        st.skipped_components, st.skipped_entities, st.skipped_pool, st.pruned_listeners, st.pruned_members
    );
    if !drop_classes.is_empty() || hard {
        println!("sector-only: hard={hard} extra dynamic classes {drop_classes:?}");
    }
    for (k, v) in &st.pruned_structural {
        println!("  pruned structural reference element {k:<32} {v:>8}");
    }
    for (k, v) in &st.pruned_md {
        println!("  pruned MD instances of {k:<32} {v:>8}");
    }
    if !opts.space_overrides.is_empty() {
        println!(
            "space overrides {:?} on {:?}: {} merged into existing <overrides>, {} added",
            opts.space_overrides, opts.override_classes, st.overrides_merged, st.overrides_added
        );
    }
    if masstraffic_off {
        println!("<jobs masstraffic> {} -> 0", idx.jobs.masstraffic.as_deref().unwrap_or("absent"));
    }
    if p.client {
        let j = &idx.jobs;
        println!(
            "client mode: <jobs enabled> {} -> 0; {} jobs set active=0 (before: {} active=1, {} firsttime, {} timers)",
            j.enabled.as_deref().unwrap_or("absent"),
            st.client_jobs_rewritten,
            j.active,
            j.firsttime,
            j.timers
        );
        println!(
            "client mode: removed {} job-queue elements (queue had {} ships with {} components, {} ship mentions, \
             {} requests); {} queued ships kept because they are referenced",
            st.client_queue_elements,
            j.queued_ships,
            j.queued_components,
            j.queued_ship_refs,
            j.requests,
            p.client_queue_kept
        );
    }
    println!("kept dynamic units by first reason (top {top}):");
    let mut kr: Vec<_> = sum.kept_dynamic_by_reason.iter().collect();
    kr.sort_by(|a, b| b.1.cmp(a.1));
    for (k, v) in kr.into_iter().take(top) {
        println!("  {v:>7}  {k}");
    }
    println!("script pool entries kept by first reason (top {top}); dropped {}:", sum.dropped_pool);
    let mut pr: Vec<_> = sum.kept_pool_by_reason.iter().collect();
    pr.sort_by(|a, b| b.1.cmp(a.1));
    for (k, v) in pr.into_iter().take(top) {
        println!("  {v:>7}  {k}");
    }
    println!("distant sectors with most kept dynamic units (top {top}):");
    let mut ks: Vec<_> = sum.kept_distant_by_sector.iter().collect();
    ks.sort_by(|a, b| b.1.cmp(a.1));
    for (k, v) in ks.into_iter().take(top) {
        println!("  {v:>7}  {k}");
    }
    println!(
        "size: XML {} -> {} bytes ({:.1}% removed); gzip {} -> {} bytes ({:.1}% removed)",
        st.in_bytes,
        st.out_bytes,
        100.0 - 100.0 * st.out_bytes as f64 / st.in_bytes.max(1) as f64,
        in_size,
        out_size,
        100.0 - 100.0 * out_size as f64 / in_size.max(1) as f64
    );
    println!(
        "time: index {:.2} s, plan {:.2} s, write {:.2} s, total {:.2} s; peak memory {}",
        idx.stats.elapsed.as_secs_f64(),
        plan_time.as_secs_f64(),
        write_time.as_secs_f64(),
        t0.elapsed().as_secs_f64(),
        mem_line()
    );
    println!("wrote {}", out.display());
    Ok(ExitCode::SUCCESS)
}

fn cmd_verify(a: &Args) -> Res<ExitCode> {
    a.check(&["baseline", "strict"])?;
    let path = PathBuf::from(a.positional.get(1).ok_or("verify: missing <save>")?);
    let t = Instant::now();
    let baseline = match a.one("baseline") {
        Some(b) => {
            let bidx = index_file(Path::new(b))?;
            Some(verify::dangling_targets(&bidx))
        }
        None => None,
    };
    let idx = match index_file(&path) {
        Ok(i) => i,
        Err(e) => {
            println!("not well-formed: {e}\nresult: FAILED");
            return Ok(ExitCode::from(1));
        }
    };
    let strict = a.flag("strict");
    let rep = verify::verify(&idx, baseline.as_ref(), strict);
    println!("{}", file_header(&path, &idx));
    print!("{}", rep.render());
    println!("verify {:.2} s; peak memory {}", t.elapsed().as_secs_f64(), mem_line());
    Ok(if rep.ok() { ExitCode::SUCCESS } else { ExitCode::from(1) })
}

fn main() -> ExitCode {
    let raw: Vec<String> = std::env::args().skip(1).collect();
    let args = match Args::parse(&raw) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("error: {e}\n{USAGE}");
            return ExitCode::from(2);
        }
    };
    let r = match args.positional.first().map(String::as_str) {
        Some("analyze") => cmd_analyze(&args),
        Some("trim") => cmd_trim(&args),
        Some("verify") => cmd_verify(&args),
        _ => {
            eprintln!("{USAGE}");
            return ExitCode::from(2);
        }
    };
    match r {
        Ok(c) => c,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(2)
        }
    }
}
