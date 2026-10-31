# Rust port of the X4 LAN relay and native UDP module (experimental)

Status: **experimental relay/native port plus a dedicated-server prototype**.
The original proof of concept was based on `contrib/astra-7-5-delivery` @ 8a56415
(7.5.0). The `contrib/kimi-dedicated-server` branch adds a supervised Linux authority
and two human guest roles; see `docs/DEDICATED-SERVER.md`. Ordinary packages retain the
Python relay and existing transport unless the dedicated build is selected.

**No native X4 acceptance is claimed by the Rust code alone.** The original evidence
came from synthetic peers on Windows 11. This branch also builds and tests on Linux,
but native game evidence is tracked separately in `reports/dedicated-server-wsl.md`.

## What is here

| Path | What it is |
| --- | --- |
| `x4lan-net/` | cdylib, Rust equivalent of `src/native/net.c` (`luaopen_x4lan_net`) |
| `x4lan-relay/` | library + `x4lan-relay` binary: `host.py` and every session layer |
| `x4lan-relay/src/dedicated.rs` | Linux process preflight, supervision, native heartbeat and local lifecycle control |
| `x4lan-relay/src/bin/x4lan-dedicated.rs` | unattended authority CLI (`run/status/pause/resume/stop`) |
| `x4lan-replay.rs` | deterministic replay driver used by the parity harness |
| `parity/` | differential harnesses (Python) comparing Python and Rust |
| `x4mp-save/` | offline savegame tool (`analyze`/`trim`/`verify`) for hot join: streaming analysis, per-client trimming by reference closure, optional client job neutralisation; see `x4mp-save/README.md` |

The runtime crates have **no third-party dependencies**. Like the Python relay (standard
library only), they depend on the Rust standard library and the OS (`bcrypt.dll`/`/dev/urandom`
for leases, `ws2_32` for one socket ioctl, `kernel32`/`libdl` for symbol lookup). The offline
`x4mp-save` tool is the exception: it uses `quick-xml` and `flate2` and is never
loaded into X4.

## What is ported

**Native module (`x4lan-net`)** - complete. Same Lua API (`open`, `send`, `receive`,
`close`, `__gc`), metatable name `x4lan.udp.v1` with `__index` = itself, 768-byte cap,
identical Lua error messages and check order, returns no value when the ABI cannot be
resolved. The Lua 5.1 C API is resolved at runtime exactly as net.c does:
`GetModuleHandleA("lua51_64.dll")` + `GetProcAddress` on Windows,
`dlsym(RTLD_DEFAULT, ..)` elsewhere; no Lua library is linked (no mlua). Deploy by
copying `x4lan_net.dll` to `src/extension/x4_lan_lab/native/x4lan_net_64.dll`
(Linux: `libx4lan_net.so` -> `x4lan_net.so`).

**Relay (`x4lan-relay`)** - every session class the relay can run, with byte-identical
output:

| Python | Rust |
| --- | --- |
| `host.py` `Session`, `Relay`, `main()` CLI | `session/mod.rs`, `relay.rs`, `bin/x4lan-relay.rs` |
| `bubble.py` `BubbleSession` | `session/bubble.rs` |
| `economy.py` `EconomySession`, `Assembly`, `parse_snapshot/lines` | `session/economy.rs` |
| `coop.py` `CoopSession` | `session/coop.rs` |
| `quests.py`, `missions.py` | `session/quests.rs`, `session/missions.rs` |
| `projectile_relay.py` + `projectile_protocol.py` (client parse, forwarded serialize) | `session/projectile.rs` |
| `docking.py` `DockSession` | `session/dock.rs` |
| `universe.py` (`Baseline`, `Receiver`, `Sender`, `UniverseSession`) | `session/universe.rs` |
| `cargo.py`, `identity.py`, `npc_combat.py` | `session/cargo.rs`, `identity.rs`, `npc.rs` |

`x4lan-relay` accepts the options of `python -m x4lan.host` (`--bind --port --token
--build --bubble --session --bubble-actions --trading`, argparse-style `--opt=value`
and unique prefixes, exit status 2 on usage errors) and selects the session class the
same way (`coop`/`world_sync` in session.json, then `--trading`, `--bubble-actions`).
Its console output matches line for line. The packaged launchers do not call that CLI;
they run `x4lan.monitor.run()` in-process, which is equivalent to
`x4lan-relay --bind 0.0.0.0 --session session.json --trading` minus the status
screen/JSON files (see "Not ported").

