# Native frame gate and battle comparison

The native frame gate and bounded tactical lockstep fixtures work in two real
X4 instances on one PC. Current fleet co-op and PvP tests both pass 18,000 frames with
14 commands and 10 observed fleet changes each. Fresh live-input co-op also
passes 18,000 frames; live-input PvP, travel and boosted withdrawal pass their
3,600-frame tests. The native interrupt RNG gap and
relay shutdown error found during this work have targeted fixes and tests.
An earlier shield mismatch remains unexplained across runs.

The checked subset covers twelve fixture ships: native pose/physics, health,
orders, fleet identity and selected diagnostics. The frame barrier gates engine
time; the per-object determinism work covers registered fixture objects and
their measured subcomponents. Human cockpit control, two native player contexts,
docking/trading/economy, general weapons and whole-universe equality are not
established. Tests refuse differing checked state; no tolerance or copied
pose/health turns a failure into a pass. Older runs below retain their original
revision, feature scope and failures.

This is a separate opt-in experiment. It does not enable the playable `lockstep`
mode or replace the existing host-authoritative combat replication. The earlier
command-barrier evidence remains in [LOCKSTEP-LIVE-INPUTS-20261027.md](LOCKSTEP-LIVE-INPUTS-20261027.md).

## Implementation

`src/native/engine/lockstep_gate.c` redirects MainMessageLoop's call to FrameTick
after the previous MoveWorker frame has completed. The accepted fixedtick module
supplies 1/60-second steps. A loopback UDP coordinator must admit each next frame.
Networking operates on the main thread while the simulation/UI thread waits, so
Lua networking is not needed to release a blocked native frame.

Admission verifies the exact executable file hash, size, PE identity, call-site
and target hashes, fixed-step settings and non-loading state. The call redirect
uses an aligned 64-bit compare/exchange, preserving adjacent instructions. Every
frame checks ownership, game speed, timestep and the matching UI report. Timeout,
shutdown or guard failure restores owned patches; it ends the experiment rather
than continuing to claim synchronized gameplay. The module remains pinned while
a trampoline could still be referenced. It does not modify X4 files on disk.

The coordinator requires both peers' immutable reports, exact common game/real
clocks, 1/60-second progression and matching declared state hashes. Reports and
permits are retried. A three-second deliberate hold tests native clock freezing.

The battle fixture creates 12 seeded native ships independently in each process.
It sends spawn at tick 1 and attack orders at tick 10 through the existing UI/MD
event path. Every frame hashes stable fixture actor numbers, operational/dead
state, actual sector position/orientation and native hull/shield percentages.
There is no NPC pose or health replication in this fixture. Hashes exclude
transient engine object IDs. This is a declared battle-state subset, not a digest
of every engine object, AI context, projectile, inventory or worker queue.

## Native evidence

Raw data stays under `.local/native-lockstep/`; no player logs belong in Git.
Each run uses two newly allocated disposable lanes and a frozen extension base.

| Run | Result |
| --- | --- |
| `clock-001` | Probe failed to load because X4 removes `ffi.load`; no gate installed. Fixed by using `package.loadlib` and X4's Lua module ABI. |
| `clock-002` | PASS: both completed 120 native ticks. Hold 3.062 seconds, clock drift during waits exactly zero, common clocks 5.016666666666654 to 7.016666666666647. |
| `clock-003` | PASS on the normal-mode build and reusable runner: 120 ticks, hold 3.047 seconds, zero clock drift and clean restoration on both clients. |
| `battle-001` | FAIL at tick 20. Actors 2 and 3 differed in x by about 0.00061 metres. All prior declared state hashes matched. |
| `battle-002` | FAIL at tick 11, immediately after engagement. Actor 2 moved 0.0166 metres in one instance while remaining stationary in the other. |
| `battle-serial-001` | FAIL at tick 11 with the optional pathing-order experiment. Actor 3 moved in only one instance. Both performed 11 early pathing joins and restored that patch. |
| `battle-rng-001` | FAIL at tick 11 with per-frame MoveWorker RNG seeding. Actor 4 moved in only one instance. Both seeded 11 times and restored the hook; no early pathing joins. |

Every completed gated run above reported `restored=1`, `wait_game_delta=0`, and
maximum timestep error `5.89805982e-17`. Both clients stopped gated progression at
the first mismatched tick. The native stop restores normal execution briefly for
runner cleanup; it is not a persistent paused desync recovery state. All these
lanes restored after the run. Independent validation recalculates SHA-256 from
the actual logged samples and checks clocks, first mismatch, stop and cleanup.
At the `clock-003` cleanup check, every lane was free and no X4 process remained.

`battle-002` also observes the MoveWorker TLS random stream. It already differs
at tick 0, before fixture creation, and differs at subsequent sampled ticks.
This proves an unsynchronized simulation input, not that RNG alone caused the
first positional mismatch. The same new-game seed is insufficient to establish
identical initial native state. The local engine analysis identifies separate
per-thread random streams and asynchronous event producers.

The ordering experiment waits for the native pathing pass immediately after its
normal kick, then replaces exactly its completion token. The original later join
still consumes that token, applies orders and flips buffers. Its separate call
redirect uses aligned 128-bit compare/exchange with pinned kick/join/worker
signatures. It failed to establish battle equality and remains off by default.
Do not treat it as a determinism fix or a performance improvement.

The separate RNG experiment sets only the owning MoveWorker thread's TLS stream
at its existing pathing-kick boundary, using a stable function of the admitted
tick. Despite identical seeds, the observed stream states differ after every
frame: the games consume different amounts of randomness during the frame. It
does not control other threads, persistent object streams or already divergent
initial state. It also remains off by default. Neither negative experiment
identifies the sole cause of the first ship mismatch or establishes a fix.

## Reproduction and limits

Build with `tools/dev/build_native_gate.py --zig <Zig-0.15.2> --test`.
Create a new frozen overlay with `tools/dev/build_native_gate_overlay.py`, supplying
the gate DLL, accepted fixedtick DLL, `--mode clock` or `--mode battle`, and a new
`--out` directory. `--serial-pathing` enables only the ordering experiment;
`--seed-rng` enables only the MoveWorker frame-seed experiment.
Run `tools/dev/run_native_gate.py` with the released `--lane-tool`, frozen inert
`--base`, `--overlay`, and new `--out` directory. Validate with
`tools/dev/validate_native_gate.py <run> --scope clock|battle`.

Eight coordinator unit tests cover missing peers, the hold, permit retries,
changed reports, wrong timestep, unequal starting clocks, digest mismatch and
foreign credentials. Native C tests cover refusal outside X4, owned atomic
redirect/restoration and refusal to overwrite foreign changes. Build uses
`-Wall -Wextra -Werror`; the 128-bit experiment requires CMPXCHG16B support.

The remaining work is substantial: equal initial native world/AI/RNG state,
deterministic random streams and asynchronous event ordering, native player-input
capture/application before simulation, and a stronger end-of-tick state digest.
The native gate is not yet wired into the existing protocol-2 player command
barrier. Independent background workers may continue while FrameTick waits;
zero game-clock movement alone does not prove all engine memory is quiescent.
No whole-universe, Windows/Linux, interactive two-player combat or long battle
acceptance is claimed.

## Shared-save admission (continuation)

The optional snapshot runner now stages one owned native save into reserved slot
009 of each admitted disposable profile. Exclusive creation refuses an occupied
slot. It verifies identical source/staged bytes, archives the slot after X4 exits,
and removes only the unchanged archived file. Five filesystem tests cover this
ownership lifecycle. Protected profiles remain refused. No ordinary save is used.

Snapshot boot installs the gate in the start menu, observes the load transition,
and waits before the first non-loading FrameTick. It exclusively owns the pinned
fixed-60 branch/data for this mode and refuses the separate fixedtick DLL. Owned
branch/data words are restored by compare/exchange. Two LuaJIT lifecycle tests
verify one load request and reporting frame 1 across the UI reload. Initial hash
identity covers the staged bytes; subsequent hashes cover the declared native
state subset. This does not claim a whole-world state hash at admission.

Game clocks still compare exactly. Real clocks are explicitly compared as elapsed
time from admission, because menu/load durations are local to each process. Raw
absolute real clocks remain in the evidence. The normal gate still requires
absolute game and real clock equality; no gameplay tolerance was introduced.

| Snapshot run | First failure | Observation |
| --- | --- | --- |
| `snapshot-boot-001` | 24 | Same save; attention 6; actual movement differs. |
| `snapshot-boot-rng-001` | 74 | Same save plus MoveWorker frame seed; positions remain equal until different shield damage appears. |
| `snapshot-near-001` | 14 | Same nearby-attention save; small lateral movement differs. |
| `snapshot-near-rng-001` | 14 | MoveWorker frame seed does not fix nearby movement. |

All four have independently validated sample hashes, three-second holds with zero
clock drift, first-mismatch stops, owned hook/data restoration and lane/save-slot
cleanup. These are negative combat results. Per-frame RNG draw counts also differ
between clients despite identical frame seeds (for example 1431 versus 1175 draws
in frame 1 of the distant-sector seeded run).

Fixture setup matters: earlier new-world fixtures used attention 8; the first
shared save accidentally placed the ships far from the observer (attention 6).
Capture now checks all 12 actors are actually `nearby` before saving; the accepted
nearby starting snapshot is `snapshot-capture-005`. A failed attention guard in
capture 004 produced no save. These distinct cases are retained, not combined
into a general combat result.

