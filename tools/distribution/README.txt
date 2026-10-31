X4 Multiplayer {{VERSION}} — INTERNAL TEST
25 October 2026 | Windows/Linux host or guest | Two players per session
X4 9.00, Steam build 23660954 | Matching official DLC including Timelines

Both packages include the server and a private Python runtime. No Python install
or download is needed. Open 00-START-HERE.html for detailed instructions.
Extract Linux's .tar.xz (or equivalent Small.7z) or Windows' .zip completely before running any action.

WHAT CHANGED IN 7.6.0 (see CHANGELOG.txt; 7.5.0 notes follow)
- Enable host-authoritative NPC shield/hull damage and death for free-flying S/M
  targets, including when the guest is in a different sector from the host.
- Enable shared native matching-ship kill contracts: either role can accept,
  qualifying faction kills count, the shared objective resolves and host pays once.
- Include reviewed Windows installer fixes: localized process output, missing
  Steam drives, manual X4.exe selection, profile reselection and owned recovery.
- Retain the 7.4.1 Linux terminal starters, player damage/projectile work, saved
  dock handling, completed-story preset and personal teleportation research.

NPC scope: uniform, unmodified stock Shuttle plasma or generic Pulse Mk1/Mk2;
free-flying S/M main hull/shields. No L/XL surfaces, beams, missiles, turrets,
mixed/modified weapons, captured-fleet orders or synchronized NPC AI claims.
Mission scope: standalone visible native GM_Destroy_RarelyOnSight offers without
inventory rewards. Guest copies use [Shared]. Other ordinary missions and plots
are not shared; host reputation is not mirrored in the guest UI. Host generates
offers normally, so an eligible offer is not guaranteed immediately.
Local Linux engine tests used one real game and a synthetic peer. The combined
Windows/Linux human pair still needs HUMAN-NPC-MISSION-TEST.txt / NEXT-TEST.txt.
These are new candidate features, not completed full-universe multiplayer.

One combined startup check refused the second ship's weapon loadout; later runs
did not reproduce it. If damage is unavailable after connection, collect both
diagnostics before restarting. The refusal log now includes a specific reason.

Normal profiles stay read-only. Personal resolution, quality, sound and bindings
are imported into the isolated test profile, which uses borderless mode.
Existing radar, ordinary cargo, movement, station/asteroid alignment, Alt+Tab and
Cooperative Trial human results remain valid unless a new regression appears.
A shared starting save does not make ongoing station economy or all NPC decisions
host-authoritative. Saving/resuming a played multiplayer campaign is not ready.

HOST
1. Close X4 and the old server. Remove the previous mod using its ORIGINAL
   package's REMOVE-MOD action (09 in 7.3.1–7.4.1; 5 in 7.3.0). Extract into a new folder.
2. Run 01-INSTALL, choose H, enter your reachable LAN/ZeroTier IPv4 address.
   Choose your normal settings profile if asked.
3. Run 02-PREPARE-WORLD-HOST once. X4 opens, creates the shared world, saves and closes
   automatically. Wait for "Shared world ready" in the launcher. Do not play or
   load a different save during preparation.
4. Send JOIN-SESSION.json privately to your guest. Run 03-START-SERVER-HOST and keep
   its console open. The host runs BOTH this server and X4.
5. Run 05-START-X4. The saved world loads automatically; wait for your Shuttle.

GUEST
1. Remove the previous mod using its original package and extract the complete
   new archive. Receive the host's JOIN-SESSION.json and keep it in this folder.
2. Run 01-INSTALL, choose G, select that invitation in the file dialog if it is not already beside the starter.
3. Run 04-TEST-CONNECTION, then 05-START-X4. The launcher downloads and checks
   the shared world, then X4 loads it automatically. Do not choose another save.

DEDICATED-SERVER SETS (only when DEDICATED-SERVER.md is in this folder)
01-INSTALL does not ask for a role. The Linux package is the server; every client
package (Windows11[-GuestK] or Linux-Client[K]) is a client and follows GUEST above.
Each client slot has a Windows and a Linux package; use the one for your OS.

The numbers identify actions; guests skip HOST-only actions 02 and 03.
Windows: double-click the numbered .cmd files. Linux: execute the matching .sh
files; they open a terminal automatically. If your file manager opens an editor
instead, use its Run/Execute option or run bash 01-INSTALL.sh in a terminal. Allow UDP 45841 and TCP 45844 to the
host on your test network. A successful UDP check alone does not test the save
transfer. No router forwarding is needed with a working ZeroTier connection.

BACKGROUND / TELEPORTATION
Use Alt+Tab to another application while X4 stays open in borderless mode. Keep
the server console running. Do not explicitly pause X4 or minimize it with the
Windows desktop button; true minimization can suspend native rendering/UI ticks.
The launcher includes -nocputhrottle. Alt+Tab passed the previous host/guest human test. Personal teleportation is unlocked after Shuttle setup;
use the normal map/context menu and normal destination/ownership requirements.
The technical story preset is fixed for this release; there is no choice menu yet.

RESTART / STOP
Every retry requires BOTH games AND the relay to stop first. Restart the relay
and both games. They reload the same STARTING world; previous gameplay progress
is not resumed. To create a different world/session, use a fresh extracted pair
and a new invitation. 02-PREPARE-WORLD-HOST never regenerates an existing world.

ONLY the reserved, recorded save_010 belongs to the launcher. Other or changed
saves are preserved and cause a refusal. Never save a normal campaign in profile 45841.
09-REMOVE-MOD removes its byte-matching reserved copy and archives the owned mod;
the package keeps the immutable world bundle. Close X4 and the relay first.

The setup does not change Workshop selections or subscribe/unsubscribe to mods.
The withdrawn 7.2 Fix-1 helper is not included. A separate profile does not by
itself isolate the Steam account. If content differs, stop and compare the two
setups; no automatic mod-selection rewrite is performed.

NEXT-TEST.txt describes the focused NPC/mission and affected regression tests. Run 06-DIAGNOSTICS on BOTH PCs before
restarting after a failure. Keep logs, role/OS, object code and approximate time.
Do not share JOIN-SESSION.json, session.json or ui/config.lua publicly.
This remains an INTERNAL test, with two simultaneous players per session.
