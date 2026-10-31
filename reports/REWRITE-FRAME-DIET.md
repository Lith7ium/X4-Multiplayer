# Server frame diet (dedicated server mode)

Owner: frame-diet agent, branch `contrib/ferrite-rw-frame-diet` (base `contrib/ferrite-rewrite-base` e63d9cf).
Build: X4 9.00, Steam build 23660954 (Windows RVAs; Linux VAs in bin-linux/X4 where given).
Status 26 Oct 2026: module, audit mode and tests done; native functional audits done on a lane (audit-001:
presentation regions; audit-003: rng_split A/B and ui_elements). Native stability/bisect later proved
`ui_elements` skipping stops UI Lua; diet now refuses it, and watchdog recovery is a failure (section 11).
Baseline timing completed in a lead-controlled quiet window (section 14); the eight-sector measurement
remains pending (rewrite-coord Q9). Terminology: the dedicated X4 is the **server**,
players connect as **clients**.

## Summary

- A dedicated server spends a large share of every MoveWorker frame on presentation. In the 8-sector attention
  run (25 Oct, simprof folds) slot 6 (UI) costs 5.3 ms of a 17.3 ms frame at baseline and 6.2 of 29.7 ms under
  load, but the Lua part of it (all UI Lua including the mod's networking) is only 0.6 / 0.8 ms. Slot 10 (render
  preparation) costs 2.3 / 2.6 ms, of which the camera render is 2.1 / 2.3 ms.
- `native/engine/framediet.c` (`x4lan_framediet_64.dll`, opt-in) skips presentation work through data-only,
  build-pinned, fail-closed hooks while the simulation, MD, AI, input and every UI Lua script run unchanged.
- **Behaviour-identical set (`server`)**: the Anark scene layer walk (`ui_scenes`, ~1.9 / 2.0 ms) plus the
  engine's own `disable_rendering` flag for the main-thread draw (`draw`). The native audit found no simulation
  side effect of the layer walk: 0 thread-RNG steps, 0 UniverseIDs, 0 changed component words in 1.36 M
  snapshots over 40,000 calls.
- **Not behaviour-identical**: the camera render (`camera`, ~2.1 / 2.3 ms). It draws about 150 MoveWorker
  thread-RNG steps per frame, allocates UniverseIDs for mass-traffic welding effects and sets a view-cache flag on
  every rendered component. Skipping it changes the RNG stream and later IDs relative to a client that renders.
  The camera view is therefore an input of the simulation RNG stream on every X4 (a parity leak). The
  `rng_split` site moves slot 10's draws to a private stream (proposal of the determinism owner); natively all
  ~150 draws per frame moved and the simulation stream stayed continuous (audit-003). IDs are not covered.
- `ui_elements` (**audit/pass only**, the Anark element update): the earlier 0 RNG/ID/component-write audit
  did not establish UI liveness. Native bisect-001 proved skipping it stops UI Lua, including networking.
  Both start and set refuse it in diet mode, regardless of presentation period.
- Expected saving of the behaviour-identical set: ~1.9-2.0 ms per frame on the MoveWorker (11 % of the
  baseline frame, 7 % under load) plus the whole main-thread draw; `camera` and `ui_render` are another
  ~2.3-2.6 ms of candidates. The ~1.9-2.0 ms element update must run.
- **Measured** (quiet window, baseline world, `server` with the RNG split on in both modes, section 14): MoveWorker
  slot loop (slots 1-14) 17.38 -> 13.25, 14.38 -> 12.49 and 15.20 -> 12.67 ms in three adjacent 30 s pass/diet
  pairs (median -2.52 ms, 16.6 %; pooled -2.76 ms, 17.7 %), main-thread CPU 12.4-12.9 % -> 5.4-5.7 % of one core.
  Under the 8-sector attention load (section 16): 26.4-28.5 -> 23.5-27.0 ms, all 10 adjacent pairs lower, median
  -2.11 ms (7.5 %); the absolute saving holds, the relative one shrinks as predicted.

## 1. Attribution (where the server's frame goes)

Source: `X4-Multiplayer-fixed-tick/.local/evidence-238-withheld`,
per-window `SIMPROF fold` lines; baseline = windows ending 40-80 s, load = 110-260 s; ms per MoveWorker frame =
samples (1 kHz) / frames (FRAMEPROBE simFrames). Reproduce: `python native/engine/framediet_analyze.py
attribution <log> --window 35:81 --window 105:261`.

| work | baseline ms (share) | load ms (share) | diet |
|---|---|---|---|
| frame (wall per MoveWorker frame) | 17.26 | 29.69 | |
| slot 6 total, UI::CUIController::Move 0xb3bad0 | 5.27 (30.5 %) | 6.21 (20.9 %) | |
| - Lua: every stack with a lua51_64 frame (game UI + mod) | 0.59 (3.4 %) | 0.82 (2.7 %) | keep |
| - Anark gameface element update 0x1399c40 | 1.85 (10.7 %) | 1.95 (6.6 %) | keep (skipping stops UI Lua) |
| - Anark scene layer walk (SceneManager slot 3 0xaf8e50 -> AnarkScene slot 12 0xaf8760) | 1.87 (10.8 %) | 2.01 (6.8 %) | `ui_scenes` |
| - ICom message pump 0xae9880 (VisibilityData::Update 0xadcfa0) | 0.75 (4.3 %) | 1.20 (4.1 %) | keep |
| slot 10 total, X4::GameRender::Move 0x121c1f0 | 2.30 (13.3 %) | 2.58 (8.7 %) | |
| - camera render, U::Camera slot 3 0x983160 via GameClient render bank 0x122bfc0 | 2.10 (12.2 %) | 2.34 (7.9 %) | `camera` (opt-in) |
| - UI render bank, CUIController::Render 0xb3bba0 | 0.19 (1.1 %) | 0.23 (0.8 %) | `ui_render` (opt-in) |
| Component::BuildCache 0x3d4a80 | 3.85 (22.3 %) | 3.68 (12.4 %) | slot 3, not presentation (transforms) |

BuildCache sits entirely under `U::UniverseFrameUpdate` in slot 3 (not on the camera path); it fills the view
caches that pathing jobs and MassTraffic read, so it is simulation work (owner: transforms). Slot 8 (character
animation) and slot 9 (sound) are 0.4 ms each under load.

## 2. What the simulation and the mod need from slots 6 and 10