The steering experiment observes inputs/outputs and worker RNG for
the twelve registered fixture actors. An optional separate mode isolates RNG
only within each actor's steering call and restores the previous thread stream.
It is default-off, excludes the existing physics census DLL, checks the original
vtable/function signature and restores only its own entry. A separate option
forces full steering updates for these actors, recording both the received and
applied cheap-update flags. A fixture-specific experiment is not general lockstep.

`snapshot-flight-diag-001` reproduced tick 14 and recorded different cheap/full
decisions for actors 1, 2, 3, 11 and 12. Some other actors had equal pose inputs
and full-update flags but different outputs and worker RNG. This exposes the
wall-time budget as a varying simulation input. `snapshot-flight-fixed-001`
forced full updates and isolated RNG per fixture actor/tick/call. It still failed
at 14: actors 6, 8, 9 and 12 had equal recorded pose input, dt and RNG before/after
but different lateral outputs. Other model/world inputs remain unsynchronized.
Both runs restored the steering entry and passed independent evidence checks.
The following context trace, `snapshot-flight-context-001`, also fails at 14.
With the pathing join and MoveWorker frame seed enabled, actors 4 and 7 already
have different steering behaviour classes on entry. Raw model diagnostics also
contain instance-local IDs, pointers and padding; they are not portable hashes.

Native unit coverage also checks that the scoped steering call invokes its
original exactly once, forwards the cheap flag in observation mode, forces full
updates only when enabled, produces equal random results from different outer
streams, and restores each outer stream afterwards.

## AI and complete movement-update experiments

The fixture AI experiment brackets the regular AIDirector interpreter call,
resolves its owning ship through a bounded component-parent walk, and optionally
seeds the worker RNG for that actor/tick/script/run. It calls the interpreter once
and restores the outer stream. It does not cover interrupt runners or all MD
events. Exact call-window/function hashes, atomic owned restoration, identity
checks and default-off configuration apply.

`snapshot-ai-rng-001` was refused before launch because RAM admission was not met.
`snapshot-ai-rng-002` ran both real games and still failed at tick 14. Both recorded
72 fixture AI calls, zero diagnostic failures and successful restoration. The
last recorded script, program counter and RNG before/after values match per
actor: order selection at tick 12 and fighter attack setup at tick 13. These
records describe the last call per actor/frame, not an exhaustive AI event trace.

At tick 14, the received movement-budget flag differs for actors 1, 2, 3, 11 and
12. The cheaper side enters steering with a generic behaviour, while the other
side has a combat behaviour. Forcing full updates only inside steering occurs
after the enclosing pathing Agent has already selected behaviour. A new opt-in
bracket therefore wraps the complete Agent update, before that decision, and
can force its full-update argument and isolate its RNG. The aligned rel32 operand
is changed by 32-bit compare/exchange; adjacent instructions are untouched.
Registration checks the fixture component identity and navigation-controller
state-slot backreference.

`snapshot-agent-001` moves the first checked mismatch to tick 50, with equal raw
steering pose/velocity output through tick 49. At tick 50, actor 6 first differs
by one float step in one position component and by small rotation/velocity terms.
Its acceleration/turning limit block already differs at tick 45: several limits
are reduced to 75% on client 2 only. The recorded Agent dt, time, per-frame call
counts and RNG before/after match throughout; AI and steering RNG records also
match. The producer of the limit difference remains to be identified.
Both clients ran 576 bracketed Agent calls, 444 steering calls and 90 AI calls,
with zero diagnostic failures and owned restoration. Independent validation
confirms first-mismatch stop, zero clock drift during the 3.047-second hold, equal
clocks and lane/save-slot cleanup. This is still a negative combat result.

`snapshot-agent-limits-001` repeats with extra read-only input-buffer/thread
context diagnostics and fails at tick 36. Actor 12's native parameter read/write
buffers first differ at tick 35; the complete Agent update receives the differing
limits at tick 36. Its worker TLS game time/view selector are zero on both sides,
while the explicit update time matches. All hooks, lanes and staged save slots
restore, and independent evidence validation passes. This variation means tick
50 is not a repeatable deterministic boundary. The refresh path includes pilot
skill modifiers; a further live trace checks the saved pilot skill values against
the reconstructed games. No skill override or movement correction is applied.

## Ship, parameter initialization and weapon experiments

`snapshot-agent-pilot-001` fails at tick 35 with differing shield damage while
recorded movement and movement limits still match. The saved/live pilot skill
byte used by the native parameter calculation matches between clients. The raw
eight-byte diagnostic around it is not a complete semantic skill representation.

The optional ShipSmall Move bracket scopes each fixture ship's RNG around its
original recursive child walk. `snapshot-ship-001` fails at tick 40 after another
movement-limit difference. Its first differing ship-level RNG exit is tick 31
despite identical entry seeds, showing different random consumption inside a
ship update. All 456 ship calls per client and hook restoration are accounted for.

An additional default-off experiment invokes the SHA-pinned native parameter
builder once for each registered fixture actor after load, using its original
controller and current write buffer. It recomputes each game's own derived data;
it does not copy peer state. `snapshot-params-001` has no recorded movement-limit
differences through tick 56, but still fails there: actor 6 shield is 90 versus 91,
with a small rotation difference. Steering velocity inputs first differ at 56;
the first differing ship RNG exit is tick 32. Both clients record 12 parameter
initializations and 648 ship calls. Independent validation confirms the negative
verdict, zero clock drift and complete owned hook/lane/save cleanup.

The next experiment brackets individual native Weapon Move calls. Registration
uses actor and physical upgrade-slot index, preserving empty slots, then checks
the native component ID and bounded parent chain. The original weapon update runs
once, with optional actor/slot/tick/call RNG and restoration of its outer stream.
The fixture's base Weapon and MissileLauncher vtables are covered; turrets,
projectile updates and event callbacks outside Move remain outside this scope.
The hook refuses foreign entries and the combat DLL, and is off by default.

`snapshot-weapon-001` refused registration before combat because the fixture also
contains a torpedo launcher. No battle acceptance or complete stop evidence was
produced. The disposable runner restored its lanes and staged saves. The second
version adds the verified MissileLauncher entry, which has the same original
Move function, with all-or-rollback installation and owned CAS restoration.

`snapshot-weapon-002` **passes 120 admitted battle frames (two simulated seconds)**.
Independent validation finds equal checked ship poses/hull/shield, equal clocks,
zero drift during the 3.047-second hold, and clean native/lane/save restoration.
Both clients register the same 30 physical weapon slots/macros, run 3,540 weapon
calls, and have matching per-weapon RNG records. Raw steering input/output matches.
Ship-level RNG consumption still differs starting at tick 34, and one raw movement
parameter word differs without a demonstrated semantic effect. Therefore this is
a bounded fixture result, not whole-engine state equality or interactive PvP/co-op
acceptance. Hull/shield in the checked digest are UI percentages, not full internal
health values; projectiles and pending events are not yet included.
An unchanged-overlay repeat extends the target to 1,200 frames.

`snapshot-weapon-003` stops at the first checked mismatch, tick 242. Weapon and AI
RNG records still match. Actor 12 receives different movement limits at tick 241,
including a sixfold difference in forward acceleration and speed terms; its
steering position first differs at 242. The boost-dependent parameter refresh
path is the next investigation. Independent negative-result validation passes,
including the 3.062-second hold with zero clock drift and complete owned cleanup.

## Native refresh scheduling and stricter state comparison

`snapshot-refresh-001` observes the original parameter builder at every native
caller, using a pinned entry trampoline and owned atomic restoration. It fails
at251. Both clients record2,513 calls:12 initialization calls and2,501 calls from
NavigationController upkeep. For common actor/frame/call keys, all recorded
parameter outputs match, but calls occur on different frames. The upkeep code
randomizes refresh delays for ships at least1km from the observer. Whole-ship
RNG scope does not isolate this draw from preceding child work.

A fixture NavigationController upkeep bracket now optionally scopes its RNG and
sets its first refresh deadline to one frame before the incoming update time.
That initialization happens once per registered actor; subsequent native delay
calculation is preserved. `snapshot-nav-001` remains equal in the checked digest
through959, then differs in actor5 rotation at960. Movement-limit records match
throughout. A one-float-step angular velocity difference is already visible on
entry to steering at902. Weapon RNG exit first differs604, and navigation RNG
exit first differs49; these remain unresolved internal divergence indicators.
Independent validation passes for the negative result and owned restoration.

The optional strict-state digest adds stored raw hull, shield recharge state and
parameters, and current/previous native position/basis/velocity vectors. It reads
shield storage without invoking the lazy recharge getter, which can write. It
excludes vector padding, raw pointers and instance IDs. This is still a fixture
subset, not a complete universe/projectile/event hash. Missing native state stops
the test. Native guard/read-only checks and12 Python/LuaJIT tests pass.

`snapshot-strict-001` catches actor5 angular velocity at901, while its visible pose,
exact hull and stored shield fields still match. Current and previous navigation
buffers differ by one float step. All recorded steering inputs/outputs through901
still match, placing the new write after steering. Independent hash recomputation
and owned cleanup pass. The next trace targets NavigationController::SetVelocity
and the native physics feedback that calls it. No tolerance or rounding is added.

`snapshot-velocity-001` fails733 in actor2 angular velocity. Both clients record
152 native SetVelocity calls with matching actor/frame/call keys and callerbf27d.
The incoming velocity already differs by one float step at733; the setter's
preexisting velocity is equal and its output preserves the differing input.
Every steering input/output, recorded movement limit, AI and weapon RNG trace
still matches through733. This places the first checked difference upstream in
physics body velocity, not in the navigation setter. Evidence and cleanup validate.
An added read-only body trace checks record ownership, BodyID/map bounds and body
backreference before recording both fixture bodies' raw poses/motion properties,
pre-step velocity, deltas and shape refresh deadline. These raw diagnostics are
not included in the semantic state digest.

