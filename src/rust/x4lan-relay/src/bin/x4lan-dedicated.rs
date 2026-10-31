use std::path::PathBuf;
use std::process::ExitCode;

use x4lan_relay::dedicated::{command, Config, Supervisor};

const USAGE: &str = "usage: x4lan-dedicated --config FILE [run|status|stop|pause|resume]";

fn main() -> ExitCode {
    let mut args = std::env::args().skip(1);
    let mut config = None;
    let mut action = None;
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--config" => config = args.next().map(PathBuf::from),
            "run" | "status" | "stop" | "pause" | "resume" if action.is_none() => action = Some(arg),
            "-h" | "--help" => {
                println!("{USAGE}");
                return ExitCode::SUCCESS;
            }
            _ => {
                eprintln!("{USAGE}");
                return ExitCode::from(2);
            }
        }
    }
    let (Some(path), Some(action)) = (config, action) else {
        eprintln!("{USAGE}");
        return ExitCode::from(2);
    };
    let config = match Config::load(&path) {
        Ok(config) => config,
        Err(error) => {
            eprintln!("x4lan-dedicated: {error}");
            return ExitCode::from(1);
        }
    };
    let result = match action.as_str() {
        "run" => Supervisor::new(config).and_then(Supervisor::run),
        "stop" => command(&config, "shutdown"),
        "pause" => command(&config, "pause"),
        "resume" => command(&config, "resume"),
        "status" => {
            let status = config.work.join("status.json");
            match std::fs::read_to_string(&status) {
                Ok(text) => {
                    print!("{text}");
                    Ok(())
                }
                Err(error) => Err(format!("cannot read {}: {error}", status.display())),
            }
        }
        _ => unreachable!(),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("x4lan-dedicated: {error}");
            ExitCode::from(1)
        }
    }
}