Slot 6 is `CUIController::Move` (0xb3bad0, CS +0x70 for the whole call): ticker toggles, the ICom message
pump 0xae9880, `CXAnarkController::Move` 0xb36300, then input-context updates on `g_InputSystem`.
`CXAnarkController::Move` (gated on +0x3f0, up to 3 passes while +0x3ee): gameface slot 12, the presentation
loop with gameface slot 13 (0x13a5f90 -> 0x138f2c0), anchored-element/camera sync, gameface slot 14 and the Lua
event `updateScenesDone`. Inside 0x138f2c0 (vtable 0x2ca2988): time update 0x1399550, clearing of dirty lists,
0x138fa90 (contains script event dispatch, 0x13b2aa0 -> Lua), the **element update** 0x1399c40 (only when the root's
subtree-dirty flag 0x2000 at +0x2c is set), and the **Lua per-frame dispatch** `(+0x38)->vfunc5` =
`AnarkLuaEngine::Update` 0xaf85e0 -> `DispatchFrameScripts` 0x13a1270 -> `onUpdate` (the mod's drivers run from
`widget_fullscreen.lua`'s onUpdate via `CallUpdateScripts`). The scene manager path (slot 3 0xaf8e50 -> per
scene slot 5 0x13aa510 -> slot 12 0xaf8760) contains no Lua at all in the profile.

Therefore slot 6 can never be skipped wholesale, nor CXAnarkController::Move, gameface slot 13 or 0x138f2c0.
What the mod's Lua uses (inventory of `extension/x4_lan_lab/ui/*.lua`): component-state FFI (positions,
contexts, operational, IDs), `SetObjectSectorPos`, `SpawnObjectAtPos2`, `RemoveComponent`, `RegisterEvent` /
`AddUITriggeredEvent` (MD <-> UI events), `GetComponentData`, time, save list, `SetObjectForcedRadarVisible`.
None of it reads Anark scene, element, render-list, holomap or camera state.

Slot 10 is `GameRender::Move` 0x121c1f0: `Render` (vfunc2) of the four render-bank entries (VbiTable
+0x110..+0x128: UI, character animation, GameClient), then a release of `*(0x6adfc30)` (graphics config object).
The GameClient bank 0x122bfc0 renders every camera of the universe camera list (Universe+0x2c0; the engine
itself skips that loop while its render pause GameClient+0x2a8 is armed) and then `(+0x2a0)->vfunc3` when that
object's +0x70 == 1. Camera::Render 0x983160 -> `U::Scene::Render` (vfunc915, 0x779a00; "Scene::Render(): No
scenecontext found") -> per component `Positional::vfunc693` 0x751770 (frustum culling, LOD, draw items into the
producer half of the double-buffered render list, see x4-re render-decouple.md) and `HoloMap::vfunc915` 0xe197f0.

Simulation-visible side effects of slot 10 found statically and confirmed natively (section 5):
- **Thread RNG**: `Random::FloatState` 0x14ddc90 on TLS+0x318 (the MoveWorker stream that AI chance rolls, MD,
  movement controllers etc. also draw from) in the scene render helper 0x77b890 (per rendered zone: a random
  time offset scaled by game speed), `maybe_U::SpawnEffect` 0xd73e00 and the effect creator 0xd717a0.
- **Components and UniverseIDs**: `Zone::vfunc693` 0x8429a0 -> 0x82f1e0 -> 0x8aba40 ->
  `U::MT::WeldingMassTrafficBehavior::vfunc16` 0x89a7d0 -> SpawnEffect -> `CreateEffectObjectCreator` 0xd73060
  (AssignNewID 0xcc220, PostEvent) -> 0xd717a0 (`Component::ConnectTo` 0x3d6e50, SetLocalTransform): visual
  welding effects of construction traffic become components in the tree.
- **View-cache flag**: the render helper 0x74ff70 sets bit 0x100000 in the flags word of the component's view
  cache (`or [comp+0x11c+idx*0x80], 0x100000` at 0x75049a) after its LOD classification. Answer of transforms
  (Q11): it means "rendered, attachment poses wanted"; PropagateCacheFlags then lets Positional vfunc 622
  0x745920 recompute the attachment-pose block vc+0x30 (and clear the bit); BuildCache's walk itself never reads
  the word. Without camera rendering the server saves that work (0.16-0.27 ms per frame of BuildCache in the
  8-sector profile) and stops refreshing attachment poses; the only known reader of vc+0x30 is the render helper
  (not proven that no simulation code reads it). Other dirty bits read by simulation code (0x40000 pose changed,
  0x10000 registered) are not touched by the camera render.
- OverlayCamera::Render 0x986070 calls Camera::Render 0x983160 directly (not through the vtable), so the
  `camera` site does not skip overlay cameras.

What the layer walk is (decompiled): AnarkScene slot 12 walks the scene's element tree depth-first and calls
slot 15 per visible element (+0x10) and slot 16 after its children. Slot 15 computes the element's world
matrices (+0x88..+0x104) from its local transform (+0x18..+0x48) and the scene's matrix stack (+0x70, depth +0x80),
with dirty propagation in +0x15b, and assigns render layers ("MAX_LAYERS exceeded"); slot 16 pops the stack.
So it is the UI scene-graph transform pass. While it is skipped, element world matrices stay at their last
values (dirty flags stay set, so the first walk after the diet recomputes them). Readers are UI-side: the UI
render, hit testing/picking and Lua functions that return element screen rectangles. The game's UI Lua can turn
UI state into simulation inputs only on user interaction (clicks, hotkeys), which a server does not have.

The element update 0x1399c40 (decompiled) walks the element tree (child +0x20, sibling +0x18, parent +0x10),
evaluates keyframe tracks against the presentation time (+0x28 stamps, key arrays +0x30/+0x58, count +0x2f),
marks changed elements through the presentation's vfunc +0xa0 into per-frame dirty lists that 0x138f2c0
clears at the start of every update, and asks the presentation's time/slide object (+0x310) for child times. It
reaches no Lua entry point; UI script events are dispatched earlier in the same update (0x138fa90).

Slot 6 Anark functions (all 573 sampled ones) call no `Random::*` helper; the only UniverseID allocation in slot
6 is `Lua::CreateSource` 0xb0e8a0 -> `CMessageManager::CreateSource` 0xae9050 (UI ICom message sources take IDs
from the universe ID map), inside the Lua dispatch that the diet keeps.

## 3. Sites