## Body shape refresh and first destruction

`snapshot-body-001` fails at916 in actor6 angular velocity. Both bodies' recorded
poses and motion properties match through915; compound-shape refresh deadlines
differ from the first read. A fixture-only Body_UpdateShape bracket now offers
scoped RNG and one initial deadline alignment per compound holder. It preserves
the original call and restores its atomic entry detour; diagnostics remain default
off. Read-only body state is not treated as a portable semantic hash.

`snapshot-shape-001` and `snapshot-shape-002` both reach1086 equal checked frames
and fail at1087 in actor2 linear velocity. Both games first report actor8 destroyed
at1086. The first run's validator rejected26 initial refreshes because it assumed
at most24 holders; destruction replaces the two actor8 holders. The second run
records per-body initialization totals: actor8 has two each, all others one,
accounting for26 in both games. It validates the negative result independently,
including26,040 shape calls per client, no hook failures, zero hold-clock drift and
complete owned cleanup. The old first run lacks these totals and remains excluded
from accepted evidence. No state rounding or peer correction is used.

Shape update deadlines and scoped RNG traces match after initial alignment. Raw
inertia-like body fields can differ before checked movement diverges; unknown
metadata and padding are not semantic proof. The next investigation identifies
projectiles by firing actor, physical weapon slot and spawn ordinal and traces
their native Move/impact/destruction path. Native C guard/CAS/read tests and all12
Python/LuaJIT gate tests pass. Interactive PvP/co-op and whole-engine equality
remain unproven.

## Bullet identity and deferred wreck creation

A Zone creation observer associates native bullets with actor/physical weapon
slot/spawn ordinal; local pointers and IDs only guard identity. A paired Bullet
Move bracket optionally scopes RNG through original impact callbacks. It retains
every event across UI sampling boundaries and fails closed on bounded storage
overflow. Both original vtable entries and callee signatures are pinned, with
owned restoration. Missiles and arbitrary player guns are not accepted by this
fixture mapping. Native RNG restoration, duplicate birth/address reuse, overflow
and hook-ownership tests pass; a LuaJIT test covers events after frame reporting.

`snapshot-bullet-diag-001` reproduces1087 with760 births and54,633 updates per
client, no unknown or out-of-context bullets. The first differing bullet check
time is1032. `snapshot-bullet-001` scopes bullet RNG: all55,393 birth/update records
now match, including RNG entry/exit, hit counts and check times, but battle still
fails1087. DEADBODY diagnostics show actor8 wreck pose and velocity already differ
at1086. Both negative results validate independently and restore owned state.

The native KillEvent is deferred beyond BulletMove. ShipSmall's slot8 listener
invokes the native Destructible kill handler, which draws random linear/angular
velocity and constructs the wreck controller. An opt-in fixture listener bracket
now scopes RNG through that original event. `snapshot-death-001` matches the kill
event stream and actor8 wreck pose/velocity, then fails1091 in actor1 angular
velocity from physics feedback. All bullet records still match. Earlier actor1
body inverse-inertia and inertia-frame quaternion fields first differ896. The
next trace targets native mass-property updates. Negative evidence, zero clock
drift and all owned cleanup validate; native C and13 Python/LuaJIT tests pass.

## Attention refresh changes physics mass

`snapshot-mass-001/002` observe native BodyInterface::NotifyShapeChanged with
body/record/actor identity checks and original-call preservation. Both fail1091
and validate cleanly. Each client records138 notifications without hook failure.
Actor1's three child shapes are removed and re-added, recomputing mass/inertia
on the same body. The first run does this at930 versus896; the second at896 versus
927. Stack traces locate the operation in periodic attention refresh: actor1
changes from attention9 to8 at these differing frames. Native Object refresh
uses its own deadline and schedules the next check after0.75+random(0.5) seconds.
Thus identical ship motion can still rebuild collision geometry at different
poses. The next experiment aligns this fixture deadline once and isolates its
RNG while retaining native attention selection. No forced attention level or
copied mass properties is used.

`snapshot-attention-001` validates a negative result at1381, with1380 matching
checked frames. The fixture-only periodic attention bracket scopes RNG and aligns
the first deadline, preserving native attention selection. All138 mass-change
records now match, as do both deaths (1086 and1337), bullet records, wreck motion
and recorded body inertia/poses through1380. Each client records16,548 attention
calls and12 initializations. Native C and13 Python/LuaJIT tests pass; owned hooks,
profiles and save slots restore. Ship1 steering output first differs at1381.
Existing steering input diagnostics cover only the first96 bytes, so the next
trace expands them to the complete input and parameter blocks and avoidance data.
The background Gravidar publication path can retain old results when its worker
has not finished; that remains an unproven candidate, not a fixed cause.

Expanded input trace `snapshot-inputs-001` fails1411 with matching complete
steering input/parameter blocks apart from local identifiers and padding.
`snapshot-avoidance-001` fails1387. Its before/after obstacle vectors show different
cached wreck transforms/velocities despite matching actual wreck motion. Living
ship obstacle geometry matches. Gravidar publication timestamps differ throughout;
the steering cache refreshes on those timestamps. Both negative runs validate and
restore owned state. The next isolated experiment refreshes native avoidance
geometry every fixture steering call while retaining native contact membership.
This does not yet synchronize Gravidar detection or the entire background worker.

The default-off `--avoidance-refresh` experiment invalidates the fixture steering
geometry cache before each original full update. `snapshot-avoidance-refresh-001`
reaches3600 matching checked frames and three matching deaths, but remains
**unaccepted**: the new diagnostic refresh counter used a non-atomic increment
across pathing workers. The counter now uses the same atomic increment as calls.

`--strict-physics` extends the digest to attention and both physics bodies, including
wrecks: position/quaternion, linear/angular velocity, inverse inertia/frame/mass,
allowed DOFs and pre-step/delta velocities. Padding and process-local IDs are
excluded; missing native data stops the run. LuaJIT tests verify a changed wreck
velocity changes state, padding does not, and unavailable state fails closed.
`snapshot-physics-strict-001` reaches3600 matching digests with31,483 calls and
31,483 refreshes per client, but X4 crops its enlarged state log at8 KiB. It also
remains **unaccepted** because independent hash reconstruction lacks full bytes.
Canonical logs now use twelve individually bounded rows and a frame terminator;
the validator rejects missing/duplicate rows and missing terminators. Sixteen
Python/LuaJIT tests and native C guards pass. The complete-log native repeat is
pending. These are fixture experiments, not interactive PvP/co-op acceptance.

`snapshot-physics-strict-002` now **PASSES**3600 admitted frames (one simulated
minute), with complete canonical rows and independent SHA-256 reconstruction.
Both native clocks hold for3.063 seconds without drift; all owned hooks, fixed-dt
data, disposable profiles and save slots restore. The stronger checked state
includes native attention, exact hull/shield/navigation, both bodies and wreck
motion/inertia. This is bounded fixture acceptance, not native player input,
general radar detection, all weapons, economy or whole-universe lockstep.

RNG schema2 mixes subsystem, tick, actor, stream, call and detail separately;
the old XOR packing aliases distinct actor/weapon-slot pairs. Schema identity is
checked before reporting native frames. Native C and17 Python/LuaJIT tests pass.
`snapshot-seed2-001` validates a negative result at182, when actor10 begins fleeing.
Complete inputs and obstacle geometry match; stored steering angles at model+0x33c
and+0x340 already differ. The original steering constructor initializes these from
the loader thread RNG, and RunAway reads them before its scoped update draws new
values. This exposes additional unsynchronized initial state that the earlier
battle sequence did not exercise. Deterministic initialization is the next test.

`--steering-init` pins the complete native constructor and Random::FloatState,
then repeats only its two float32 angle draws on a local actor/model-generation
seed when registering a new fixture model. It preserves model orders and the
thread RNG; tests cover exact limits, once-only initialization, independent model
generations and untouched neighboring fields. `snapshot-steering-init-001` now
**PASSES**3600 frames under RNG schema2 and the stronger physics digest, including
independent reconstruction, all12 model initializations and complete cleanup.
The five-minute repeat uses `--trace-stride30` only for verbose diagnostics;
canonical state and bullet/death/mass events remain complete on every frame.

The experimental gate now supports protocol2 with one bounded command from each
client per frame. The coordinator waits for both immutable submissions and sends
the same ordered pair with every permit/retry. Native code verifies its own
submission before accepting a permit. The test adapter adds both commands to the
state hash and enqueues native MD orders in player order. Opposing-team and
shared-team fixtures validate actor ownership; native order existence/type and
application time are checked independently against the admitted input history.
The initial command set is Attack/Wait, with scripted submissions from each real
client. This does not yet capture keyboard/cockpit controls or establish human
PvP/co-op acceptance. Native C checks,24 Python/LuaJIT tests and MD schema
validation pass; the first two-instance command run is pending.

Long-run coordinator checkpoints now write atomically once per second and on
stop/failure. Rewriting the complete growing history on every frame caused
quadratic serialization work in the first18000-frame attempt. The running attempt
keeps its original code and evidence; no incomplete run is treated as a pass.

`commands-pvp-001` reaches1200 matching frames and creates all8 independently
submitted native orders at matching game times. The battle first differs from the
no-command trajectory at64. It remains unaccepted because20 steering
initializations exceed the original12-model validator and lack per-actor evidence.
`commands-pvp-002` adds generation totals and stops at2753: actor1 has2 versus3
model generations across clients. Pointer-based initialization misses allocator
address reuse. Its first checked gameplay difference is stored shield recharge
state for actor2, with matching pose and current UI shield percentage. All122377
bullet trace events match; the shield mismatch may have a separate cause.

