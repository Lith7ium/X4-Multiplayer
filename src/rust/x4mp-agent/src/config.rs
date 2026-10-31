//! Opt-in configuration from the environment. The agent is off unless `X4MP_AGENT=1`.

use std::path::PathBuf;

use crate::scheduler::{RenderPolicy, SchedConfig};

pub const ENV_ENABLE: &str = "X4MP_AGENT";
pub const ENV_LOG: &str = "X4MP_AGENT_LOG";
pub const ENV_CADENCE_MS: &str = "X4MP_AGENT_CADENCE_MS";
pub const ENV_SETTLE_MS: &str = "X4MP_AGENT_SETTLE_MS";
pub const ENV_DISARM_FILE: &str = "X4MP_AGENT_DISARM_FILE";
/// `fixed60`, `fixed30` or `fixed20` enables the fixed-step scheduler at that step rate;
/// unset or `off` leaves it off.
pub const ENV_SCHED: &str = "X4MP_SCHED";
/// `never`, or the minimum ms between renders (0 = render every on-time step).
pub const ENV_RENDER_INTERVAL_MS: &str = "X4MP_RENDER_INTERVAL_MS";
pub const ENV_SCHED_MAX_CATCHUP: &str = "X4MP_SCHED_MAX_CATCHUP";
pub const ENV_SCHED_MAX_LAG_MS: &str = "X4MP_SCHED_MAX_LAG_MS";
pub const ENV_SCHED_WAIT_MS: &str = "X4MP_SCHED_WAIT_MS";
/// Longest time without a render, even while behind or with `never` (0 disables).
pub const ENV_SCHED_MAX_RENDER_GAP_MS: &str = "X4MP_SCHED_MAX_RENDER_GAP_MS";
/// Stall watchdog: no simulation step for this long outside loading restores and disarms.
pub const ENV_SCHED_STALL_MS: &str = "X4MP_SCHED_STALL_MS";

/// Hard cap on consecutive no-render steps (`X4MP_SCHED_MAX_CATCHUP` upper bound).
pub const MAX_CATCHUP_CAP: u64 = 3600;

