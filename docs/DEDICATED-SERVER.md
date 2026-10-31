# Linux dedicated-server prototype

Status: internal development. This is an unattended **full X4 process** supervised by
Rust, not a renderer-free or standalone universe simulator. Up to seven human clients
connect to one Linux server. Ordinary authority replication does not require engine hooks.

The optional build-pinned Linux engine agent controls fixed simulation steps and
render cadence. It does not implement the experimental native command/hash barrier.
The supervisor refuses the reserved `lockstep1-` session mode until that engine
capability exists; a matching build label or `LSCAP` protocol reply is insufficient.
Legacy authority sessions continue to use their existing admission rules.
See [the compatibility audit](../reports/LINUX-SERVER-COMPATIBILITY-20261027.txt)
for the tested source versions, shared wire/RNG checks and remaining native port work.

Terminology: the dedicated X4 is the **server**; humans connect as **clients**
(player 1 ... N, all equal). Older identifiers keep their names for compatibility:
package roles `host`/`guest`/`guestK`, Windows folders `-GuestK`, wire slot `p1` for the
server and MD/log strings such as `SetupHost`/`SetupGuest`.

## Players, slots and ships

- A dedicated session declares `max_players` N (2..8; session.json and package.json).
  Slots are `p1..pN`. `p1` is the server; its internal player stays in the observer ship
  and never consumes a shared ship.
- Shared ships: `max(2, N - 1)`, one per client, entity ids 1.. on the wire. Default
  local ships: player 2 -> ship 2, player 3 -> ship 1 (as before), player K -> ship K-1.
  Ship 2 starts with player 2, every other ship with the server until its client takes
  the controls through the normal authenticated CREW handoff.
- Every client's motion, fire, body and authority reaches every other client and the
  server; server state reaches every client. Each client has independent sector
  interest and acknowledged baseline delivery.
- A session without `max_players` is the legacy three-slot session (players 2 and 3).
  Non-dedicated (Python relay) sessions stay at two players; the Python relay refuses
  dedicated or N-player sessions.

### Motion smoothing (7.6.0 proxy-motion / npc-flicker)

The Rust relay carries the 7.6.0 motion work for every client: `NMODE` (normal, boost,
travel, highway) is accepted from each ship's movement authority and forwarded to every
other peer ahead of NAV2; clients keep one motion track, render clock and mode per remote
ship and drive fast replicas by pose only. Universe motion is forwarded fresh-first per
client (unsent samples oldest first, then round robin). `RTTPING` is answered with
`RTTPONG` by the relay loop; the relay-side NETRTT statistics of the Python relay are
not ported.

### Protocol version

Every new build label carries the protocol tag `np1`; dedicated labels also name the
player count, e.g. `world1-7.5.0-np1-p4-9.00-steam23660954`. The launcher refuses an
invitation whose label or player count does not match, the supervisor refuses a
session whose label lacks `-np1-p<N>-` or whose authority config lacks
`max_players=N`, and the relay never admits a HELLO with another build label. A
package for a player number above `max_players` refuses to configure.

### N-player limits (fail closed)

Ships 1/2 are the fixture ships. These subsystems stay on ships 1/2 in every session
size; the relay rejects ships 3.. explicitly (no reply, or `REJECTED` for orders):

| Subsystem | Ships 3.. |
| --- | --- |
| WORLD rows (hull/shield/cargo/dock) | separate server-owned `XSHIP` rows, 5 Hz |
| Pilot-reported health (HEALTH/HACK/HSTALE/HBASE), HGEN | not offered; health is server-owned via `XSHIP` |
| Shot claims (SHOT), NPC hit claims (NCLAIM) | rejected; no hit credit from ships 3.. |
| Projectile visuals (BOLT/BEND), harmless shot visuals | rejected; remote fire of ships 3.. is therefore **not drawn** in packaged sessions (`projectile_visuals`/`shot_visual` on), logged once as `FIRE_VISUAL_LIMIT`. FIRE2 is still relayed and replayed natively only when both visual modes are off (dev configs) |
| Native cargo drop/pickup (CARGOREQ), market orders (COOPORDER) | rejected (`REJECTED`) |
| Legacy bubble actions (fixture drops, station transfer) | rejected |
| Dock attachment (CDOCK) | rejected; the remote ship follows its NAV2 pose only |
| On-foot avatars (WALK2) | every client, keyed by slot; contexts are shared ships 1..N and stations of the shared save ([WALKING.md](WALKING.md)) |
| Remote spacesuit kill (SUITKILL) | replicas for every client; the server reports a kill only for the most recent remote suit and only when its generation is unique |