The steering initializer now brackets the SHA-pinned native constructor and
counts each actual lifetime, even at a reused address, before its first update.
Registration tracks the Agent while it uses the initial spline model. Per-actor
counts, constructor/registration totals and owned patch restoration are audited.
BATTLE_SUBSET_4 also compares active spline/steering identity, generation and the
two steering random angles. Unknown model types fail closed; spline internals
remain outside this declared subset. Two startup probes were rejected by the
initial overly narrow model-state reader, before any combat. Native C and26
Python/LuaJIT checks pass; the corrected native repeat is in progress. No new
PvP/co-op acceptance is claimed.

`commands-pvp-005` now **PASSES**3600 native frames with independently rebuilt
BATTLE_SUBSET_4/input hashes. Both clients apply all8 commands at matching game
times; all20 model constructors, per-actor generations and angle state agree.
The3.062-second hold has zero clock drift and all owned patches/profiles/snapshots
restore. This accepts the bounded scripted opposing-team Attack/Wait fixture,
not direct cockpit controls or human PvP. The five-minute shared-team repeat is
running. Shield-cache getter semantics remain under investigation: the native
getter can replace a fully recharged value/timestamp cache while returning the
same current charge.

The shield difference in commands-pvp002 represents two lazy caches of exactly the
same current1955 shield charge. At time52.966333333331626, the second cache holds
1725 at36.032999999999255, delay15 and rate120, reaching full charge at
52.94966666666592. Native79ddb0 returns the maximum and may materialize value=max,
changed=0 via79ef60, depending on its query time relative to the caller's TLS clock.
An opt-in --shield-refresh calls the original SHA-pinned GetShieldPercent using
the owning simulation clock before reading the strict cache. It copies no values
between clients and retains exact raw comparison. Guard/Lua tests pass; native
acceptance for this option is pending.

The first shared-team command run stops at241 because MD rejects modulo on a
Lua-supplied floating-point actor number. Explicit actor comparisons replace
modulo, and the runner/validator now reject any MD error from either fixture
script. The corrected shared-team run is in progress.27 Python/LuaJIT tests and
native C checks pass. No co-op result is accepted yet.

A separate --command-plan maneuvers adds bounded MoveWait destinations and
ProtectShip orders, followed by Wait/Attack, for both ownership layouts. Lua
validates the complete batch before enqueueing, and MD checks ownership, friendly
escort/enemy attack targets and the returned native order type. Native default
movement/escort scripts remain responsible for flight.28 Python/LuaJIT tests and
the native MD schema pass; the first movement/escort native run is pending.

`commands-coop-002` independently **PASSES**18000 native frames (five simulated
minutes), with18001 complete samples and all8 native Attack/Wait orders applied
at identical game times. BATTLE_SUBSET_4/input hashes match exactly; the3.047-second
hold has zero clock drift. All owned hooks, lanes and snapshot staging restore.
This validates the bounded scripted shared-team command fixture only. It does
not establish interactive cockpit control or equality outside the declared subset.
The movement/escort fixture and opt-in shield refresh are now under native test.

`maneuvers-coop-001` independently **PASSES**3600 frames, all8 native orders,
and a3.031-second zero-drift hold with full cleanup. Both clients create the
MoveWait/ProtectShip/Wait/Attack sequence at the same game times. Exact checked
state remains equal through1636 bullet creations and2 ship deaths/wrecks.
The opt-in native shield refresh passes its per-alive-sample count/failure audit.
Order creation plus matching motion is bounded evidence; native queue state was
not yet part of this revision's hash.

An opt-in --order-state now extends the canonical scope to BATTLE_SUBSET_5:
exported GetOrders3/GetDefaultOrder/GetOrderQueueCurrentIdx queries contribute
queue positions, definition/state strings, parameter counts and execution flags.
Process-local order IDs and localized display names are excluded. Queue length,
text size/delimiters and read completeness fail closed. This does not hash order
parameter values or the full AI stack.32 Python/LuaJIT checks pass; the first
opposing-team maneuver run with this stronger scope is in progress.

`maneuvers-pvp-001` independently **PASSES**3600 frames under BATTLE_SUBSET_5,
all8 native orders and a3.063-second hold, with owned restoration. Native queue
state remains identical through1699 bullet creations and2 deaths.

An opt-in live command path now accepts sequenced local input, which each client
captures and submits through the existing immutable native barrier. Duplicate
requests cannot replace a command; lost deliveries are retried. The validator
links external submission, owning-client capture, barrier proposal and native
application.36 Python/LuaJIT checks pass.
`live-pvp-001` captures and applies all8 external orders identically across games,
including commands captured on different frames324/325. It **FAILS**at3040:
actor1's velocity/position differs while both use the native spline flight model.
Order state, health and existing AI/Agent/weapon traces agree. The failure evidence
and cleanup validate, but checked state is unequal. Spline controller internals
were outside the old steering observer; targeted instrumentation is next. No live
input gameplay acceptance is claimed from this failed run.

A separate --spline-diag observer now brackets the exact-build-pinned native spline
Update(vtable2c4f4f0,calleeeb7230). It records arguments and before/after model,
slot and parameter bytes for registered fixture Agents only. Original arguments,
RNG and scheduling are unchanged; pointer installation/restoration use ownership
CAS, and read failures stop the fixture. Raw model pointers remain private
trace data, outside the canonical hash. A replay plan preserves the failed run's
exact input proposal frames, including distinct324/325 client captures. Native C
and37 Python/LuaJIT checks pass; the first replay/observer run is in progress.

`spline-replay-001` reproduces actor1's movement mismatch at3036 with identical
semantic slot input, dt, cheap flag and movement parameters. Spline obstacle cache
publication times differ(57.033 versus57.483) and its avoidance speed factor is1
versus0.8091763258 before movement separates. The same native geometry helper used
by steering also serves spline, using cache timestamp+108 and vector+f0.

Opt-in --spline-refresh invalidates that timestamp before each full native spline
update. It retains native contact membership/avoidance calculations and copies no
state between clients. The helper and caller are exact-build pinned; counters and
owned restoration are audited. `spline-replay-002` independently **PASSES**3600
frames with all8 replayed orders,5387 native refreshes/0failures,3.016-second hold
and complete cleanup. The four spline progress/speed fields match for all5387
recorded updates, including before/after each call.

Optional --spline-state adds those four semantic fields to BATTLE_SUBSET_6, so a
future limit divergence fails before it changes pose. It does not hash pointer
storage, publication timestamps or the entire spline controller. Native C and38
Python/LuaJIT checks pass; a fresh live-input opposing-team run under this stronger
scope is in progress. General Gravidar membership and path-result scheduling
remain outside the bounded acceptance.

`live-pvp-002` independently **PASSES**3600 frames under BATTLE_SUBSET_6, with
all8 externally submitted commands captured by their owning real client and
applied at matching native times in both games. Capture frames46,166,325,429
are newly observed rather than replayed.6865 spline refreshes and36574 shield
refreshes have0failures;1483 bullets and3deaths occur with exact checked state.
The3.062-second hold and all owned restoration pass. This accepts the bounded
local live tactical-input opposing-team fixture, not direct cockpit control or
human PvP. The five-minute equivalent shared-team run is in progress.

A new travel command plan uses the native MoveWait order with boost/travel left
enabled, two distant destinations, escort, a later destination change and Wait.
Opcode5 is available to live input as well. Native MD telemetry samples actual
travel-active/charging flags, speed, normal maximum speed, boost charge and
attention. The validator requires complete matching samples and actual travel
activation by both controlled leaders; accepting the order alone is insufficient.
40 Python/LuaJIT checks and the native MD schema pass. Native travel acceptance
is pending; this does not yet test highway/sector transitions or cockpit travel.

`live-coop-001` **FAILS**at14523 despite all8 external commands applying at matching
times. Failure evidence and full cleanup validate; this is not an accepted
five-minute live co-op result. Actor1's spline progress/movement diverges at
attention8, while its avoidance limit remains1 in both clients. This is a
separate unresolved case after the earlier cache fix. Thirty-frame diagnostic
sampling omitted the exact failing update. The observer now retains its captured
arrays after owned restoration and dumps every actor's last spline call on STOP,
without reading live engine objects. Native C and41 Python/LuaJIT checks pass.
An exact co-op command replay is in progress; the farther travel test is prepared
but has not run.

`travel-coop-001` independently **PASSES**3600 frames with8 native commands,
matching travel telemetry,3.062-second hold and complete owned restoration.
Both controlled leaders1/2 actually enter native travel mode; actors7/8 also do.
The first validator attempt rejected attention because it incorrectly expected
a number. The native MD API returns common.xsd's named attention enum. The parser
now validates that exact enum and still compares every recorded field between
clients. Revalidation of the unchanged evidence has0errors;42Python/LuaJIT tests
pass. This accepts the bounded shared-team travel/escort fixture, not highway,
sector transitions, direct cockpit control or general travel in other worlds.

`coop-replay-001` independently **PASSES**18000 frames,8 replayed orders and a
3.063-second hold with full cleanup. Both clients match the failed live run
through14522 and both take its client1 result at14523. The earlier live-coop
failure remains open: replay success does not resolve a timing-dependent fault.

The spline observer additionally records pending request count and completion
status/flags under the native result critical section. It does not change the
request or its scheduling. An inclusive --trace-window can retain every update
around a suspect transition while leaving sparse diagnostics elsewhere. Native C
and42Python/LuaJIT checks pass. `coop-replay-002` uses the same recorded inputs
and traces every update14490..14560; native acceptance is pending.