| site | hook (data-only except `ui_elements`) | what is skipped | equivalence |
|---|---|---|---|
| `ui_scenes` | AnarkScene vtable 0x2bc0e10 slot 12 (entry 0x2bc0e70, fn 0xaf8760), skipped only inside AnarkSceneManager slot 3 (entry 0x2bc0dd8, fn 0xaf8e50) on the MoveWorker | the per-frame UI scene-graph pass (element world matrices, render layers); slot 5 still clears the scene's per-frame lists, so they stay empty (= the engine's state for a scene whose root is not visible) | simulation identical (static + native audit); UI element matrices stale (UI render, picking) |
| `ui_elements` | CODE PATCH: E8 rel32 at 0x138f477 (call of 0x1399c40 inside 0x138f2c0) retargeted to a near stub; verified against the original target, compare-exchanged, re-verified every frame, restored on disarm | the Anark element update (keyframes/attributes) | simulation identical as far as probed (static direct-call closure + native audit-003: 0 RNG, 0 IDs, 0 component words); UI animations stall: opt-in |
| `draw` | RenderFlags bit `disable_rendering` 0x8000000 of qword 0x6adfa30 and staged copy 0x6adfa38 (interlocked OR/AND of that bit only, re-applied each frame, released only if we set it) | main-thread `RenderPreviousFrame` 0xf9ff40 skips drawing the render targets (test at 0xfa007a); per-target bookkeeping, present and frame limiter still run | identical (main thread only reads the double-buffered half) |
| `ui_render` | CUIController XVbiHandler vtable 0x2bc07d0 slot 2 (entry 0x2bc07e0, fn 0xb3bba0) | UI draw-list build (CXAnarkController slot 2 0xb36c10 -> AnarkSceneManager slot 4 0xaf8a10 -> AnarkScene slot 6 -> AnarkRenderEngine 0x13af290/0x13af530/0xb2f740); also the OS cursor shape (g_InputSystem+0x90e8, WM_SETCURSOR) | no RNG/ID/event effect natively; statically no per-frame growth (manager state saved/restored per call, render queues reset by FrameTick, element dirty bits 1/3 at +0x15b stay set and only force the walk to recompute); native commit-size check pending: opt-in |
| `camera` | U::Camera vtable 0x2bae1a0 slot 3 (entry 0x2bae1b8, fn 0x983160), skipped only inside the GameClient render bank (entry 0x2c8abe0, fn 0x122bfc0) | camera render-list preparation | **changes the RNG stream, IDs and view-cache flags**: opt-in |
| `rng_split` | X4::GameRender vtable 0x2c8ac08 slot 1 (entry 0x2c8ac10, fn 0x121c1f0) | nothing: slot 10 runs with TLS+0x318 swapped to a private render stream (seed "RENDERV1") | a determinism fix, verified natively (audit-003); effect objects created by the render still take IDs |

Per-frame guard: empty VBI slot 2 (before GameClient::Move). It re-verifies the table pointer, its own slot and
every installed vtable entry; any change disarms everything (entries restored by compare-exchange, a foreign
value is never overwritten, the draw bit released). Wrappers pass straight through on other threads (loading
mode runs the UI on the main thread), while loading, and when not armed. `period=N` presents every N-th frame
fully (an operator heartbeat). Linux counterparts of every site and of the RNG/ID side effects are in
rewrite-coord/questions.md Q10 and its correction (Linux vtable slot = Windows slot + 1 except where MSVC and GCC
order overloaded virtuals differently: the Linux layer walk is AnarkScene slot 14 -> thunk 0xa7e250 -> 0xa5d840;
Linux presentation update 0x2338f80 calls the element update at 0x2339015 and the Lua dispatch at 0x2339098;
thread RNG at `fs:[-0x2720]`; AutoIDMap fields +8).

## 4. Module

`native/engine/framediet.c`, built by `python native/engine/build.py --zig <zig> --framediet [--test]`,
never packaged. Lua: `start(mode, sites[, period[, audit_every[, digest_max]]])` returns a handle or
`nil, reason`; `handle:set(mode[, sites])`, `handle:stats()`, `handle:active()`, `handle:stop()` (also `__gc`).
Modes: `diet` (skip the active sites), `pass` (all wrappers installed, inert: A/B in one session), `audit`
(run the originals and measure). Sites: `ui_scenes`, `ui_render`, `camera`, `draw`, `ui_elements`, `rng_split`, `server`
(= ui_scenes,draw), `all` (= ui_scenes,ui_render,camera,draw; ui_elements and rng_split are always named).

Safety: X4.exe verified by size, SHA-256 and PE header; 16 code signatures (signatures.py `FRAMEDIET_SITES`)
for every wrapped function, the patched call site, the render-flag test and copy, and the audit probes; every vtable entry must hold the
verified original before anything is written (all-or-nothing install); main Lua thread only; DLL pinned; state
kept in static storage (the Lua handle only holds a generation number, so a wrapper running on another thread
never touches memory the Lua GC freed); digest reads through ReadProcessMemory (a component freed meanwhile
yields a failed copy, never a fault). Registered in rewrite-coord/hooks.tsv.

Threads (code review 26 Oct, fixed in 3e4ab7a): on loading screens the UI, and so Lua (`stop`, `__gc`, `set`),
runs on the main thread while the MoveWorker keeps calling the slot 2 guard. Arm, disarm, set and the guard
therefore run under one SRW lock (the guard only tries it and skips that frame), and the guard returns on
loading frames before its entry checks. Without the lock, a stress test that stops the owner on one thread
while another runs the guard fails 5 of 5 runs (access violation or a vtable page left writable); with it,
3,000 rounds pass. Arming is refused on the main thread (`loading (UI on the main thread)`, retried by the
loader), so the owner thread is the MoveWorker and the rel32 is written by the thread that executes that call.
`start()` checks the PE header and the loading flag before the file hash, which is computed once per process.
The engine copies RenderFlags with plain stores (options apply 0x121ef1f, ProcessGraphicsStateChanges
0xfa3811) and never sets `disable_rendering` itself, so a bit of ours that a racing copy brings back is
cleared again by the guard for 120 frames after a release, and the next owner adopts a stale bit of ours.

Loader `native/engine/framediet_probe.lua` (inert unless `X4LAN_FRAMEDIET_MODE` is set and the config gate
`test_profile == "45841"` holds): retries while loading, loud `FEATURE framediet DISABLED reason=...` on any
refusal or disarm, `FRAMEDIET` stats every `X4LAN_FRAMEDIET_REPORT_S`, `FRAMEDIET_AUDIT` rows and
`FRAMEDIET_FRAMES` totals in audit mode, phase cycles (`X4LAN_FRAMEDIET_AB_S`, `X4LAN_FRAMEDIET_AB_CYCLE`, e.g.
`pass;server;all` or `audit:camera;audit:camera,rng_split`) for in-session A/B runs.