### Bandwidth

Per client and 20 Hz tick the relay sends AUTH, HGEN, WORLD, one NAV2 and one FIRE2
per remote ship, one BODY2 per other player, a WALK2 per other player on foot
(only on a new pose or every 0.2 s), and the existing bounded universe
traffic (up to 32 new motion records plus 2 repeats, a 12-packet baseline window, NHP and
IDENT only when the server issued a new sequence). Clients that send `XBCAP` receive each
tick's packets packed into XB datagrams of at most 768 bytes. XSHIP adds
`N - 3` rows per client at 5 Hz. Player traffic therefore grows linearly with N
(about `3 * (N - 1)` extra packets per tick per client); the universe budgets are per
client and unchanged.

## Build

Requirements: Linux x86-64, Rust 1.82+, Cargo, Python 3.10+, and the pinned X4 9.00
Linux installation (Steam build 23660954). Build an internal package set:

```sh
python3 tools/distribution/build_packages.py /path/to/new-dedicated-set --dedicated     --max-players 4 --runtime-cache /path/to/internal-runtime-cache
```

`--max-players` (default 3) counts the server slot. Every client slot gets a Windows
and a Linux package with the same role and profile; use either one for that player.
4 builds the Linux server package `X4-LAN-*-Linux` plus:

| Player | Role | Profile | Windows package | Linux package |
| --- | --- | --- | --- | --- |
| 2 | `guest` | 45841 | `X4-LAN-*-Windows11.zip` | `X4-LAN-*-Linux-Client.tar.xz` |
| 3 | `guest2` | 45842 | `X4-LAN-*-Windows11-Guest2.zip` | `X4-LAN-*-Linux-Client2.tar.xz` |
| 4 | `guest3` | 45843 | `X4-LAN-*-Windows11-Guest3.zip` | `X4-LAN-*-Linux-Client3.tar.xz` |

All packages of one build share one `pair_id` and the same mod scripts; never mix
packages from different builds. `PACKAGES-SHA256.txt` lists every archive.

Build into a Linux file system: the bundled runtime's terminfo tree does not fit a case-insensitive `/mnt/c` target.
`--runtime-cache` is required by the CLI. The dedicated build compiles
`x4lan-dedicated` and the Rust `x4lan-net` module once; the server and every Linux
client package load that `x4lan_net.so`. Windows clients keep `x4lan_net_64.dll`. It does
not use new C code.