### Where the type system carries the protocol

The pitch is not "Rust is faster" (the relay is idle most of the time) but that the
protocol's invariants become types the compiler checks:

* Fields are parsed once into validated types: `Hex32` (token/nonce/lease/request,
  constant-time comparison built in), `Atom` / `Atom::parse_macro`, `Slot` (`p1`..`p8`,
  admitted only up to the session's `max_players`: 2 outside dedicated mode), `Ship`
  (1..8, validated against the session's `ship_count`; `is_fixture()` marks ships 1/2,
  which Python indexes with ints after re-checking `entity in (1, 2)` in ~20 places).
  A handler holding these never re-checks. N-player routing is dedicated-only; the
  two-player wire output stays byte-identical to Python (see `tests/nplayer.rs`).
* Lifecycles are enums: `CommandResult`, `OrderResult`, `CargoResult`, `MissionState`
  with its transition table (`may_become`), `DockMode::attached()`, `BodyMode`,
  `NavKind`, `WeaponKind`. A string like `"PENDNG"` cannot be stored.
* `CargoVerb::{Drop, Pickup { object }}` makes Python's
  `(DROP and obj != 0) or (PICKUP and obj == 0)` rule structural.
* `quests::Progress::parse` is the only constructor and enforces the stage invariants;
  `universe::Baseline::create` is the only way to obtain a baseline, so "a partial
  baseline cannot replace committed state" is enforced by construction.
* Coop's snapshot code in Python re-splits the strings that bubble/economy produced
  and rewrites them (`ORDER_EXEC` -> `COOP_EXEC`, drop `PEER/BODY/FIRE/DOCKSTATE`).
  The Rust layers emit the right packet at the source instead.
* The FFI boundary is explicit: every Lua entry point and every resolved API pointer
  uses the `"C-unwind"` ABI because `luaL_error` unwinds through our frames (SEH on
  Windows LuaJIT, DWARF on Linux LuaJIT, `longjmp` in PUC Lua). C compiles the same
  code without stating this; Rust forced the question and the answer is documented at
  each call site. `unsafe` in the relay is limited to two OS calls (RNG, one ioctl).

Where Python's semantics are part of the wire behaviour they are reproduced on purpose
and documented: CPython `float()` grammar (`" 1_0e1 "` is a valid pose value),
`format(x, '.9g')`, `'%.17g'`, `'.3f'` (all exact-value, round-half-even), dict
insertion order (`ordmap.rs`; it decides which pending command is sent first and the
order of forwarded frames), and `str.isspace()` vs `float()` whitespace.

## Parity evidence

All runs on Windows 11 x64, Python 3.12.10, Rust 1.94.1, commit of this README.

### Native module: C (`x4lan_net_64.dll` as shipped) vs Rust

`parity/native_parity.py` loads each DLL into a real LuaJIT 2.1 runtime registered as
`lua51_64.dll` (built from the `luajit-src` crate's vendored sources by
`parity/build_luajit.py`), through the **unmodified `transport.lua`**, and compares
observations: module/metatable shape, 7 bad ports, 20 host strings, wrong `self`,
non-string payloads, 768/769-byte payloads, closed connections, GC, real datagrams of
0/4/768/769/2000 bytes in both directions, foreign senders, ICMP reset after the peer
closes, and an unresolvable ABI (LuaJIT loaded under another module name).

**Result: 17/17 scenarios identical.** The first run found one real difference, fixed
in the port: see finding 7 (`INET_ADDRSTRLEN`).

`parity/lua_e2e.py` runs the extension's own `transport.lua` + `client.lua` (two
synthetic clients) through each DLL against each relay process. **4/4 combinations
pass** (C or Rust DLL x Python or Rust relay): both clients receive the other's pose
and keep their lease.

### Relay: Python vs Rust, deterministic replay

`parity/relay_parity.py` feeds identical command streams (new session, datagram at
time *t* from address *a*, snapshot tick, expire) into `parity/py_driver.py` (the
unmodified Python classes) and `x4lan-replay`. Leases come from a counter in both, so
every reply and every snapshot packet must be **byte-identical and in the same order**.

