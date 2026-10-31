//! Integration test on the prepared shared starting world (`save_010.xml.gz`).
//!
//! The save is not in Git (it is player data). Copy it to `<repo>/.local/saves/` or set
//! `X4MP_SAVE_010` to its path; otherwise the test is skipped with a message. It is
//! slow in debug builds; prefer `cargo test --release -p x4mp-save`.

use std::path::PathBuf;

use x4mp_save::index::build_index;
use x4mp_save::io::{open_input, ThreadedGzWriter};
use x4mp_save::trim::{plan, write_trimmed, ScriptRefs, TrimOptions};
use x4mp_save::verify::{dangling_targets, verify};

fn save_path() -> Option<PathBuf> {
    if let Ok(p) = std::env::var("X4MP_SAVE_010") {
        return Some(PathBuf::from(p));
    }
    let p = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../.local/saves/save_010.xml.gz");
    p.exists().then_some(p)
}

#[test]
fn trim_then_verify_save_010() {
    let Some(src) = save_path() else {
        eprintln!("SKIPPED: save_010.xml.gz not found (copy it to .local/saves/ or set X4MP_SAVE_010)");
        return;
    };
    let base = build_index(open_input(&src).unwrap()).expect("original is well-formed");
    let baseline = dangling_targets(&base);
    let in_size = std::fs::metadata(&src).unwrap().len();
    let dir = std::env::temp_dir().join(format!("x4mp-save-test-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();

    for (mode, strict, client) in
        [(ScriptRefs::Weak, false, false), (ScriptRefs::Strong, true, false), (ScriptRefs::Weak, false, true)]
    {
        let opts = TrimOptions {
            sectors: vec!["cluster_14_sector001_macro".into()],
            neighbours: 1,
            keep_owner: "player".into(),
            script_refs: mode,
            client,
            hard: false,
            prune_md: Vec::new(),
            masstraffic_off: false,
        space_overrides: Vec::new(),
        override_classes: Vec::new(),
        };
        let p = plan(&base, &opts).unwrap();
        let out = dir.join(format!("trim_{mode:?}_{client}.xml.gz"));
        let gz = ThreadedGzWriter::create(&out).unwrap();
        let (st, gz) = write_trimmed(&base, &p, open_input(&src).unwrap(), gz).unwrap();
        gz.finish().unwrap();
        let out_size = std::fs::metadata(&out).unwrap().len();
        let trimmed = build_index(open_input(&out).unwrap()).expect("trimmed save is well-formed");
        let r = verify(&trimmed, Some(&baseline), strict);
        eprintln!(
            "{mode:?} client={client}: XML {} -> {} ({:.1}% removed), gzip {in_size} -> {out_size} ({:.1}% removed), \
             dropped {} components; verify: new structural {}, new script values {}",
            st.in_bytes,
            st.out_bytes,
            100.0 - 100.0 * st.out_bytes as f64 / st.in_bytes as f64,
            100.0 - 100.0 * out_size as f64 / in_size as f64,
            st.skipped_components,
            r.new_structural,
            r.new_script_values
        );
        assert!(r.ok(), "{}", r.render());
        assert_eq!(r.new_structural, 0);
        assert_eq!(trimmed.comps.len() as u64 + st.skipped_components, base.comps.len() as u64);
        if client {
            assert_eq!(st.client_jobs_rewritten, base.jobs.jobs);
            assert_eq!(trimmed.jobs.enabled.as_deref(), Some("0"));
            assert_eq!(trimmed.jobs.active + trimmed.jobs.firsttime + trimmed.jobs.timers, 0);
            assert_eq!(trimmed.jobs.inactive, base.jobs.jobs);
            assert_eq!(trimmed.jobs.requests, 0);
        }
        if mode == ScriptRefs::Weak {
            assert!(out_size * 10 < in_size * 8, "weak trim should remove more than 20% of the gzip size");
        }
    }
    let _ = std::fs::remove_dir_all(&dir);
}