`coop-replay-002` independently **PASSES**18000 frames,8 orders,3.062-second hold
and full restoration. Its dense trace shows actor1's provisional route pending
at14522, then completed and consumed at14523 in both clients. Consumption resets
spline progress. The earlier failed client's additional provisional movement is
consistent with this asynchronous readiness choice, but that failed run did not
record the request flag itself.

Opt-in --spline-join now waits at most1second for the registered fixture's pending
native request before its original spline update. The existing native calculator
produces the route and native update consumes it; no result is copied, replaced,
cancelled or calculated by the extension. Producer, solver, enqueue and request
creation code are pinned. An unavailable worker, invalid queue or timeout fails
the fixture. The native test exercises real delayed publication, immediate ready
results, timeout and unavailable-worker refusal. Native C and42Python/LuaJIT
checks pass. The exact co-op input replay with this option is running; native
acceptance and a fresh live co-op repeat remain pending.

`coop-replay-003` with --spline-join independently **PASSES**18000 frames,8 native
orders,3.047-second hold and complete cleanup. Each client joins20 pending native
requests, waits for16 unfinished results and records0timeouts. The validator now
requires equal joined-request counts across clients while permitting different
wall-time wait counts;43Python/LuaJIT checks pass. The changed result-admission
timing changes subsequent native movement relative to the old run, with exact
checked equality between the two current clients. A fresh live-input co-op run
is in progress. General asynchronous world queries remain outside this result.

`live-coop-002` independently **PASSES**18000 frames with8 fresh external inputs,
22 joined native route requests/16 waits/0timeouts per client,3.063-second hold
and full restoration. Capture frames are56,197,361/360,465; commands captured on
different client frames still apply in the same admitted batches. This accepts
the bounded five-minute-equivalent live tactical co-op fixture after the spline
join change. It is not human cockpit or whole-world multiplayer acceptance.

A fleet plan adds native AssignCommander orders for defence assignment,
reassignment and removal, plus movement and Wait. Native order code performs the
assignment and default-order/cancellation behavior. BATTLE_SUBSET_7 includes each
live ship's commander mapped to fixture identity, assignment and subordinate
group; foreign identities or malformed data fail closed. The validator requires
each requested fleet change to appear before that actor's next command, beyond
merely accepting its order. Fleet commands require this stronger state capture.
47Python/LuaJIT checks and the native MD schema pass. Native fleet testing is
pending; these source checks do not establish fleet gameplay acceptance.

`fleet-coop-001` **FAILS**at1273 with valid failure evidence and complete cleanup.
All8 commands admitted before the stop apply consistently, and all6 requested
fleet changes so far are observed. Both clients join15 spline requests with7
waits/0timeouts. Actor3's pose, health and fleet identity still agree; only its
native order queue differs: one client has a newly queued Attack while both
retain Patrol as the default order. AI execution agrees through1263 in
move.seekenemies; only client1 resumes the patrol path at1273. Radar-scan event
publication is a candidate trigger, not yet a proven cause. Fleet acceptance
remains open; the earlier live co-op and travel results retain their bounds.

`travel-pvp-001` independently **PASSES**3600 frames,8 native commands, matching
travel telemetry,3.062-second hold and complete cleanup. Leaders1/7 both enter
native travel. Each client joins14 spline requests with10 waits and0timeouts.
This accepts the bounded opposing-team travel/escort fixture with the native
spline join; human cockpit, highway and sector transitions remain untested.

Opt-in --radar-diag observes GravidarHasScanned notifications delivered to
registered fixture AI contexts, with frame, ordinal, fixture source and script.
The original native handler receives every call once with unchanged arguments;
no scheduling or state changes are made. Full handler SHA and vtable pointer
are pinned; overflow, foreign-thread delivery or malformed fixture data fails
closed. The validator requires a complete event sequence and clean restoration.
Native C and48Python/LuaJIT checks pass. Fleet replay002 is running to test the
event-timing hypothesis; this observer is not a synchronization fix.

`fleet-coop-002` **FAILS**at1258 with valid failure evidence and full cleanup.
The read-only observer records a GravidarHasScanned delivery for actor4 in
move.seekenemies only on client2 at1258. Its queued Attack is the sole checked
state difference; pose, health and fleet state still match. Client1 stops before
its first eligible radar event. The validator now permits this truthful zero
count on a failed run while still requiring complete sequences/restoration and
at least one exercised event across a passing pair.48checks pass. Native radar
completion/publication and randomized scan deadlines are under investigation.

Opt-in --radar-serial moves the original MoveWorker radar publication from
before its admission wait to the admitted frame before pathing. It waits at
most1second for both the prior pass and the newly awakened native pass; worker
state and its critical-section-protected completion flag are checked. Native
publication executes once per admitted frame. Native contact calculation and
event payloads are unchanged. A fixture unit observer records scan deadlines,
notification time and RNG without changing them. Both patch sites use owned CAS
restoration and exact-build pins. The native test exercises delayed completion,
timeout, unavailable/reset worker refusal and partial ownership restoration.
Native C and49Python/LuaJIT checks pass. Fleet-coop003 is running; this timing
experiment has no native acceptance yet.

`fleet-coop-003` **FAILS**at1267 with valid evidence and cleanup. Both clients
complete1267 radar phase joins with0timeouts. Their registered native scan calls
differ224/230. Early traces show unequal initial scan/notification timestamps
and worker RNG, then unequal refresh deadlines. Onlyclient1 receives the radar
notification at1267; the queued Attack is again the only checked difference.
The ordering change alone is insufficient.

Opt-in --radar-rng scopes actor/frame/call RNG around the unchanged native scan
application and restores the prior worker stream. --radar-initial-refresh aligns
three transient scan/notification timestamps once for each registered fixture
unit while the worker is parked; existing contact lists remain intact. Native
lookup and unit identity are pinned/checked under native locks. Replacement
units fail closed. Native tests verify once-only initialization and RNG scope;
49Python/LuaJIT checks pass. Fleet-coop004 is running; acceptance is pending.

`fleet-coop-004` **FAILS**at3571 with valid evidence and full cleanup. All14
commands and all10 requested native fleet changes match. Both clients complete
3571 radar phase joins,657 bit-identical native scan records and2 identical
radar notifications;0timeouts. The new mismatch is actor12 shield damage with
matching movement, orders and radar. Bullet records around3568..3571 match.
Fleet acceptance remains open. The validator now checks complete scan records
and equal scan/event timelines for a passing radar-enabled pair. Unit traces
are emitted on every update even with sparse general diagnostics.

`fleet-pvp-001` is an invalid scenario: both clients refuse the same MoveWait
command at211 because leader7 is already destroyed. The plan now moves that
leader at31, before assembling its wing. This is not evidence of a lockstep
divergence or a fleet pass. Native absorption tracing (--damage-diag) records
fixture shield amount/time/result/cache and caller RVAs with unchanged native
arguments and return value. Full function SHA, group identity, bounded events
and owned restoration are checked. Native C and51Python/LuaJIT tests pass; a
co-op replay with this read-only observer is the next native check.
`fleet-coop-005` independently **PASSES**3600 frames,14commands and10actual
fleet changes, with384 matching damage events and662 bit-identical scan
records. Both clients complete3600 radar phase joins with0timeouts, and the
3.047-second hold and cleanup pass. The validator now compares native damage
inputs/results, excluding caller addresses.51Python/LuaJIT checks pass.
This run does not explain the earlier shield mismatch: its native steering
trajectory already differs from run004 at2576, while each pair still agrees.

`fleet-pvp-002` **FAILS**at2254 with valid evidence and full cleanup. Its10
commands and8actual fleet changes match. Damage and radar timelines also match,
including a23-point shield hit on commander1 in the divergent frame. Only
actor5's queue differs: client2 adds Attack while both retain Escort. Its
scheduled interpreter starts at the same PC368 with the same seeded RNG;
onlyclient2 consumes RNG. Native event/interrupt handling is under investigation.

Opt-in --interrupt-diag observes native fixture HandleScriptEvent and the
separate interrupt interpreter entry, including nested include-action calls.
It preserves event delivery, arguments, RNG and return values. Exact full
function pins, bounded trace, thread checks and owned CAS restoration guard
both sites; existing AI-driver/fixedtick hooks must be absent. Native C tests
and53Python/LuaJIT checks pass, including overflow, argument/result preservation,
foreign-patch refusal and final trace draining. PvP replay003 is in progress;
this instrumentation changes no gameplay decisions.

`fleet-pvp-003` reproduces **FAIL2254** with valid evidence, no validation
errors and complete restoration. Both actor5 contexts receive event497 and
enter interrupt range237..368, but their incoming interrupt RNG states differ.
One queues Attack; the other does not. Earlier interrupt RNG differs from22.
Damage/radar still match. Opt-in --interrupt-rng now scopes this separate native
interpreter using actor/frame/script/range/mode/call identity, including nested
include actions, restoring the caller stream. No event is suppressed or order
fabricated. Native tests verify caller RNG preservation and stable isolated
results;54Python/LuaJIT checks pass. PvP004 is running with this change.

`fleet-pvp-004` crosses2254 with matching checked state and matching interrupt
execution/RNG. At2731 both clients reject the same final Wait command because
its actor7 is destroyed. This is an invalid scenario, not a completed pass.
The final Wait now targets surviving wing member9; commander movement and all
fleet assignment/reassignment/removal commands remain exercised. Co-op006 is
running with the interrupt RNG scope while the corrected PvP replay is prepared.