## 5. Equivalence evidence

Method. Skipping region R is behaviour-identical for the simulation if, for the same inputs, R writes nothing the
simulation reads. Audit mode tests that per call on the running game (diet off, originals run):
- thread RNG (TLS+0x318) before/after; the exact number of LCG steps is recovered by stepping (the generator is a
  bijection), bound 65,536;
- UniverseID insertions: AutoIDMap insert counter (+0x38 of `*(0x6cf1500)`);
- deferred event list size (`U::g_EventCommandQueue` 0x39943a8 +8; other threads also post, see control);
- TLS game time unchanged;
- component digest: the first 0x200 bytes (U::Component: identity, parent, children, attention, flags, timed
  event queue, both view caches) of up to 16,384 registered components (AutoIDMap slot order, rotating cursor),
  snapshotted before and compared after the region; refcount/lifecycle (+0x40) excluded;
- a control window of equal length in slot 2 measures what other threads change meanwhile;
- whole frames (slot 2 to slot 2): all simulation-stream RNG steps and ID insertions, for scale.

Synthetic (Windows, `build.py --framediet --test`): install/refusal/fail-closed lifecycle, skip only in context,
other threads and loading pass through, draw bit ownership, period, pass mode, audit counters (RNG steps, IDs,
events, word histogram, control), rng_split on/off (with simulation draws between the guard and slot 10), the
ui_elements call-site patch (skip/pass/audit, argument forwarding, restore, foreign retarget, refusal), set/stop/
active cores, the lock (skipped guard frames, 300 concurrent stop rounds), main-thread refusal, draw re-clear and
adoption, X4-like page protections (vtable pages read-only, code execute/read), CPU accounting: 13 groups. Real Lua
ABI: `framediet_lua_abi.py`. Loader: 16 LuaJIT tests; analyzer: 6 unit tests.

Native (functional, lane b1, other X4 running; `results/2026-10-26-frame-diet-audit.md`), audit-001, 9,952 frames:

| region | RNG steps | IDs | events | changed components |
|---|---|---|---|---|
| ui_scenes (40,000 calls) | 0 | 0 | +8 in 2 calls (background) | 0 / 1,359,872 |
| ui_render (9,952) | 0 | 0 | 0 | 1 freed block |
| camera (19,904) | 1,497,601 (~150 per frame) | 418 | 0 | 446 (+0x198 flags word 271x) |
| control (82) | 0 | 0 | 0 | 54 (view-cache words of other threads) |

audit-003 (10,801 frames, all five sites, 90 s phases without / with `rng_split`):

| region | RNG steps | IDs | events | changed components |
|---|---|---|---|---|
| ui_scenes (43,312 calls) | 0 | 0 | +2 in 2 calls (background) | 0 / 589,824 |
| ui_elements (43,312) | 0 | 0 | +2 in 1 call (background) | 0 / 589,824 |
| ui_render (10,801) | 0 | 0 | 0 | 1 freed block |
| camera (21,602) | 1,626,145 (150.4-150.8 per frame in every phase) | 976 | +1 | 105 (view cache [1] +0x1a0..+0x1d8) |

| phase (frames) | simulation stream per frame | camera region per frame | render stream per frame |
|---|---|---|---|
| no split (2,174) | 644.8 | 150.4 | - |
| split (2,767) | 392.9 | 150.7 | 150.7 |
| no split (2,868) | 443.6 | 150.5 | - |
| split (2,975) | 342.5 | 150.7 | 150.7 |

With the split, the render stream advanced by exactly the camera region's steps (416,880 and 448,239), so the
camera region is slot 10's only consumer and none of its draws reached the simulation stream; the simulation stream
stayed continuous over all 10,800 frame intervals (0 unknown distances: an unrestored stream would be far beyond the
65,536-step bound). Outside the camera region the simulation stream drew 494 / 393 / 293 / 343 / 446 steps per frame
(new-world population drift, no step at the switches). The element update's 78 functions reachable by direct calls
contain no event post, ID allocation, thread-RNG draw or Lua import (its 7 virtual calls go to Anark presentation
objects and are not resolved statically).

Interpretation: with fixed per-frame inputs (game time from the fixed-tick stage, same session RNG), the `server`
set leaves every probed simulation channel bit-identical, so runs with and without it differ only through
wall-clock-dependent engine behaviour (thread timing of Gravidar/MassTraffic/pathing), the same class of
difference as running on a faster PC. Removing slot 10's camera work on the server instead changes the RNG
stream relative to every client that renders (and IDs where welding traffic is in view); with `rng_split` on
the server and on all clients the RNG streams stop depending on the view (audit-003). IDs still differ where
welding traffic is in view (0.06-0.11 per frame in audit-003).

### rng_split design (determinism)

Proposal of the multiplayer determinism owner (rng-streams 23c841f), implemented as the `rng_split` site:
- Boundary: the whole VBI slot 10 callback (`GameRender::Move` 0x121c1f0, vtable entry 0x2c8ac10). Inside it the
  MoveWorker's TLS+0x318 holds a private render stream (seed "RENDERV1" at arming, continued across frames);
  the simulation value is restored right after. All slot-10 draws found statically go through TLS+0x318
  (0x77b890, SpawnEffect 0xd73e00, 0xd717a0, 0xddd570 via `Random::FloatState` on the TLS block); the only
  other RNG use there (Cluster render 0x3bb900) uses a stack-local state.
- Effect objects spawned by the render walk: their later updates in slot 3 (`EffectObject::Move` 0xd78d40,
  82 functions reachable by direct calls) draw no RNG and allocate no IDs; they post their own lifetime events
  (FadeOutEffectEvent). So with the split, the simulation stream no longer depends on the view. Their
  UniverseIDs still come from the shared allocator: a client that renders welding traffic allocates IDs a
  server without that view does not. Treat such objects as client-local (not in the replicated ID space).
- Parity needs the split on every participant (server and clients); with the split, `diet:all` and
  `pass` give the same simulation stream (synthetic test; natively audit-003: every slot-10 draw on the render
  stream, simulation stream continuous).
- Not covered: other camera couplings outside slot 10. Render cameras are attention sources (Universe+0x2c0
  anchors read by ComputeAttention, slot 3; see the attention hooks), and UI Lua can create ICom message sources
  that take UniverseIDs (`CMessageManager::CreateSource` 0xae9050). Linux: the same swap on `fs:[-0x2720]`
  around GameRender::Move 0xfb0180 (rewrite-coord Q10).

