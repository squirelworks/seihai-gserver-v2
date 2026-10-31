// Seihai lab tool, GPL-3.0 like the rest of this fork. See docs/lab/2026-10-31-npcserver-gs1.md.
//! Lab probe: log in to a loopback gserver with Seihai's protocol code, run a
//! small step script, and print every server message.
use seihai_core::net::{describe_message, login_gserver, random_key};
use seihai_core::protocol::ClientVersion;
use std::time::{Duration, Instant};

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let (addr, ver, acct, steps) = (&a[1], &a[2], &a[3], &a[4]);
    let version = ClientVersion::from_label(ver).expect("version");
    let mut s = login_gserver(addr, version, random_key(), acct, "labpw", false).expect("login");
    let rx = s.conn.spawn_reader().expect("reader");
    let t0 = Instant::now();
    let drain = |until: Instant| {
        while Instant::now() < until {
            match rx.recv_timeout(Duration::from_millis(50)) {
                Ok(Ok(body)) => {
                    for m in body.split_inclusive(|b| *b == b'\n') {
                        println!("[{:6.2}] S>C {}", t0.elapsed().as_secs_f32(), describe_message(m));
                    }
                }
                Ok(Err(e)) => { println!("[{:6.2}] closed: {e}", t0.elapsed().as_secs_f32()); std::process::exit(1) }
                Err(_) => {}
            }
        }
    };
    drain(Instant::now() + Duration::from_secs(2));
    let _ = s.enter_level.at_login(&mut s.conn);
    for line in std::fs::read_to_string(steps).unwrap().lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') { continue; }
        println!("[{:6.2}] STEP {line}", t0.elapsed().as_secs_f32());
        let (cmd, rest) = line.split_once(' ').unwrap_or((line, ""));
        let f: Vec<f32> = rest.split_whitespace().filter_map(|x| x.parse().ok()).collect();
        let r = match cmd {
            "wait" => { drain(Instant::now() + Duration::from_secs_f32(f[0])); Ok(()) }
            "walk" => s.conn.send_position(f[0], f[1], *f.get(2).unwrap_or(&2.0) as u8),
            "chat" => s.conn.send_chat(rest),
            "trigger" => {
                let p: Vec<&str> = rest.splitn(4, ' ').collect();
                s.conn.send_trigger_action(p[0].parse().unwrap(), p[1].parse().unwrap(), p[2], p.get(3).copied().unwrap_or(""))
            }
            "warp" => {
                let p: Vec<&str> = rest.split_whitespace().collect();
                s.conn.send_levelwarp(p[1].parse().unwrap(), p[2].parse().unwrap(), p[0])
            }
            "enter" => { let _ = s.conn.send_enter_level(version, &[]); Ok(()) }
            _ => { println!("bad step {cmd}"); Ok(()) }
        };
        if let Err(e) = r { println!("send error {e}"); }
        drain(Instant::now() + Duration::from_millis(std::env::var("STEP_MS").ok().and_then(|v| v.parse().ok()).unwrap_or(300)));
    }
    drain(Instant::now() + Duration::from_secs(1));
}