`fleet-coop-006` independently **PASSES**3600 frames with14commands,10actual
fleet changes,278 matching damage events,638 bit-identical radar scans and
matching native interrupt execution/RNG. Each client records573 event/interpreter
trace entries,3600radar phase joins and0timeouts. Hold3.062seconds; restoration
and cleanup pass. This accepts the bounded co-op fleet fixture at the current
interrupt-scope revision, not the unresolved older shield failure or all gameplay.

`fleet-pvp-005` independently **PASSES**3600 frames with14commands,10actual
fleet changes,286 matching damage events,662 bit-identical radar scans and
equal native interrupt execution/RNG. Each client records621 event/interpreter
trace entries,3600radar phase joins and0timeouts. Hold3.063seconds; restoration
and cleanup pass. This accepts the bounded fleet PvP plan after its final Wait
was moved to surviving actor9. Long fleet-coop007 targets18000 frames, with
complete canonical/event traces and verbose diagnostics every30frames.

`fleet-coop-007` completes18000 frames on both games with owned hooks restored,
504damage events and2483radar scans each. Its relay records completion, then
raises Windows10054 during recvfrom after clients begin closing; the runner
correctly records FAIL. This is not yet an accepted clean long run. Windows
reports a previous UDP send to a closed destination through this error
([Microsoft recvfrom documentation](https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-recvfrom)).
The relay now tolerates ConnectionResetError on receive/send, continuing to
serve the other peer. It cannot admit a frame without both reports; unrelated
socket errors still propagate.56Python/LuaJIT checks pass, including a reset
before reports and during a STOP send. A clean native repeat is required.

The validator now exposes `session_pass`, requiring valid evidence, equal
checked state and runner success with exit code zero. `evidence_valid` retains
its separate meaning for usable failure evidence. Revalidating co-op006 yields
`session_pass=true`; co-op007 yields false despite matching checked gameplay.
The native repeat co-op008 uses the exact same frozen game overlay and snapshot
with the corrected relay. Fresh live-input and travel overlays are prepared.

`fleet-coop-008` independently **PASSES the complete session** at18,000 frames
using the same game overlay as007 and the corrected relay. All14commands and
10actual fleet changes match, as do504damage events,2483radar scans,17radar
notifications and native interrupt execution/RNG. Each client completes18,000
radar joins with0timeouts; hold3.031seconds, owned restoration and lane cleanup
pass. Fresh live-pvp003 has completed3600frames with8external inputs captured
at49,175,305,444 on both clients; independent validation is pending.

`live-pvp-003` independently passes the complete 3,600-frame session with eight
fresh external commands, captured at frames 49, 175, 305 and 444 on each client.
Native command application, checked state, 215 damage events, radar scans and
interrupt execution/RNG match. Hold: 3.047 seconds; restoration and cleanup pass.
No AI radar notification was delivered in this direct-combat scenario. The
validator now records that coverage as `radar_event_delivery_exercised=false`
rather than requiring every scenario to subscribe to notifications. Passing
fleet-change scenarios still require that delivery path to be exercised;
complete and equal radar scan/event records remain mandatory when enabled.
This run does not add radar notification coverage. All 56 focused checks pass.

`travel-coop-002` independently passes the complete 3,600-frame session with
eight native commands. Leaders 1 and 2 enter native travel (actors 7 and 8 also
travel); actual telemetry, checked state, 177 damage events, radar scans and
interrupt execution/RNG match. Hold: 3.062 seconds; restoration and cleanup pass.
Radar notification delivery is unexercised here too. PvP travel002 is running.

`travel-pvp-002` independently passes the complete 3,600-frame session: eight
commands, actual travel on leaders 1 and 7, 226 matching damage events, equal
radar scans and native interrupt execution/RNG. Hold: 3.063 seconds; restoration
and cleanup pass. Native radar notifications are unexercised. Fresh live-coop003
is running for 18,000 frames with eight external inputs captured at 53, 174, 345
and 434 on both clients.

A new withdrawal plan uses native MoveWait with its existing withdraw parameter,
which invokes X4's boost retreat script. Opcode 8 requires owned actors and boost
telemetry. Acceptance requires actual active boost, speed above normal maximum,
charge consumption and matching complete sampled telemetry for both leaders;
order creation alone is insufficient. The native MD schema and 58 focused Python/LuaJIT checks pass; native withdrawal
acceptance is pending.

`live-coop-003` independently passes the complete 18,000-frame session with
eight fresh external inputs. All commands apply; checked state, 404 damage
events, 2,134 radar scans and native interrupt execution/RNG match. No radar
notification was delivered in this scenario. Hold: 3.062 seconds; all hooks
restore and both disposable lanes clean up.

`withdraw-coop-001` and `withdraw-pvp-001` independently pass 3,600 frames each,
including all eight commands and actual native boost by both commanded leaders
(1/2 and 1/7 respectively). Complete sampled boost telemetry, checked native
state, damage inputs/results (245/231 events), radar scans and interrupt execution
match. Neither run delivered a radar notification. Holds: 3.062/3.063 seconds;
hooks restore and both pairs clean up.

The first boost validator incorrectly treated the native `maxspeed` property as
a fixed ordinary-flight limit: it rises during active boost. The corrected
validator requires speed above each ship's inactive frame-30 native baseline,
measured before withdrawal applies at frames 31/46, along with active boost and
charge consumption. Without that baseline it cannot claim boost coverage. A
regression test covers a boosted speed below the contemporaneous boost ceiling
but above the ordinary-flight baseline. All 58 focused checks pass. The native
runs and equality checks are unchanged; the validator now tests the intended
physical behavior rather than requiring speed above the active boost ceiling.

`fleet-pvp-006` independently passes 18,000 frames: 14 commands, 10 actual fleet
changes, 486 matching damage events and 16 matching radar notifications, with
equal scan timing and native interrupt execution/RNG. The deliberate hold lasts
3.047 seconds. All owned hooks restore and both lanes clean up. This extends
the current PvP fleet result to the same duration as co-op; it does not establish
whole-world or human cockpit lockstep.

The opt-in docking extension adds a native `DockAt` command, two disposable
single-module station anchors and `BATTLE_SUBSET_8`. It hashes native docking,
docking-in-progress and internal-storage flags, containing station identity,
station pose/health and canonical docked-ship populations each frame. Both
anchors remain represented even if their corresponding fixture actor dies.
Acceptance requires actual attachment and station occupancy before the next
order, then detachment and at least 100 metres of native movement after a
departure order. Stock station assets and stock AI perform docking; the new
construction plan is original test data, with no copied engine scripts. Native
MD schema validation passes. Native docking acceptance is pending.

The first docking run, `docking-coop-001`, reaches matching native approaches
but both readers refuse frame 1,156. Diagnostic repeat `docking-coop-002`
identifies actor 1's controller transition from NavigationController to
DockMovementController (vtable RVA 0x2c3fdc0). Neither run passes.

The read-only dock controller reader pins its native constructor and transform
evaluator, checks the owning fixture component, and captures exact bits for
three poses, initial linear/angular velocity, start time, animation selector
and two options. It excludes vector padding and process-local pointers. The
canonical dock representation explicitly replaces absent navigation buffers;
existing native physics, health, order and occupancy checks remain active.
Unknown controllers still refuse, and dock-controller state is accepted only
with the opt-in docking fixture. Native C tests and 64 focused Python/LuaJIT
checks pass, including padding exclusion, invalid-state rejection and timing/
controller-state changes entering the hash. Native repeat is pending.

`docking-coop-003` crosses the first refusal and both ships actually attach to
their respective stations with matching occupancy. Both stop at frame 1,718
when actor 1 switches to UndockMovementController (vtable RVA 0x2c3e568), which
the first reader did not support. No complete docking session is accepted yet.
The additional read-only reader pins its native transform evaluator and hashes
two poses, start time, animation selector and option flag.

The one-shot DockAt order completed and resumed the queued attack, explaining
the early departure. Opcode 9 now creates native DockAndWait. The next runs check whether it holds
the berth until the next admitted movement command. Validation additionally requires
attachment and occupancy immediately before that command: an automatic earlier
undocking cannot count as a commanded departure. Native C and all 64 focused
checks pass; the revised native scenario remains pending.

`docking-coop-004`, `005` and `006` all fail with valid negative evidence at
frames 2,686, 2,295 and 2,973 respectively. Run 004 first differs in actor 1's
navigation coordinate frame and native physics pose/velocity; run 006 differs
in actor 1's physics velocity/collision feedback. Their damage, radar and native
interrupt traces match. In run 005, actor 2 receives the arrived-at-waypoint
script event on only one client at the first differing frame, advancing Undock
to Attack while the other client retains Undock. Pose and physics still match
at that point. These causes are not resolved and none is a passing session.

DockAndWait alone still resumes the old queued Attack at its infinite-order
sync point. The command now cancels that old queue using the native MD action
before creating DockAndWait. All 64 focused checks and the MD schema pass.
`docking-coop-007` tests this correction; acceptance still requires actual
attachment through the admitted departure command and subsequent movement.
Changing the fixture does not establish a fix for the preceding divergences.

`docking-coop-007` confirms that both ships remain attached until their departure
commands, then undock. It fails at frame 6,882 with actor 1 physics velocity
feedback differences. Diagnostic repeat `008` fails at frame 7,178. Both retain
matching damage, radar and native interrupt traces and valid negative evidence.
The departures had not yet reached the required 100 metres from the last attached
position, so neither run earns a completed departure or a passing session.

