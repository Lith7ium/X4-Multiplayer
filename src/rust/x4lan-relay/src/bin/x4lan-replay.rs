//! Deterministic replay driver for the differential parity harness.
//!
//! Reads one JSON command per stdin line and writes one JSON result per line. The same
//! protocol is implemented for the Python relay by `src/rust/parity/py_driver.py`; leases
//! come from a counter so both sides produce byte-identical packets.
//!
//!   {"op":"new","level":"coop","token":..,"build":..,"bubble":..} -> {"ok":true} | {"error":msg}
//!   {"op":"recv","t":0.1,"addr":"127.0.0.1:10001","hex":"48454c4c4f.."} -> {"out":[hex,..]}
//!   {"op":"snap","t":0.2}   -> {"out":[[addr,hex],..]}
//!   {"op":"expire","t":30}  -> {}
//!   {"op":"float","text":".."} -> {"v":"<f64 bits hex>"|null}
//!   {"op":"fmt","x":1.5}    -> {"g9":..,"g17":..,"f3":..}

use std::io::{BufRead, Write};
use std::net::SocketAddr;

use x4lan_relay::json::{quote, Json};
use x4lan_relay::pynum::{fmt_f, fmt_g, py_float};
use x4lan_relay::session::{bundle, LeaseSource, Level, Session};

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn unhex(text: &str) -> Vec<u8> {
    (0..text.len() / 2).map(|i| u8::from_str_radix(&text[2 * i..2 * i + 2], 16).expect("hex")).collect()
}

fn level(name: &str) -> Option<Level> {
    Some(match name {
        "base" => Level::Base,
        "bubble" => Level::Bubble,
        "economy" => Level::Economy,
        "coop" => Level::Coop,
        "dock" => Level::Dock,
        "universe" => Level::Universe,
        _ => return None,
    })
}

fn main() {
    let stdin = std::io::stdin();
    let stdout = std::io::stdout();
    let mut out = std::io::BufWriter::new(stdout.lock());
    let mut session: Option<Session> = None;
    for line in stdin.lock().lines() {
        let line = line.expect("stdin");
        if line.trim().is_empty() {
            continue;
        }
        let cmd = Json::parse(&line).expect("command JSON");
        let op = cmd.get("op").and_then(Json::as_str).expect("op");
        let text = |key: &str| cmd.get(key).and_then(Json::as_str).unwrap_or_default().to_string();
        let t = cmd.get("t").and_then(Json::as_f64).unwrap_or(0.0);
        let reply = match op {
            "new" => {
                let level = level(&text("level")).expect("level");
                match Session::new(level, &text("token"), &text("build"), &text("bubble"), LeaseSource::Counter(0)) {
                    Ok(s) => {
                        session = Some(s);
                        "{\"ok\":true}".to_string()
                    }
                    Err(e) => {
                        session = None;
                        format!("{{\"error\":{}}}", quote(&e))
                    }
                }
            }
            "recv" => {
                let address: SocketAddr = text("addr").parse().expect("addr");
                let replies = session.as_mut().expect("session").receive(&unhex(&text("hex")), address, t);
                let items: Vec<String> = replies.iter().map(|r| quote(&hex(r))).collect();
                format!("{{\"out\":[{}]}}", items.join(","))
            }
            "snap" => {
                // The relay loop bundles every tick for XBCAP peers (host.py `bundle`).
                let session = session.as_mut().expect("session");
                let packets = bundle(session.snapshots(t), &session.core.peers);
                let items: Vec<String> =
                    packets.iter().map(|(a, p)| format!("[{},{}]", quote(&a.to_string()), quote(&hex(p)))).collect();
                format!("{{\"out\":[{}]}}", items.join(","))
            }
            "expire" => {
                session.as_mut().expect("session").expire(t);
                "{}".to_string()
            }
            "float" => match py_float(&text("text")) {
                Some(v) => format!("{{\"v\":\"{:016x}\"}}", v.to_bits()),
                None => "{\"v\":null}".to_string(),
            },
            "fmt" => {
                let x = match cmd.get("bits").and_then(Json::as_str) {
                    Some(bits) => f64::from_bits(u64::from_str_radix(bits, 16).expect("bits")),
                    None => cmd.get("x").and_then(Json::as_f64).expect("x"),
                };
                format!(
                    "{{\"g9\":{},\"g17\":{},\"f3\":{}}}",
                    quote(&fmt_g(x, 9)),
                    quote(&fmt_g(x, 17)),
                    quote(&fmt_f(x, 3))
                )
            }
            other => panic!("unknown op {other}"),
        };
        writeln!(out, "{reply}").expect("stdout");
    }
}