01-INSTALL configures the server package as the server and every client package as a
client (the launcher's `host`/`guest` roles) without asking; a client package refuses
the server role. A Linux client needs native Linux X4 9.00 (build 23660954) and installs
into `~/.config/EgoSoft/X4/<profile>` (or `$XDG_CONFIG_HOME/EgoSoft/X4/<profile>`;
Flatpak Steam: `~/.var/app/com.valvesoftware.Steam/.config/EgoSoft/X4/<profile>`).
Send the same private `JOIN-SESSION.json` to every client. Do not publish session files or configured Lua.

### Several clients on one test PC

Each client package uses its own isolated profile (`test_profile` in package.json:
45841 for player 2, 45839+K for player K). Install each package into its own X4
installation or sandbox; the profile id keeps saves and config apart, but a worktree
or profile does not isolate the Steam client, the X4 installation folder or the
extension folder. Profiles 45841..45848 are recognized as test profiles and are never
offered as settings sources.

## Steam client requirement

The pinned build is the Steam build. Without a running, logged-in Linux Steam client
for the same user it either tries to start `~/.steam/root/steam.sh` and exits 1, or
(with `SteamAppId` set) blocks at a modal "X4 - Startup error" dialog: "Steam client is
required, please start the Steam client and try again." `run` therefore fails closed
when `$HOME/.steam/steam.pid` does not name a live process. A Steam login with an
account that owns X4 is an operator step; the supervisor never automates it.
`tools/dev/linux_x4_start_probe.sh` reproduces one bounded start under Xvfb + lavapipe with
an isolated HOME and reports this dialog.

## WSL2 / WSLg smoke test

WSLg must provide a working X11 display (`DISPLAY=:0` and `/tmp/.X11-unix/X0`). Xvfb
is not required by this setup, but a display and working graphics driver are still
required. `-disableui` is unsupported because it prevents the multiplayer Lua from
loading. No claim is made that WSLg removes GPU work.

Use only the downloaded test installation and a fresh disposable profile 45841 with
autosave disabled and Protected UI off. Prepare the shared host world and install the
dedicated authority configuration through the normal internal launcher. Preserve
normal profiles, saves, Workshop selections, and existing sealed packages.

Start through `03-START-SERVER-HOST.sh --console`, or generate the same configuration
and invoke the Rust binary directly:

```sh
./x4lan-dedicated --config .local/dedicated/config.json run
./x4lan-dedicated --config .local/dedicated/config.json status
./x4lan-dedicated --config .local/dedicated/config.json pause
./x4lan-dedicated --config .local/dedicated/config.json resume
./x4lan-dedicated --config .local/dedicated/config.json stop
```

`run` verifies the pinned executable, prepared save, installed scripts and dedicated
Lua configuration before starting X4. Readiness requires advancing native game time
and an admitted authority peer, not just a live PID or listening UDP socket.
`stop` requests graceful `QuitGame()` through the local control file. After a crash,
the supervisor exits failed and never starts a replacement world automatically.

## Validation boundary

Rust/Python/LuaJIT tests and synthetic 3-/4-player peers do not establish native
multi-client play. A WSL smoke test with one native authority and synthetic guests is a separate,
limited result. Two actual clients, multi-sector combat/attention behavior, long
sessions, and persistent-world lifecycle require their own acceptance tests.

## Native N-player check (pending)

No native N-player run has been made. Synthetic evidence: `src/rust/x4lan-relay/tests/nplayer.rs`
and `tests/test_nplayer_lua.py`. A first native check with a server plus three clients:

1. Build a fresh set with `--max-players 4`; set up the server (Linux package, profile
   45841) and three client packages (profiles 45841, 45842, 45843) from fresh folders.
2. Start the server; check `status` shows the server peer admitted.
3. Start clients one at a time. In each client log expect `shared ships ready: ... count=3`,
   `BUBBLE_READY ... ships=3 local=<own ship>` (player 2 -> 2, player 3 -> 1, player 4 -> 3),
   and `ship <n> movement authority=p<k>` for every ship.
4. Fly every client: each client must see the two other clients' ships move, fire
   (FIRE2) and change sector; the server log shows all three NAV2 sources.
5. Disconnect one client for more than 30 s, reconnect it (resume), and check that the
   other two never stopped seeing each other.
6. Damage ship 3 on the server side and check the hull/shield mirror on the other
   clients (XSHIP); confirm cargo/market/dock-attachment actions from ship 3 are refused
   with a notice, not silently half-applied.

## Relay listen address

The relay listens on `127.0.0.1` by default. On a server whose firewall admits the
session UDP port (default 45841), set `X4LAN_RELAY_BIND=0.0.0.0` (or a specific
local IP) in the supervisor's environment so guests on other machines can reach it.
Only literal IP addresses are accepted; anything else refuses to start.