Run 008's full trace shows equal native steering/spline inputs and outputs,
navigation refresh times and deadlines at the failed frame. The velocities fed
back from native physics already differ before NavigationController receives
them. This narrows the investigation to physics inputs/results; it does not
prove a solver defect. Read-only navigation diagnostics now include both state
buffer tails and refresh metadata, excluded from portable hashes because they
contain local references and padding.

An opt-in contact observer records native collision manifolds involving fixture
ships, both body poses and velocities, and contact settings before/after X4's
original callback. The callback remains authoritative and runs exactly once.
The complete native helper is pinned before an owned, reversible entry detour;
foreign patches and unsupported builds refuse. The observer excludes unused
contact capacity and vector storage lanes, bounds its buffer, and fails closed
on incomplete capture. Independent validation requires complete ordered traces
and restoration, and checks physical input/settings equality on positive runs.
Local body/component IDs are excluded from comparisons. Native tests cover
argument forwarding, static bodies, identity reuse, unchanged inputs, invalid
counts and owned restoration; all 66 focused Python/LuaJIT checks pass. Native
contact evidence is pending in `docking-coop-009` at that revision.

Runs `docking-coop-009` and `010` fail at frames 7,181 and 6,890. The contact
observer finds different contact geometry already at frame 1,792 in run 009,
while the participating body poses and velocities still match. Run 010 adds
read-only shape-deadline metadata: the dock module's deadline differs at the
first contact, frame 1,143. Damage and interrupt traces continue to match.

The next opt-in experiment registers the two fixture DockArea modules through
their native station parent chains and identities, then extends the existing
shape-refresh RNG/deadline control to their main and secondary records. The
native refresh function and both helper functions receive full-function pins.
Run `011` completes 18,000 matching checked-state frames, but independent
validation rejects it: 53,473 contact records per client differ, and neither
client completes any of the four required departures. Four actual dockings per
client do occur. Clock/barrier success is therefore not docking acceptance.

Identity diagnostic run `012` completes 3,600 matching checked-state frames but
also fails contact equality (5,454 records per client). The actual collision
body belongs to the same registered DockArea component but is outside its two
standard body slots. The direct records' refresh traces match; the additional
record was not covered. This is a specific scope gap, not evidence that the
native physics solver itself is nondeterministic.

The pending correction resolves additional records through their live native
physics scene and BodyID, verifies the body's userdata points back to the
record, and assigns a stable scene/object-layer role within each registered
dock module. Unknown scenes/layers, duplicate roles and reused body identities
refuse the fixture. Native refresh still runs once, the caller RNG is restored,
and shared shape holders receive only one initial deadline. No local address
or BodyID enters the seed. Additional records and their initialization counts
are explicitly reported and required by independent validation.

Native C checks and all 68 focused Python/LuaJIT checks pass. The MD schema
passed after adding collider registration. Live run `013` is testing the full
18,000-frame docking/departure scenario; its initial 2,450 contact records and
1,590 common additional-body refresh samples match. That partial observation
does not establish a passing docking session or resolve the earlier departure
failure. General station geometry and the full universe remain outside scope.

Run `013` subsequently completes 18,000 matching checked-state frames. All
53,473 contacts per client have identical physical inputs/settings when compared
as a multiset within each frame. However, callback order differs in the final
79 frames, beginning at 17,922: physical and sensor contacts against the dock's
main and additional records exchange order. Independent validation retains its
ordered-equality failure; the diagnostic multiset result does not establish
that callback order is harmless. The ships also complete four dockings but zero
accepted 100-metre departures per client. This is still a failed docking session.

Both additional dock records execute 35,988 refresh calls in total per client,
with matching initialization coverage and clean restoration. Ship shape-holder
initialization totals differ (37 versus 35 overall), despite matching sampled
gameplay state. Pointer reuse during wreck creation is a possible lifetime gap
requiring investigation, not a proven explanation or an accepted invariant.

Private diagnostic run `014` retains the same controls and acceptance checks,
changing only departure destinations to one kilometre above their own anchors
and enabling native MoveWait debug output. It tests whether the original route
is responsible for the failed movement. Its outcome cannot erase the original
route failure. The added validator multiset diagnostic has regression coverage
for callback order, frame, physical values and multiplicity; all 69 focused
Python/LuaJIT checks pass. Ordered contact acceptance remains unchanged.

Run `014` completes all four commanded dockings and departures per client with
18,000 matching checked-state frames. The first departures pass 100 metres at
the same ticks (6,644 and 6,656), reaching about 940 metres before the queued
DockAndWait resumes. Damage (356 records per client) and interrupt RNG match.
This establishes functional movement for that route, but the session still
fails contact equality: 33,548 records per client, different shape deadlines
from the first contact at 1,143, and actual contact values differing in 167
frames even after excluding deadline metadata and callback order. An upward
route therefore does not by itself fix synchronization.

The public test builder now supports explicit `--docking-exit above`; the
original `diagonal` default and its failure remain available. The choice is
validated before commands are emitted and carried in both clients' admitted
MD actions. The source MD schema and 70 focused Python/LuaJIT checks pass.
`docking-pvp-001` is exercising this reproducible variant without private
MoveWait debug output; independent acceptance is pending.

The next optional experiment, `--docking-shape-refresh`, gives the two registered
dock modules a fixed refresh cadence. The native interval calculation depends
on distance and shape bounds in addition to RNG, and run 014's deadlines already
differ at the first sampled frame 30. Asset readiness is a hypothesis, not a
proven cause. The experiment makes each owned native shape call refresh normally,
records its calculated deadline, then applies the simulation time as the next
due time. It changes scheduling, without copying geometry or changing contact
settings. All identity checks, full-function pins and original-once/caller-RNG
restoration remain in effect. Other components retain their existing behavior.

The new flag is off by default and requires dock shape identity/RNG control.
Validation requires forced-refresh coverage of every registered dock-body call.
Native tests verify both deadlines, original arguments/call count, RNG restoration,
and exclusion of ships and the disabled option. Native C and 71 focused checks
pass; a new co-op run `015` is prepared to test the cadence. No native result is
claimed for that experiment yet.


`docking-pvp-001` completes 18,000 checked frames and four actual dockings and
100-metre departures per client, with 432 matching damage records and matching
interrupt RNG. Its 32,328 contact records differ both in order and within-frame
multisets. Independent acceptance fails solely on contact equality.

Fixed-cadence `docking-coop-015` completes the same functional checks and 18,000
matching checked-state frames. All 33,548 contact records per client now match
as multisets within each frame, including geometry, settings and deadlines.
The 356 damage records and interrupt RNG match. Strict acceptance still fails:
callback order differs beginning at frame 7,364, and radar notification timing
differs beginning at frame 12,457. At that first radar difference, scan time,
next scan deadline and RNG agree; only the last notification time differs.
The native notification path depends on contact-set changes or a ten-second
refresh interval, so the underlying result sets need direct observation.

Of 1,786 common early dock-shape samples through frame 400, 24 native deadline
and RNG results differ at frames 7 through 10; every applied deadline agrees.
The native interval has a no-RNG fallback for degenerate bounds. This narrows
the readiness investigation but does not prove an asset-loader root cause.
PvP repeat `002` is testing the fixed cadence. No passing docking session is
claimed, and ordered-contact/radar acceptance checks remain unchanged.


The existing opt-in radar hook now captures bounded, read-only snapshots of
old, incoming, applied and newly added contact sets. Unit vectors are read under
the native lock; no contacts or notifications are filtered, copied between
clients or synthesized. Raw IDs remain local diagnostics, with known fixture
actors labelled separately. Overflow, malformed vectors and missing trace
stages fail validation. The full native apply-function pin remains required.
Native C checks and 72 focused Python/LuaJIT checks pass; live observation is
pending. Existing ordered-contact and radar-state equality gates remain strict.


Fixed-cadence PvP `002` completes 18,000 matching checked-state frames and four
dockings/departures per client. All 32,328 contacts/client match as per-frame
multisets, and all 432 damage records match. Strict acceptance fails on callback
order, radar scan state and interrupt execution/RNG (724 records per client).
Both workers join every frame without timeout and restore their hooks. Equal
visible/checked movement therefore still does not establish shared native AI.


Co-op repeat `016` completes 18,000 matching checked-state frames, all four
actual dockings/departures per client, and matching damage and interrupt RNG.
This time the complete contact traces match in order as well as by per-frame
multiset. Independent validation still fails solely on radar state. The first
difference is at frame 15,306: one client receives 17 primary contacts versus
16, with an additional object outside the twelve registered ships. Its native
added-contact set contains that object, explaining the notification update.
The other client's contact set is unchanged. This directly identifies differing
native scan input; it does not yet identify the object's gameplay role or cause.
Local runtime IDs cannot be assumed identical to save-file IDs after loading.

PvP `002` has a separate earlier interrupt RNG difference at frame 9,993:
actor 2, orders.base, range 0..45, with equal initial RNG but 11 versus 20 native
advances. This precedes its contact-order and radar differences. The existing
opt-in interrupt observer now also traces each native instruction's class,
action, program counter, flags and RNG under the fixture interpreter bracket.
It invokes the original instruction once and does not change its decisions.
The complete 164-byte instruction runner is pinned; capture is bounded, tied to
its parent execution and restored with ownership checks. Cross-client instruction
equality is required for positive runs using the new diagnostic revision.

Radar observations also identify previously unseen outside objects through
read-only macro, owner and player-role queries. Full 64-bit IDs are preserved;
the metadata is diagnostic and never enters a portable state hash. Native C
checks and all 74 focused Python/LuaJIT checks pass. Native validation of the
instruction observer and metadata remains pending in the next PvP repeat.