/// Fixed-step scheduler settings (only with `X4MP_SCHED=fixedN`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Sched {
    pub policy: SchedConfig,
    /// Longest wait in slot 13 for the main thread to leave FrameTick before the frame
    /// is left undecided. 100..=60000 ms, default 5000.
    pub wait_ms: u64,
    /// Stall watchdog limit. 1000..=600000 ms, default 10000.
    pub stall_ms: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Config {
    /// Cadence/diagnostic log. Default `$HOME/x4mp-agent/agent-<pid>.log`.
    pub log: PathBuf,
    /// Period of `CADENCE` summary lines (wall clock), 100..=60000 ms, default 1000.
    pub cadence_ms: u64,
    /// The game must be out of loading, with game time advancing, for this long before
    /// the frame callback is registered. 500..=600000 ms, default 2000.
    pub settle_ms: u64,
    /// When this file exists the agent disarms permanently. Default `<log>.disarm`.
    pub disarm_file: PathBuf,
    /// `None` unless `X4MP_SCHED=fixedN`.
    pub sched: Option<Sched>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Parsed {
    /// `X4MP_AGENT` is not exactly `1`: do nothing at all.
    Disabled,
    Enabled(Config),
    /// Opted in, but the configuration is unusable: refuse (fail closed).
    Invalid(String),
}

fn bounded(value: Option<String>, name: &str, default: u64, lo: u64, hi: u64) -> Result<u64, String> {
    match value {
        None => Ok(default),
        Some(text) => match text.parse::<u64>() {
            Ok(v) if (lo..=hi).contains(&v) => Ok(v),
            _ => Err(format!("{name} must be an integer from {lo} to {hi}")),
        },
    }
}

fn absolute(text: &str, name: &str) -> Result<PathBuf, String> {
    let path = PathBuf::from(text);
    if text.is_empty() || !path.is_absolute() || text.contains('\0') || text.contains('\n') {
        return Err(format!("{name} must be an absolute path"));
    }
    Ok(path)
}

fn parse_sched(get: &impl Fn(&str) -> Option<String>) -> Result<Option<Sched>, String> {
    let hz = match get(ENV_SCHED).as_deref() {
        None | Some("off") => return Ok(None),
        Some("fixed60") => 60,
        Some("fixed30") => 30,
        Some("fixed20") => 20,
        Some(_) => return Err(format!("{ENV_SCHED} must be fixed60, fixed30, fixed20 or off")),
    };
    let render = match get(ENV_RENDER_INTERVAL_MS).as_deref() {
        Some("never") => RenderPolicy::Never,
        other => RenderPolicy::IntervalMs(bounded(other.map(str::to_owned), ENV_RENDER_INTERVAL_MS, 0, 0, 600_000)?),
    };
    let max_catchup_steps =
        bounded(get(ENV_SCHED_MAX_CATCHUP), ENV_SCHED_MAX_CATCHUP, 120, 1, MAX_CATCHUP_CAP)? as u32;
    let max_render_gap_ms = bounded(get(ENV_SCHED_MAX_RENDER_GAP_MS), ENV_SCHED_MAX_RENDER_GAP_MS, 5000, 0, 600_000)?;
    let max_lag_ms = bounded(get(ENV_SCHED_MAX_LAG_MS), ENV_SCHED_MAX_LAG_MS, 1000, 50, 60_000)?;
    let wait_ms = bounded(get(ENV_SCHED_WAIT_MS), ENV_SCHED_WAIT_MS, 5000, 100, 60_000)?;
    let stall_ms = bounded(get(ENV_SCHED_STALL_MS), ENV_SCHED_STALL_MS, 10_000, 1000, 600_000)?;
    if wait_ms >= stall_ms {
        return Err(format!("{ENV_SCHED_WAIT_MS} must be below {ENV_SCHED_STALL_MS}"));
    }
    Ok(Some(Sched {
        policy: SchedConfig { hz, render, max_catchup_steps, max_render_gap_ms, max_lag_ms },
        wait_ms,
        stall_ms,
    }))
}

/// `get` looks up an environment variable (injected for tests).
pub fn parse(get: impl Fn(&str) -> Option<String>, pid: u32) -> Parsed {
    if get(ENV_ENABLE).as_deref() != Some("1") {
        return Parsed::Disabled;
    }
    let result = (|| {
        let log = match get(ENV_LOG) {
            Some(text) => absolute(&text, ENV_LOG)?,
            None => {
                let home = get("HOME").ok_or("neither X4MP_AGENT_LOG nor HOME is set")?;
                absolute(&home, "HOME")?.join("x4mp-agent").join(format!("agent-{pid}.log"))
            }
        };
        let disarm_file = match get(ENV_DISARM_FILE) {
            Some(text) => absolute(&text, ENV_DISARM_FILE)?,
            None => {
                let mut name = log.clone().into_os_string();
                name.push(".disarm");
                PathBuf::from(name)
            }
        };
        Ok(Config {
            log,
            cadence_ms: bounded(get(ENV_CADENCE_MS), ENV_CADENCE_MS, 1000, 100, 60_000)?,
            settle_ms: bounded(get(ENV_SETTLE_MS), ENV_SETTLE_MS, 2000, 500, 600_000)?,
            disarm_file,
            sched: parse_sched(&get)?,
        })
    })();
    match result {
        Ok(config) => Parsed::Enabled(config),
        Err(reason) => Parsed::Invalid(reason),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    fn env(pairs: &[(&str, &str)]) -> impl Fn(&str) -> Option<String> {
        let map: HashMap<String, String> = pairs.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect();
        move |k| map.get(k).cloned()
    }

    #[test]
    fn off_unless_exactly_one() {
        for value in [None, Some("0"), Some("true"), Some("yes"), Some(" 1"), Some("")] {
            let pairs: Vec<(&str, &str)> = value.map(|v| vec![(ENV_ENABLE, v)]).unwrap_or_default();
            assert_eq!(parse(env(&pairs), 1), Parsed::Disabled, "{value:?}");
        }
    }

    #[test]
    fn defaults_under_home() {
        let Parsed::Enabled(c) = parse(env(&[(ENV_ENABLE, "1"), ("HOME", "/srv/x4")]), 42) else { panic!() };
        assert_eq!(c.log, PathBuf::from("/srv/x4/x4mp-agent/agent-42.log"));
        assert_eq!(c.disarm_file, PathBuf::from("/srv/x4/x4mp-agent/agent-42.log.disarm"));
        assert_eq!((c.cadence_ms, c.settle_ms), (1000, 2000));
        assert_eq!(c.sched, None);
    }

    #[test]
    fn scheduler_is_opt_in_and_bounded() {
        let base = [(ENV_ENABLE, "1"), ("HOME", "/h")];
        let with = |extra: &[(&str, &str)]| {
            let mut pairs = base.to_vec();
            pairs.extend_from_slice(extra);
            parse(env(&pairs), 1)
        };
        let Parsed::Enabled(c) = with(&[(ENV_SCHED, "off")]) else { panic!() };
        assert_eq!(c.sched, None);
        let Parsed::Enabled(c) = with(&[(ENV_SCHED, "fixed60")]) else { panic!() };
        assert_eq!(c.sched, Some(Sched { policy: SchedConfig::default(), wait_ms: 5000, stall_ms: 10_000 }));
        let Parsed::Enabled(c) = with(&[
            (ENV_SCHED, "fixed30"),
            (ENV_RENDER_INTERVAL_MS, "500"),
            (ENV_SCHED_MAX_CATCHUP, "600"),
            (ENV_SCHED_MAX_RENDER_GAP_MS, "0"),
            (ENV_SCHED_MAX_LAG_MS, "250"),
            (ENV_SCHED_WAIT_MS, "3000"),
            (ENV_SCHED_STALL_MS, "20000"),
        ]) else {
            panic!()
        };
        let s = c.sched.unwrap();
        assert_eq!(
            s.policy,
            SchedConfig {
                hz: 30,
                render: RenderPolicy::IntervalMs(500),
                max_catchup_steps: 600,
                max_render_gap_ms: 0,
                max_lag_ms: 250
            }
        );
        assert_eq!((s.wait_ms, s.stall_ms), (3000, 20_000));
        let Parsed::Enabled(c) = with(&[(ENV_SCHED, "fixed20")]) else { panic!() };
        assert_eq!(c.sched.unwrap().policy.hz, 20);
        let Parsed::Enabled(c) = with(&[(ENV_SCHED, "fixed60"), (ENV_RENDER_INTERVAL_MS, "never")]) else { panic!() };
        assert_eq!(c.sched.unwrap().policy.render, RenderPolicy::Never);
        for bad in [
            vec![(ENV_SCHED, "fixed45")],
            vec![(ENV_SCHED, "fixed60"), (ENV_SCHED_MAX_CATCHUP, "3601")],
            vec![(ENV_SCHED, "fixed60"), (ENV_SCHED_STALL_MS, "999")],
            vec![(ENV_SCHED, "fixed60"), (ENV_SCHED_WAIT_MS, "10000"), (ENV_SCHED_STALL_MS, "10000")],
            vec![(ENV_SCHED, "fixed60"), (ENV_SCHED_MAX_RENDER_GAP_MS, "x")],
            vec![(ENV_SCHED, "fixed60"), (ENV_RENDER_INTERVAL_MS, "-1")],
            vec![(ENV_SCHED, "fixed60"), (ENV_RENDER_INTERVAL_MS, "600001")],
            vec![(ENV_SCHED, "fixed60"), (ENV_SCHED_MAX_CATCHUP, "0")],
            vec![(ENV_SCHED, "fixed60"), (ENV_SCHED_MAX_LAG_MS, "10")],
            vec![(ENV_SCHED, "fixed60"), (ENV_SCHED_WAIT_MS, "x")],
        ] {
            assert!(matches!(with(&bad), Parsed::Invalid(_)), "{bad:?}");
        }
        // Without X4MP_AGENT=1 the scheduler variables do nothing at all.
        assert_eq!(parse(env(&[(ENV_SCHED, "fixed60")]), 1), Parsed::Disabled);
    }

    #[test]
    fn explicit_values_and_refusals() {
        let Parsed::Enabled(c) = parse(
            env(&[(ENV_ENABLE, "1"), (ENV_LOG, "/w/engine-agent.log"), (ENV_CADENCE_MS, "250"), (ENV_SETTLE_MS, "500")]),
            1,
        ) else {
            panic!()
        };
        assert_eq!((c.log.to_str().unwrap(), c.cadence_ms, c.settle_ms), ("/w/engine-agent.log", 250, 500));
        for bad in [
            vec![(ENV_ENABLE, "1")],
            vec![(ENV_ENABLE, "1"), (ENV_LOG, "relative.log")],
            vec![(ENV_ENABLE, "1"), (ENV_LOG, "/a"), (ENV_CADENCE_MS, "5")],
            vec![(ENV_ENABLE, "1"), (ENV_LOG, "/a"), (ENV_SETTLE_MS, "x")],
            vec![(ENV_ENABLE, "1"), (ENV_LOG, "/a"), (ENV_DISARM_FILE, "rel")],
        ] {
            assert!(matches!(parse(env(&bad), 1), Parsed::Invalid(_)), "{bad:?}");
        }
    }
}