## 6. Measurements

Baseline quiet-window measurement is complete (section 14), the 8-sector load series too (section 16). Recipe of
the load series: attnload steps 0/8,
phases `pass` / `server` / `all` every 30 s (never `ui_elements` diet), MoveWorker slot-loop ms and fps from the fixed-
tick probe per phase (`framediet_analyze.py phases`), CPU shares of the main thread and the MoveWorker per phase
(`audit --phases`).

## 7. What a headless Linux server gets for free

Nothing in slots 6 and 10: the Linux X4 under Xvfb + lavapipe runs the same MoveWorker loop (x4mods-61's VPS
profile: slot 6 21 %, slot 10 9.8 %) and the same RNG coupling (`fs:[-0x2720]`). `-disableui` removes the Lua
state and with it the mod. What it does get, without hooks:
- the main-thread draw can be dropped by its own no-render decision (x4mods-61's FrameState+0x200 read #2); with
  lavapipe this is CPU time on the same machine, so it matters more than on Windows;
- supported launch switches (parsed in 0x1221840 / 0x121df00): `-disablesound` (and `-disablemusic`,
  `-disablevoicesounds`, `-disableambientsounds`, `-disableuisounds`, `-disableeffectsounds`),
  `-disableventures`, `-disableventureupdate`, `-noantialiasing`, `-disablecockpit`,
  `-disableplayershipgeometry`, `-disableallplayershiprendering`, small `-width`/`-height`; their MoveWorker
  effect is unmeasured;
- the Linux split agreed with x4mods-61: they implement the Linux switches (rust/x4mp-agent), this branch owns
  the Windows module, the audit and the safe/unsafe list (Q10).

## 8. Next client-only costs

1. Anark element update 0x1399c40 (~1.9 / 2.0 ms): `ui_elements` instrumentation (09cadac) remains available
   in audit/pass mode, through its verified call-site rel32 patch and near stub. Stability-002 and bisect-001
   supersede the earlier UI-only assumption: skipping it stops UI Lua, not merely animation. Diet is refused
   for every period. The element walk builds the per-frame onUpdate dispatch list (section 12).
2. ICom message pump 0xae9880 / VisibilityData::Update 0xadcfa0 (0.75 / 1.2 ms): NOT presentation-only. Its
   message handling sets the player's soft target (`SetSofttargetMessage` in 0xaecb60 posts
   `U::PlayerChangedTargetEvent` deferred) and can post `U::StopDialogEvent` (0x11789d0), both visible to MD/AI.
   Keep it. VisibilityData::Update (0.46 / 0.76 ms, decompiled) is not a presentation-only part either: for each
   ICom message source it computes the distance to the player camera (Universe+0x3d0; in range below 15 km), the
   view angle and an on-screen test (0x97af10), and 0xaddfe0 turns those into the priorities that decide which
   messages are delivered. Delivery has the simulation-visible effects above, so skipping it would change them.
3. Slot 8 character animation, slot 9 sound (0.4 ms each under load): `-disablesound` clears the sound-enabled
   flag 0x2f4b9d3, read only by sound-system initialisation (SoundLibrary::Init 0x128bde0, XSoundEnvironmentLibrary
   ::Setup 0x128fa60, 0x14e9600, 0x14ea680), so slot 9 should idle. Unmeasured. MD can wait on speech (`event_speak_finished`,
   `event_speak_line_finished`); those events come from `U::EntitySpeakData` timers on game time (0x563d30 ->
   `OnSpeakFinished` 0x562590), with OpenAL source pause/resume calls alongside (0x14f0ba0 alSourcePause), so
   speech timing looks independent of loaded audio; check natively (same cue timing with and without
   `-disablesound`) before a server uses it. Character animation
   may move walking NPCs and attachments (simulation state): not a candidate without RE.
4. BuildCache (3.7-3.9 ms): Q11 answered: without camera rendering only the attachment-pose refresh (vfunc 622,
   0.16-0.27 ms) goes away (owner transforms).
5. Animation timelines in the simulation walk: `maybe_U::Positional::UpdateAnimationTimelines` 0x1165660 with
   `AnimationTimeline::SampleCached` 0x1165ee0 (walk agent's labels) cost 0.25 ms per frame at baseline and
   1.6 ms inclusive under the 8-sector load (SampleCached 0.9 ms, mostly inside it, partly via 0x116b0c0), all
   in slot 3 under Positional::Move. Some timelines are
   cosmetic (lights, rotating parts), others move connections and so attached children (attachment poses depend
   on animation state, see Q11). Not a presentation site as a whole; a per-timeline split (no connections
   driven, no attached component) would be needed first, with walk and transforms.
6. Slot 8 character animation `XGfx::SCharacterBaseClass::Move` 0x129f370: 0.41 ms per frame under load, ~0 at
   baseline. It updates the animation library's actor instances and their attachments ("ActorInstance",
   "Attachment ... of ActorInstance") on game time (TimerBase); its 228 functions reachable by direct calls are
   library code without a single U:: (universe) call. Whether simulation code reads its results (root motion of
   walking NPCs, attachment bones) is open; the audit framework could measure it (one more VBI vtable entry)
   once a reader search says it is worth 0.4 ms.
7. The game's own HUD Lua inside the Lua dispatch (0.6 / 0.8 ms in total): besides Lua itself, the sampled C calls
   are HUD work (`SetPointerOverride` 0.10-0.13 ms, `GetTargetElementInfo`, `RegisterMouseInteractions`,
   `GetWeaponDetails3`, `GetDroneDetails`, ...) plus the harness's own `DebugError` logging (0.08-0.18 ms). The
   engine side cannot separate them from the mod's scripts; a Lua-side server switch that keeps the HUD menus
   closed would (mod Lua owners decide).