The first live metadata query in PvP `003` returns empty macro/owner values even
for the known player ship. The native player-role comparison works, exposing a
UI-ID conversion error in the observer. Queries now use the same decimal-string
to Lua-ID conversion as the existing weapon reader. The regression check covers
that API boundary and exact 64-bit identities; all 74 focused checks pass.
The running frozen test is retained for instruction evidence; object names from
that revision remain unavailable and are not treated as identification proof.


PvP `003` stops at frame 13,843 on a real gameplay-state mismatch. Independent
validation confirms valid negative evidence: ordered contacts match, but actor 8
receives three additional native damage callbacks in client 2. Its shields reach
zero and hull also changes; client 1 receives no damage callbacks that frame.
Client 2 had three extra outside objects in radar input from frame 11,306 and
reports untracked/outside projectile activity. The causal source still needs
identification, but this is more than a radar-notification discrepancy.
Both clients complete four dockings and the two departures scheduled before the
failure. The later departure commands are not reached. All owned hooks restore.

The new instruction observer captures complete native evidence. The earlier
frame-9,993 RNG divergence does not recur: both clients execute the same NPC
combat-chatter handler and consume 11 native RNG advances. The first differing
instruction/event in this repeat follows the additional damage at the failed
frame. This localizes the old observation without proving it harmless or fixed.
Outside-object metadata now also records the native ID code for cross-checking
persistent identities; all 74 focused checks pass. The next co-op test uses the
corrected UI-ID lookup and native ID codes.


Live co-op `017` exposes a second metadata API mistake: Lua's string replacement
returns both the string and replacement count, so the conversion call received
two arguments. Parenthesizing the expression now supplies exactly one; the
regression test enforces the native API's arity. This affects identification
only, not scan results or native gameplay. The separately captured native ID
codes work: the saved player ship has the same code in both clients, while the
newly created test stations have different codes. Fresh native ID codes alone
are therefore unsuitable as shared identities for general object creation.
All 74 focused checks pass after the metadata correction. The frozen run remains
useful for native codes and instruction evidence, but its macro/owner fields
remain unavailable. No live input is edited in place.

Co-op `017` completes 18,000 checked frames and four dockings/four departures
per client. Checked physics/health, all 33,548 ordered contact callbacks, all
356 damage callbacks and radar timing match. Independent acceptance still fails:
the first instruction-level divergence is actor 6 at frame 8,579, inside the
native SignalObjects action in the NPC combat communication handler. The action
starts with equal RNG but returns different RNG; 18 instruction records differ
in total. Matching physical state at this duration does not make that harmless.

The saved fixture also contains duplicate native ID codes, including 27 codes
shared by multiple ship/station/build-storage roots. Display codes cannot be
used as universal identities. Read-only investigation confirms the engine's
UniverseID import helper registers saved-to-runtime mappings for components
and resource areas. The new optional `--world-identities` diagnostic observes
that helper during snapshot loading, calls it once with unchanged arguments and
return value, and captures only successful canonical saved IDs in a bounded
private index. It does not change object IDs, creation, RNG or gameplay.

The diagnostic pins the full native function and owns/restores an aligned entry
patch. Malformed IDs, conflicting runtime mappings, index overflow, native import
failure and patch ownership changes fail closed. Post-load imports remain
unmapped and are counted; newly created objects still need a separate identity
scheme. Per-actor saved identities must be complete, nonzero, unique and equal
between clients, and outside radar objects expose an optional saved ID. Native
C tests and all 76 focused Python/LuaJIT checks pass. Live validation of this
capture is pending; no full-world or playable-lockstep acceptance is claimed.

PvP `004`, with corrected outside-object metadata, fails at frame 16,156 after
four complete dockings and departures per client. Actor 8 retains 35% hull in
client 1 but is destroyed in client 2 by an additional native 1,000-damage hit.
All ordered contacts and all 2,799 common instruction records match before the
extra event. Client 2 has four outside projectile creations and 717 untracked
projectile updates; client 1 has neither. An Argon fighter enters only client 2's
radar at frame 13,205. Its code and macro match one existing ship in the shared
save, an identification inference that the import observer can now verify.
Independent validation accepts the negative evidence, not the session.

The first live identity-capture check in co-op `018` succeeds: both clients report
the same twelve saved fixture IDs, independently matched to the expected six
Argon and six Xenon ship macros in the authorized snapshot. The player ship also
maps to the same saved ID despite different runtime IDs. Newly created stations
and build storage correctly remain unmapped. This validates the sampled lookup
behavior; the run is still active, and final counts, restoration and strict
gameplay acceptance remain pending. No general background RNG control is added.

Co-op `018` subsequently completes and passes independent strict acceptance:
18,000 matching checked frames, ordered contacts, native damage, radar, interrupt
RNG, instruction records and twelve saved fixture identities. Both clients
complete four dockings and four departures. The import observer captures 521,929
mappings with zero failures, no late imports, and clean restoration. This is a
bounded passing docking repeat; earlier communication, callback-order and outside
damage failures remain open.

Following the request for broader hashing, `--world-hash-stride N` adds a native
read-only census of every imported identity at frame 1 and each Nth frame. The
four domains are topology/lifecycle, stored hull/shield state, movement, and
timing. Motion includes verified navigation vectors, reference identities,
physics-mode fields, static/value-source controller transforms, and existing
docking controller readers. Timing includes attention, navigation refresh cursor,
parameter deadline/gate, and pending waypoint/arrival fields in both state banks.
No lazy transform or recharge getters are called by this census.

Every identity/domain receives SHA-256. Domain roots hash the sorted saved IDs
and their object hashes. The probe logs an initial complete inventory followed
by sparse changes in bounded log pages; the independent verifier reconstructs
the inventory and recomputes every root. The root state joins the existing native
barrier digest, so a broader mismatch stops the pair. `compare_native_world.py`
reports the first sample, domains, affected object counts and saved identities.
It reads a bounded view even if the log is still growing.

Coverage is explicit: newly created objects have no imported identity; unknown
controllers, class readers and references are counted. Null-controller connection
transforms are marked uncovered. Unknown nonzero references must not be mistaken
for a verified null or for each other. This is not a complete universe snapshot:
ownership, cargo/economy, AI/MD variables and concurrent publication quiescence
still require additional coverage. Native tests verify pointer-independent saved
references, vector-padding exclusion, separate health/motion/timing changes,
identity refusal and reusable SHA-256. All 79 focused Python/LuaJIT checks pass,
including sparse-delta reconstruction, validly encoded tampering, missing rows,
duplicates, cadence and stop counts. First live broad-hash validation is queued.

The first broad-hash pair now fails closed at frame 1. Independent reconstruction
verifies all four roots without errors: 521,929 imported identities, 426,799
currently present, no unknown classes/references, and 32,607 uncovered motion
controllers on each client. Recorded topology and health match. Timing differs
for 10,008 objects; motion differs for one existing Khaak fighter. This exposes
initial-state differences before the later battle symptoms, but does not yet
establish which fields are causal or whether each timing field is portable.

`--world-hash-watch SAVED_ID` retains canonical bytes for up to 64 selected saved
identities during that same census. These are the exact bytes fed to SHA-256,
not a later memory read. Four bounded detail records per object allow independent
verification against its hashes and metadata, then field-level comparison of
navigation banks, references, attention, refresh cursors and deadlines. Missing
watched identities refuse the capture; malformed, duplicate, incomplete or
tampered details fail independent validation. The comparison retains examples
per domain so numerous timing differences cannot hide a rarer motion difference.

The watched replay also stops at frame 1, with independently verified roots and
all forty watched domain preimages verified on each client. This repeat has only
two timing differences, both on fixture ships; its recorded motion matches. The
earlier broad timing/motion pattern did not reproduce. The ten selected watch
objects themselves match, so that replay cannot identify the two differing fields.

Timing bytes are now retained for every imported object and emitted with each
changed object record. The verifier recombines them with the already recorded
type, flags and saved parent, and checks the existing timing SHA-256. This leaves
the four hash domains unchanged while removing the need to predict which object's
timer will differ. Comparison reports affected field counts and bounded examples;
missing or changed timing bytes fail verification when the installed configuration
requires them. Log pages shrink to twelve rows to retain the existing size bound.
Native C and 83 focused Python/LuaJIT checks pass; this broader detail replay is
pending. No field has been reset or excluded to obtain equality.

The all-object timing replay completes and fails closed at frame 1 with valid
independent evidence. Every recorded timing preimage reproduces its existing
object hash. Exactly two fields differ: the navigation parameter-refresh
deadline on fixture actors 6 and 12. Their refresh cursors, attention, arrival
flags, topology, health and motion match; so do the other imported object hashes.
Both native gates restore, import capture reports zero failures/late imports,
and the world scan reports one completed capture with zero failures.

The fixture registration path has a concrete first-frame gap: the Lua update
queues RestoreActors after native frame 1 has already executed. The per-fixture
RNG/initial-refresh hooks therefore lack actor identities during that frame.
The two affected ships are the outer pair in the saved formation; their distance
from the player is above the native refresh scheduler's first randomization
threshold, while the other ten are below it. This matches the observed field
pattern but does not by itself attribute the earlier 10,008-object mismatch.
An explicit snapshot-manifest prebinding experiment is being developed to bind
the validated fixture before the first admitted native frame. Hashes remain
strict, and save-backed arrival flags are not reset.

The separate holder-lifetime correction and its observers are now integrated.
Native C and 87 focused Python/LuaJIT checks pass together with the new world
diagnostics. The holder repeat's 18,000-frame result and limits are recorded in
`NATIVE-CONTACT-ORDER-20261027.txt`; it does not establish a general callback-order
fix or whole-universe determinism.