| Scenario source | Scenarios | Commands | Packets compared | Identical | Different |
| --- | ---: | ---: | ---: | ---: | ---: |
| Sessions recorded from the existing Python test suite (`record_tests.py`) | 134 (+11 not replayable*) | 13,994 | 9,321 | 134 | 0 |
| Hand-written protocol scenarios (`scenarios.handwritten`) | 63 | 1,798 | 2,959 | 63 | 0 |
| Randomized, seed 7541 (`--generated 2000`) | 2,000 | 641,859 | 577,138 | 2,000 | 0 |
| Randomized, seeds 1-5 (1,000 each) | 5,000 | 1,607,592 | 1,457,460 | 5,000 | 0 |
| CPython `float()` / `.9g` / `%.17g` / `.3f` cases | 21 | 10,059 | - | 21 | 0 |

\* A recorded trace is compared only if a fresh Python session reproduces the
recorded test results from packets alone; 11 tests poke session state directly
(e.g. assign `s.world`), so their traces cannot be replayed.

What the scenarios cover: every inbound packet kind at every session level (base,
bubble, economy, coop, dock, universe); handshake, `PING`, `RESUME`/`RESET`, retired
nonces, one endpoint claiming two slots, sector pinning; wrong token/lease/address,
non-ASCII bytes, wrong version, missing/extra fields, 768/769-byte datagrams; CPython
number grammar in every float field (`" 1"`, `1_0`, `inf`, `1e309`, `nan`, `-0`, ...);
sequence reordering, duplicates and 2**31 bounds; lease expiry at exactly 30.000 s,
stale-pose (0.75 s), periodic publishers (0.1/0.2/0.25/0.5 s) and staging timeouts
(15 s); plus cooperative multi-packet flows (baseline delivery with selective ACKs,
health generations, cargo transactions, NPC claims, projectile births/ends). Every
relay output packet kind (48) is produced by `--generated 2000` (`parity/coverage.py`).

**Harness sanity check:** four deliberate one-line mutations of the Rust relay
(lease 30 s -> 29 s, health staleness 0.75 -> 0.8 s, projectile repeats 3 -> 2,
WALK context bound 4 -> 5) were each detected by the recorded-trace run alone
(5, 1, 5 and 1 differing scenarios).

### Relay: live processes over UDP

`parity/live_parity.py` starts `python -m x4lan.host` and `x4lan-relay` with the
same arguments and runs identical scripted traffic from two UDP clients. Leases are
normalised; direct replies must match in order; the snapshot stream is compared as the
set of distinct packets in a quiet window after the script settles.

| Mode (CLI / session.json) | Replies | Settled snapshot set | Survives one 1000-byte datagram |
| --- | --- | --- | --- |
| base | identical (11) | identical (4) | Python **no**, Rust yes |
| `--bubble-actions` | identical (19) | identical (6) | Python **no**, Rust yes |
| `--trading` | identical (25) | identical (8) | Python **no**, Rust yes |
| session.json `coop` | identical (34) | identical (17) | Python **no**, Rust yes |
| session.json `world_sync` | identical (38) | identical (13) | Python **no**, Rust yes |

The datagram result is finding 1 below; it is the one intentional behavioural
difference.

### Cargo tests

`cargo test`: 24 tests (16 unit, 8 integration in `x4lan-relay/tests/session.rs`,
ports of `tests/test_host.py` plus co-op handoff and real UDP). `cargo clippy
--all-targets`: no warnings.

## Measurements (Windows 11 x64, this PC)

| | Python | Rust |
| --- | --- | --- |
| Relay code (non-blank, non-comment lines) | 1,795 (13 modules, ported scope) | 4,574 (3,512 session layers, 233 wire types, 107 UDP loop, 192 CLI, 436 support: `float()`/format emulation, ordered map, JSON, RNG; 87 replay driver) |
| Native module code | 157 (`net.c`) | 247 |
| Deliverable size | Windows embeddable CPython 3.14.8: 12.6 MB zip / 24.4 MB extracted (37 files); Linux standalone 3.14.8: 36.3 MB tar.gz / 101.9 MB extracted (4,569 files); plus the x4lan sources | `x4lan-relay.exe` 650 KiB, one file, no runtime |
| Native module size | 169 KiB (`x4lan_net_64.dll`, zig) | 117 KiB |
| Start to first PONG (median of 10) | 105 ms (local Python 3.12) | 14 ms |
| Idle working set / private bytes | 19.3 / 11.7 MiB | 4.8 / 0.8 MiB |
| Replay of 641,859 commands (harness-dominated) | 6.9 s | 4.3 s |

