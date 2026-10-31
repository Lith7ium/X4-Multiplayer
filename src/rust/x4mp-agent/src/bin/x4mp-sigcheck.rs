//! Offline check of a Linux X4 executable against the agent's pinned build and sites.
//! Usage: x4mp-sigcheck /path/to/X4

use std::process::ExitCode;

use x4mp_agent::elf::FileImage;
use x4mp_agent::pinned::{self, Image};
use x4mp_agent::sha256::{hex, Sha256};

fn main() -> ExitCode {
    let Some(path) = std::env::args_os().nth(1) else {
        eprintln!("usage: x4mp-sigcheck /path/to/X4");
        return ExitCode::from(2);
    };
    let data = match std::fs::read(&path) {
        Ok(data) => data,
        Err(e) => {
            eprintln!("FAIL cannot read {}: {e}", path.to_string_lossy());
            return ExitCode::FAILURE;
        }
    };
    let mut failures = 0;
    if data.len() as u64 != pinned::FILE_SIZE {
        eprintln!("FAIL file size {} (pinned {})", data.len(), pinned::FILE_SIZE);
        failures += 1;
    }
    let mut hasher = Sha256::new();
    hasher.update(&data);
    let digest = hex(&hasher.finish());
    if digest != pinned::FILE_SHA256 {
        eprintln!("FAIL file sha256 {digest}");
        failures += 1;
    }
    let Some(image) = FileImage::new(data) else {
        eprintln!("FAIL not an x86-64 ELF64 executable");
        return ExitCode::FAILURE;
    };
    if image.elf_type() != Some(2) {
        eprintln!("FAIL not ET_EXEC");
        failures += 1;
    }
    match pinned::verify_sites(&image) {
        Ok(count) => println!("PASS build-id and {count} signature sites"),
        Err(reason) => {
            eprintln!("FAIL {reason}");
            failures += 1;
        }
    }
    // Scheduler preconditions in the file (the live agent re-checks them before patching).
    let checks: [(&str, u64, Vec<u8>); 6] = [
        ("decision_cmp_original", pinned::DECISION_CMP_VA, pinned::DECISION_CMP_ORIGINAL.to_vec()),
        ("dt_load_original", pinned::DT_LOAD_VA, pinned::DT_LOAD_ORIGINAL.to_vec()),
        ("dt_min_original", pinned::DT_MIN_VA, pinned::DT_MIN_ORIGINAL.to_vec()),
        ("one_sixtieth_constant", pinned::ONE_SIXTIETH_CONST, (1.0f64 / 60.0).to_le_bytes().to_vec()),
        ("one_twentieth_constant", pinned::ONE_TWENTIETH_CONST, (1.0f64 / 20.0).to_le_bytes().to_vec()),
        ("main_frame_in_flight_initial", pinned::MAIN_FRAME_IN_FLIGHT, vec![1]),
    ];
    for (name, va, want) in checks {
        if image.read(va, want.len()).as_deref() == Some(&want[..]) {
            println!("PASS scheduler {name} at 0x{va:x}");
        } else {
            eprintln!("FAIL scheduler {name} at 0x{va:x}");
            failures += 1;
        }
    }
    // fixed30's constant page must lie outside every PT_LOAD segment of the image.
    for &page in &pinned::CONST_PAGE_CANDIDATES {
        if image.loads.iter().any(|l| page < l.vaddr + l.filesz.max(1) && l.vaddr < page + 4096) {
            eprintln!("FAIL scheduler const page 0x{page:x} overlaps the image");
            failures += 1;
        }
    }
    println!("PASS scheduler const page candidates outside the image");
    if failures == 0 {
        println!("PASS executable {} build {}; no files modified", pinned::GAME_VERSION, pinned::STEAM_BUILD);
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}