8. Combat effect spawns in slot 3 (combat's Q24): `SpawnEffect` 0xd73e00 from `FireShot` 0x8185b0 (muzzle) and the
   impact effect 0xd519e0 (from `ApplyHit`) cost 0.60 % of MoveWorker samples at 87 ships and 1.44 % at 800 ships.
   Not a presentation skip as it stands: SpawnEffect draws the MoveWorker thread RNG (Random::FloatState in its
   direct-call closure, section 5 tool), allocates UniverseIDs (CreateEffectObjectCreator 0xd73060 -> AssignNewID)
   and posts events. A server that skips them changes its own RNG stream and ID sequence; with server-owned
   identities (ecs-core's Q16 answer) the IDs are acceptable, the RNG is not unless every participant moves these
   draws to a private stream (an `rng_split` around SpawnEffect, like slot 10's). Worth it only under heavy combat.

## 9. Running it

```
python native/engine/build.py --zig <zig> --framediet --test
python native/engine/framediet_lua_abi.py --lua-runtime <lua51_64.dll copy> --module .local/evidence-037-withheld
x4lane run --agent frame-diet --overlay <wt>\.local\lane\overlays\probe --overlay <wt>\.local\lane\overlays\framediet ^
    --lua "X4LAN_FRAMEDIET_MODE='audit'" --lua "X4LAN_FRAMEDIET_SITES='ui_scenes,ui_render,camera'" ^
    --until "\[X4LAN\] FRAMEDIET_AUDIT row=control" --until-count 30 --timeout 1200 ^
    --expect "\[X4LAN\] FRAMEDIET armed" --fail-on "FEATURE framediet DISABLED" --collect <wt>\.local\lane\runs\audit-001
python native/engine/framediet_analyze.py audit [--phases] <collect>\x4lan-debug.txt
```
The framediet overlay holds `native/x4lan_framediet_64.dll`, `ui/framediet_probe.lua` and `ui.xml.files`.

`x4lane` is `python C:\Users\developer\Documents\X4Mods\lanes\release\dev\x4lane.py` (release 82af231 or newer).

Timing (only inside a quiet window the lead opens with `handoff: frame-diet`; never queued in advance;
probe + attention-load harness steps 0/8 for 240/360 s + phases every 30 s):
```
x4lane make-overlay attnload --attnload <X4-Multiplayer-attnload> --out <wt>\.local\lane\overlays\attnload --steps 0,8 --durations 240,360 --warmup 20
x4lane run --agent frame-diet --label timing1 --timing --wait 7200 --overlay <probe> --overlay <attnload> --overlay <framediet> ^
    --lua "X4LAN_FRAMEDIET_MODE='diet'" --lua "X4LAN_FRAMEDIET_SITES='all'" --lua "X4LAN_FRAMEDIET_DELAY_S=10" ^
    --lua "X4LAN_FRAMEDIET_AB_S=30" --lua "X4LAN_FRAMEDIET_AB_CYCLE='pass;server;all'" ^
    --until "\[X4LAN\] ATTNLOAD done" --timeout 1150 --fail-on "FEATURE framediet DISABLED|ATTNLOAD error|HOOKS_DISABLED" ^
    --collect <wt>\.local\lane\runs\timing-001
python native/engine/framediet_analyze.py phases <collect>\x4lan-debug.txt          # MoveWorker ms/frame and fps per phase and step
python native/engine/framediet_analyze.py audit --phases <collect>\x4lan-debug.txt  # per-phase CPU shares (main thread, MoveWorker)
```
Equivalence of `ui_elements` and the RNG split (functional lane run): mode `audit`, sites
`ui_scenes,ui_render,camera,ui_elements,rng_split`, cycle
`audit:ui_scenes,ui_render,camera,ui_elements;audit:ui_scenes,ui_render,camera,ui_elements,rng_split` every 90 s.

## 10. Open questions and native steps

- Q11 answered (transforms): attachment-pose refresh; open whether simulation code reads vc+0x30.
- `ui_render`: per-frame UI queue growth when skipped (commit size over a long diet run).
- Whether `CMessageManager::CreateSource` IDs (UI message sources) differ between server and clients in practice.
- Production integration: decided (Q17, x4mods-61 agreed); patch for review in section 15. Not packaged until
  reviewed. The Linux server keeps its own switches in rust/x4mp-agent (x4mods-61, Q10).
- Native steps: safe-site stability (`server,rng_split` / `pass`) and the baseline quiet-window timing are done
  (sections 13 and 14); the 8-sector timing (`timing8`) waits for the batched window. Never queue the old
  ui_elements diet recipe.


## 11. UI liveness failure and fail-closed correction (26 Oct 2026)

Native evidence (all under this worktree's `.local/evidence-075-withheld`; functional runs under contention):

- `stability-002`: all skips plus `ui_elements,rng_split` stopped UI updates immediately after arming.
  The agent stopped the lane, verdict fail / exit 9. This was not a passing stability test.
- `bisect-001` on 5bceea6: individual ui_scenes, draw, ui_render and camera phases completed without a
  watchdog stall. At game time 280.131 the `ui_elements` phase began; at 283.167, frame 12,275, the guard
  reverted to pass after UI Lua stopped. Element skipped count was 731, and subsequent UI updates resumed.
  The generic lane tool returned pass because it counted phase lines. That verdict proves collection and
  watchdog recovery only; **the ui_elements diet failed**. It must not be used as liveness acceptance.

Correction: `fd_arm` and `fd_set` refuse ui_elements in diet mode (even when a periodic presentation frame
is requested); the wrapper also never skips this site. Audit/pass instrumentation is retained. A runtime
stall leaves the current owner in pass, records a reason and makes every later diet request fail for that
owner. The Lua loader emits `FEATURE framediet DISABLED`, reports the stall and stops cycling. It retains
its pass-through owner so an already active render RNG split is not silently removed during recovery.
A new explicit owner can be started for a different experiment; no automatic restart occurs.

`stats()` snapshots the owner and thread-accounting handles under the lifecycle lock, then releases the
lock before allocating Lua values. A Lua allocation/finaliser can stop and replace the owner; the returned
snapshot stays the original owner's data and no lifecycle lock is held across Lua allocation.

Verification of this correction: C synthetic lifecycle suite (including 300 concurrent stop rounds),
new unsafe-mode/sticky-stall tests and allocation-time replacement test; 17 real LuaJIT loader tests;
standalone Lua 5.1 DLL ABI validation. These are synthetic/standalone checks, not new native gameplay proof.


The offline `framediet_analyze.py stability LOG --check --min-duration 200 --max-gap 15` now returns
nonzero on watchdog recovery (either marker or nonzero cumulative stalls), requested/actual mode drift,
disabled features, insufficient report duration, missing frame progress, large report gaps or invalid/
regressing counters. This checks only the reported interval; it is not a gameplay-equivalence gate.
Historic bisect-001 fails this check for one watchdog stall and four mode-drift reports, even though its
41 reports cover 303.2 seconds with a maximum gap of 10.1 seconds. Engine error-like lines are listed for
review, not automatically equated with mod failures.


## 12. Why skipping the element walk stops onUpdate (static, 26 Oct 2026)

Verified against the pinned Windows X4.exe copy (SHA-256
19750a6563889a970f434b5566eb396c6b2dc29ff814bd3e336f838176ad6891, matching signatures.py).
This is a data-flow explanation of the native bisect result, not a new native test:

1. Presentation update 0x138f2c0 obtains its per-frame lists through vfunc30 (+0xf0). For the presentation
   vtable 0x2ca2988 the accessor 0xb61250 returns the embedded object at +0x298. At 0x138f3a8 it clears that
   object's +0x70 list size (and clears the storage at +0x60), alongside the other per-frame lists.
2. The conditional call at 0x138f477 runs the element walk 0x1399c40. That walk obtains the same list owner
   through its presentation pointer. At 0x1399e6e..0x1399e76 it appends an eligible element to the data pointer
   at +0x60 and increments the count at +0x70. It also builds activation lists and updates element flags.
3. The later Lua update 0xaf85e0 calls DispatchFrameScripts 0x13a1270. That dispatcher obtains the same
   per-frame-list object through presentation vfunc30, then passes its +0x40 container to 0x13a35c0 with the
   event name onUpdate (call setup 0x13a1433..0x13a145e). The container's data/count offsets are +0x20/+0x30,
   so these are exactly list-owner +0x60/+0x70. An empty list returns before any Lua callback is selected.
4. Widget fullscreen onUpdate calls CallUpdateScripts, which dispatches mod onUpdate handlers. Therefore
   reaching the C++ Lua-update function is insufficient: omitting the list producer removes the callbacks.

This corrects the earlier inference that zero direct Lua imports and zero sampled simulation writes made
this walk presentation-only. Its output is an input to Lua scheduling. A future optimization must preserve
list membership, order and activation/deactivation/slide events before it can skip any cosmetic calculation;
blindly keeping the old list is unsafe when elements change. Current code keeps the whole original walk.
Private listings remain in x4-re/work/rw-frame-diet/out/; no engine source/listing is committed here.


## 13. Safe-site native stability after correction (26 Oct 2026, 03:31)

`codex-safe-stability-001` used the b75b3bf native/loader correction with frozen overlay
`.local/evidence-066-withheld` (DLL SHA-256
7f5ca1b011fd555e378346042547d66169da9f09b5ad87fe0b93af72441033a6).
The released lane tool ran a new disposable world on b6/profile45856 and collected logs at
`.local/evidence-081-withheld`.

Sequence: server,rng_split at t10.045, pass at 70.057, server,rng_split at 130.077, pass at 190.082,
then server,rng_split at 250.095. There are 29 UI reports through t250.301 (240.256s covered),
10,616 frames, maximum report gap 10.0s, zero watchdog stalls, zero requested/actual mode drift and
zero FEATURE DISABLED lines. Scene walk calls: 42,556 total  / 22,552 skipped; draw bit acquired 3 times.
Render RNG split remained active across all phases: 10,616 calls /1,595,552 measured draws.
The new strict `stability --check --min-duration 240 --max-gap 15` passes the collected log.

X4 exited through WM_CLOSE (exit 0), and the lane tool reports lane_restored=true. This establishes
safe-site UI liveness for this interval, not whole-gameplay equivalence or long-run memory stability.
The run shared the PC with up to 6 X4 processes; available memory reached 4.9 GB and page-fault load was
high. timing_trustworthy=false: no performance saving is inferred. The log's other error-like lines
are the missing optional signature, a resource-area message and vanilla trade failures; no Lua error
or frame-diet disabled marker appears. Quiet-window timing remains pending the lead's scheduling.


## 14. Bounded quiet-window baseline A/B (26 Oct 2026, 03:58-04:03)

The lead granted quiet.lock to frame-diet, then transforms. `codex-safe-timing-001` ran once on b1,
profile 45850, using the identical validated frozen overlays/DLL of section 13 and the same disposable
creative-world scene. The tool confirms timing_trustworthy=true, max_x4_total=1, no other X4 observed,
minimum available memory 37.0 GB and lane_restored=true. After graceful exit it handed quiet.lock to
transforms automatically. No additional native test was queued.

The loader waited 30 seconds before arming, then ran three adjacent pass/diet pairs of 30 seconds each.
The RNG split was active continuously in both modes; diet selected only `server` (ui_scenes + draw).
Every first FRAMEPROBE sample after a switch was excluded. The remaining 28-29 samples per phase were
weighted by simFrames, giving these MoveWorker slot-loop mean times:

- Pair 1: pass 17.380 ms -> diet 13.246 ms; difference 4.135 ms (23.8%).
- Pair 2: pass 14.380 ms -> diet 12.488 ms; difference 1.893 ms (13.2%).
- Pair 3: pass 15.197 ms -> diet 12.674 ms; difference 2.523 ms (16.6%).

Median paired difference: 2.523 ms / 16.6%. Counting the diet->pass neighbours too, all five adjacent pairs
are lower in diet (1.135-4.135 ms; re-derived from the raw log with the step-aware helper
`.local/evidence-h036391-withheld`, same transient rule). Main-thread CPU time divided by wall time was 12.4-12.9%
of one logical core in pass and 5.4-5.7% in diet. This separate reduction is consistent with disabling
the main-thread draw; it is not additional MoveWorker time to add to the slot-loop saving.

Strict liveness also passed: 25 UI reports cover t30.048-210.202 (180.154 seconds), 10,845 frames,
maximum report gap 10.016 seconds, zero watchdog stalls, mode drift or FEATURE DISABLED. This confirms
the measured interval retained UI callbacks; it does not prove every gameplay channel equivalent.

Limits: only three sequential pairs in one fresh baseline world, not eight-sector attention load.
The first pass is slower than later passes, showing startup/world drift; report all pairs rather than
using its larger saving alone. No confidence interval or general production speedup is claimed. The
same fixed-tick probe, frame-diet wrappers, RNG split, 10-second reports and snapshot accounting remain
in both modes; their absolute overhead was not independently subtracted. Quiet admission verifies
other X4 instances, not the absence of every unrelated OS/background workload. No simulation/RNG
state was reset between phases, so this is paired timing evidence, not a deterministic replay.

Evidence: `.local/evidence-082-withheld` (result.json, x4lan-debug.txt, paired-summary.json).
The local analysis helper `.local/evidence-042-withheld` closes each phase at the next phase
marker, excludes the first FRAMEPROBE sample and never includes the final incomplete pass phase.
Recheck liveness with `framediet_analyze.py stability <log> --check --min-duration 180 --max-gap 15`.


## 15. Production integration patch (Q17, for x4mods-61's review; 26 Oct 2026)

Commits on this branch, each one concern, all additive:

| commit | files | what |
|---|---|---|
| 89c2712 | `extension/x4_lan_lab/ui/framediet.lua` (new), `tests/test_framediet_loader.py` (new) | production loader; nothing loads it yet |
| 3514ae6 | `extension/x4_lan_lab/ui/config.lua`, `extension/x4_lan_lab/ui.xml` | inert flags `frame_diet = false`, `rng_split = false`; ui.xml entry after simtick.lua |
| 1fae8e1 | `distribution/session_launcher.py`, `tests/test_framediet_session_config.py` (new) | installed configs: Windows `rng_split` for every role, `frame_diet` for a dedicated server (p1) only; Linux both off |
| 71d5de1 | `distribution/build_packages.py`, `native/engine/build.py`, `tests/test_ship_spec.py` | Windows packages bundle `native/x4lan_framediet_64.dll`; Linux packages never |

Loader behaviour (`framediet.lua`): inert while `enabled = false`. `frame_diet = true` arms mode `diet` with
the behaviour-identical `server` set (scene layer walk + main-thread draw) only when `dedicated = true` and
`slot = 'p1'` on Windows; anywhere else it logs `FEATURE framediet DISABLED reason=not_dedicated_server`, so a
playing host or client never loses its picture. `rng_split = true` arms the render RNG split (mode `pass`,
skips nothing). Linux, profiles outside 45841-45848 (np8 assigns one per slot), a missing DLL or a module refusal log
`FEATURE <name> DISABLED reason=...` once and load or arm nothing; loading screens are retried every second.
One `FEATURES process=x4lan-framediet framediet=<on|off|auto-disabled:reason> rng_split=<...>` line once the
outcome is known. The loader calls `active()` on every UI update (the stall-watchdog heartbeat); a watchdog
stall disables the diet for the session and keeps the split; a disarm disables both. `ui_elements` is never
requested and the module refuses it in diet mode. No automatic restart.

Turning it off: `frame_diet = false` and/or `rng_split = false` in the installed
`extension/x4_lan_lab/ui/config.lua` (each flag alone), or remove the `ui/framediet.lua` line from ui.xml, or
delete `native/x4lan_framediet_64.dll` (the loader then logs `load_failed` and arms nothing).

Known asymmetry (accepted in x4mods-61's review): the Linux server and Linux clients run without the split,
Windows clients with it. That is fine for the current server: the VPS's discard layer does not render, so its
slot 10 makes no view-dependent draws; a Linux client that renders keeps the camera coupling in its own stream
until x4mods-61's Linux switches add the same swap (`fs:[-0x2720]` around GameRender::Move 0xfb0180, Q10).

What it does not change: no Linux file, no transport code, no other feature's defaults. Current packages build
the dedicated server for Linux, so `frame_diet` stays off in practice until a Windows server package exists;
Windows clients do arm the split (one-time ~0.1-0.2 s hash of X4.exe when it arms after loading).

Tests: 13 loader and 2 session-config LuaJIT/unit tests, the package-assembly test (fixture DLLs, both build
paths). The full Python suite on this PC shows the same failure set before and after (2 failures, 222 errors,
pre-existing on Windows: LuaJIT/native-environment tests), 729 tests. Native: the sites and the module are those
of sections 13 and 14 (same DLL); the loader itself has not run natively yet.


np8 delivery (branch `contrib/ferrite-framediet-np8` from `contrib/ferrite-np8` d914645): c887f66 imports the
module, `signatures.py`, the synthetic tests, the ABI check, the lane probe and this report from
contrib/ferrite-rw-frame-diet 6caf810, with `native/engine/build.py` reduced to the frame-diet module; then the Q17
commits as cherry-picks (`-x`), adapted to np8 where needed: loader 2a8f617 and the 45841-45848 profile gate
20a5b08 (as is), config/ui.xml 5f11fea (entry after `ui/frame_profile.lua`), installer d49bc7c (np8's dev-rule
comment, the test session carries `build`), packager 2d42b30 (only `build_framediet()` and `--zig`, np8's pair
and dedicated-set tests check the module in every Windows package and in no Linux package). Built there, the
module's code and data sections are identical to the natively validated DLL (only lld's path-dependent build id
and PE timestamp differ). Full Python suite: 754 tests, the same failure set as d914645 (2 F / 38 E on this
Windows PC); synthetic C suite and the real Lua ABI check pass.

## 16. 8-sector timing in the lead's quiet window (26 Oct 2026, 13:24-13:35)

`timing8-002` (lane b1, profile 45850): quiet.lock handed from overhaul-lead, no other X4 in any of 67 samples,
min. 32.2 GB free, timing_trustworthy = true. Same frozen DLL as sections 13/14. Attention-load harness step 0
(240 s) then step 8 (360 s); loader pass / `server,rng_split` every 30 s, RNG split on in both modes. MoveWorker
slots 1-14, simFrames-weighted, first sample after each switch dropped, phases spanning a step change excluded
(`.local/evidence-h036391-withheld`).

| step | pass ms | diet ms | adjacent pairs lower in diet | median saving, pass-first pairs |
|---|---|---|---|---|
| 0 | 15.58-16.83 | 13.99-14.44 | 6 / 6 (1.36-2.64 ms) | 2.10 ms (13.0 %), 3 pairs |
| 8 | 26.43-28.51 | 23.49-27.03 | 10 / 10 (0.89-4.48 ms) | 2.11 ms (7.5 %), 5 pairs |

Main-thread CPU: pass 8.8-14.5 %, diet 4.8-6.8 % of one core. The saving is the predicted ~2 ms of the scene
layer walk plus the draw's main-thread share, independent of the simulation load.

Non-X4 load (the lane tool only checks other X4 instances; `.local/evidence-h3dfc5c-withheld` sampled all processes
every 10 s): about 4.3 of 32 logical CPUs on average during the measured part (max 9.8 for ~30 s early in step 8;
machine total mean 28 %, max 59 %), mostly cpptools.exe (VS Code IntelliSense), MsMpEng.exe (Defender), dwm.exe,
Code.exe, claude.exe, Taskmgr.exe and firefox.exe. At the start 6.4 cores (cpptools 101 %), at the end 6.0
(python 98 %, cpptools 81 %). Paired adjacent phases share that background. Limits: one world, 3 + 5
pass-first pairs, no confidence interval, probe and wrapper overhead in both modes. Evidence:
`.local/evidence-h3232a1-withheld`, `timing8-002-cpu.jsonl`; rewrite-coord/results/2026-10-26-frame-diet-timing8.md.