The Python LOC is dense (many statements per line, one-letter names); the Rust count
is `rustfmt` output at 120 columns and includes the explicit types/enums that are the
point of the port. Runtime sizes are from the archives pinned in
`tools/distribution/public/runtimes.json` (SHA-256 verified). The Linux Rust binary was not
built. Replay timings include JSON/hex encoding on both sides and are not a relay
benchmark.

## Findings in the Python/C implementation

Found while porting or by the harnesses. File:line refer to 8a56415.

1. **Remote crash of the relay on Windows hosts (unauthenticated).**
   `src/x4lan/host.py:208` calls `recvfrom(MAX_PACKET + 1)` and catches only
   `BlockingIOError` and `ConnectionResetError`. On Windows a datagram longer than 769
   bytes raises `OSError [WinError 10040]` (WSAEMSGSIZE) instead of being truncated,
   which escapes `Relay.step()` and ends the process. The packaged server loop has the
   same path (`src/x4lan/monitor.py:390`, only `KeyboardInterrupt` is caught). Reproduced:
   one 1000-byte UDP packet from any address stops `python -m x4lan.host` (all five
   modes in `live_parity.py`). Linux truncates and drops the packet, so Linux hosts
   are unaffected. The Rust relay counts and skips any other receive error.
2. **Malformed universe packets get acknowledged and pin the world epoch.**
   In `src/x4lan/universe.py:294-297`, a host packet with a `UPOS`/`UMOVE` prefix but the
   wrong field count (or a `UACK`/`UINTEREST` sent by p1) falls through to
   `Receiver.receive`, which replies `UACK ... |-1` (or `count+1` when the revision
   matches the latest baseline) and sets `universe_epoch`. Verified:
   `UPOS|1|L1|L1|5` -> `UACK|1|L1|L1|5|-1` and `universe_epoch` set. Same host lease,
   so not exploitable by the guest, but the epoch fence is set by a packet that is not
   a baseline. Reproduced faithfully in Rust; worth a protocol decision.
3. **ACTION is authenticated by token and source IP only**
   (`src/x4lan/bubble.py:63,65`): no lease, and the port is ignored. Any process on a
   player's IP that knows the session token can submit or replay-query commands for
   that slot (relevant behind NAT/ZeroTier gateways). Every other channel binds lease
   and full address.
4. **The wire grammar for numbers is CPython's `float()`.** Pose fields accept
   `" 1"`, `"1_000"`, `"infinity"`, `"1e309"` (= inf, then rejected) etc.
   (`src/x4lan/host.py:155` and every `float(...)` in the session modules); the projectile
   codec alone uses a strict regex (`projectile_protocol.py:28`). Integers use three
   grammars: `sequence()` (ASCII digits), `integer()` (`-?[0-9]+`), and plain `int()`
   in `missions.py:81` (accepts `" +1"`, `"1_0"`). Whitespace is defined twice:
   `float()` strips ASCII `\t\n\v\f\r `, while `universe.py:35` rejects records with
   `str.isspace()` characters, which also include `\x1c-\x1f`. Any second
   implementation (Lua, Rust) has to emulate CPython to stay compatible; a documented
   grammar would remove this coupling.
5. **Output order is defined by dict insertion order.** Which pending
   command/order/cargo request is executed first, and the order of forwarded NAV/FIRE2/
   CDOCK frames, follow Python dict semantics (assignment keeps position, pop+insert
   moves to the end, e.g. `coop.py:161` vs `:182`). Clients appear tolerant, but it is
   implicit protocol behaviour; the Rust port needed an insertion-ordered map to match.
6. **Side effects before validation.** `economy.py:97` resets the snapshot assembly
   when the ECON sender's lease differs *before* the packet's fields are parsed, so a
   malformed ECON from a new host lease discards staged parts; `missions.py:70` resets
   the journal on any `MSTATE`/`MREQ`-prefixed packet when the host lease changed,
   before authentication. Both are harmless in practice and reproduced as-is.
7. **`net.c` host-length limit is platform-dependent** (`src/native/net.c:120`):
   `INET_ADDRSTRLEN` is 22 in Windows `ws2tcpip.h` and 16 on Linux, so a 16-21
   character host string reaches `inet_pton` on Windows but not on Linux (same error
   class either way). Found by `native_parity.py` on the first run; the Rust module
   uses the per-platform value to stay identical.
8. **Latent crash path in projectile forwarding** (`projectile_relay.py:133`):
   `pp.serialize()` raises `ProtocolError` if a forward exceeds 768 bytes, inside
   `snapshots()` with no handler. With the codec's bounds the longest forward is about
   480 bytes, so it is unreachable today; a future field or bound change would turn it
   into a relay crash.
9. Minor: every received packet runs the full `expire()` chain two or three times
   along the `super()` calls (e.g. `bubble.py:50` then `host.py:75`); it is
   idempotent, so the Rust port runs it once.

## Not ported / not exercised

Not ported:

* `src/x4lan/monitor.py` (status screen, `logs/server-status.json`, JSONL session log,
  `probe`/`attach`, process CPU/RSS sampling). The packaged launchers
  (`tools/distribution/session_launcher.py:311`, `tools/distribution/linux_setup.py:186`) start the
  relay through `monitor.run()`, so the Rust binary is a drop-in for
  `python -m x4lan.host` but **not yet for the packaged server**. A Rust monitor would
  need the same status JSON contract.
* `projectile_protocol.parse(..., 'forwarded')` and `serialize` of the client
  direction (used by clients/tests, never by the relay).
* Non-relay modules: `seed.py`, `world_start.py`, `save_origin.py`,
  `station_catalog.py`, `test_peer.py`, `world_launcher.py` (TCP 45844 transfer),
  packaging (`build_packages.py`), `prepare.py`.

Not exercised:

* **X4 itself** (no game launched; synthetic peers only; no two-native-game test).
* Linux: neither crate was built or run on Linux; `tests/test_lua.py` (which loads
  `x4lan_net.so` via lupa) needs Linux x86_64 and was not run. The Linux code paths
  (`dlsym`/`libdl`, `/dev/urandom`) compile only under `cfg(not(windows))` and are
  untested.
* PUC Lua 5.1 (`longjmp` error path): only LuaJIT 2.1 was used.
* Real networks/NAT, packet loss and latency beyond localhost; long-running endurance.
* The Python test suite ran on Windows for trace recording: 636 tests, 35 failing
  (all launcher/symlink/Linux-shell tests, unrelated to the relay); its Lua tests that
  need `lupa.luajit21` ran, `test_lua.py`'s two native-module tests did not.

## Reproduce

```sh
cd src/rust
cargo test && cargo clippy --all-targets
cargo build --release
cd ../..
python src/rust/parity/build_luajit.py                       # LuaJIT 2.1 -> src/rust/target/luajit/lua51_64.dll
python src/rust/parity/native_parity.py --lua src/rust/target/luajit/lua51_64.dll \
    --module c=src/extension/x4_lan_lab/native/x4lan_net_64.dll --module rust=src/rust/target/release/x4lan_net.dll
python src/rust/parity/lua_e2e.py --lua src/rust/target/luajit/lua51_64.dll \
    --module c=src/extension/x4_lan_lab/native/x4lan_net_64.dll --module rust=src/rust/target/release/x4lan_net.dll \
    --relay python --relay rust=src/rust/target/release/x4lan-relay.exe
python src/rust/parity/record_tests.py --out src/rust/target/parity/test-traces.jsonl
python src/rust/parity/relay_parity.py --rust src/rust/target/release/x4lan-replay.exe \
    --traces src/rust/target/parity/test-traces.jsonl --generated 2000 --report src/rust/target/parity/report.json
python src/rust/parity/live_parity.py --rust src/rust/target/release/x4lan-relay.exe
```

`build_luajit.py` needs MSVC Build Tools and the `luajit-src` crate in the cargo
registry. All outputs go to `src/rust/target/` (ignored).

## Remaining work

1. Decide finding 1 upstream (catch `OSError` in `Relay.step`) independently of Rust.
2. Linux build + `tests/test_lua.py` against `libx4lan_net.so` (WSL), and a Linux run
   of all parity harnesses.
3. A Rust `monitor` (status JSON, session log) so the packaged launchers can start the
   Rust relay; then package both binaries (`build_packages.py`) behind a switch.
4. Native X4 tests per `AGENTS.md` (profile 45841, disposable world), first with the
   Rust DLL only, then the Rust relay.
5. Settle the protocol questions in findings 2-6 and write the wire grammar down, so
   the Rust types can become the specification rather than an emulation.
