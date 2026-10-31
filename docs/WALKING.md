# On-foot avatars, shared-ship crews and EVA

Status: internal, 26 October 2026. Walking avatars are implemented for N players and
covered by Rust, Python and LuaJIT tests with synthetic peers. **No native run yet**:
whether X4 draws, places and animates the avatars as intended is unverified. The
in-game procedure is [tools/distribution/HUMAN-WALK-TEST.txt](../tools/distribution/HUMAN-WALK-TEST.txt).
Multi-crew and EVA are design notes (sections 4 and 5), with the groundwork they
need already in the protocol.

Source: `src/extension/x4_lan_lab/ui/walk_game.lua` (protocol, interpolation, lifecycle),
`src/extension/x4_lan_lab/md/x4lan_walk.xml` (local pose, avatar actors), relay
`src/x4lan/bubble.py` and `src/rust/x4lan-relay/src/session/bubble.rs`. Config:
`walking_avatars` (source config off; internal session packages on).

## 1. What a client sees

Every client that stands on a station or inside a shared ship publishes its pose.
Every other client in the same place sees one avatar per remote client: an Argon NPC
named "Player K" (K = the client's player number), standing or walking at the remote
position and heading. The avatar is a local MD cue actor; it has no physics or
inventory authority and cannot be talked to (custom conversation handler).

The avatar is hidden when the remote player sits in a pilot seat, is in a spacesuit,
stops sending for 1 s (disconnect, pause, loss), or is in a place the local game cannot
resolve. It is forgotten after 30 s of silence. The dedicated server neither walks nor
draws avatars; it still receives the walk rows (5-10 small rows/s per walking client).

## 2. Protocol: WALK2

Sender (every 0.1 s while moving, every 0.5 s while standing, at once on a change of
place, room, state or transporter count):

```
WALK2|1|lease|seq|state|kind|ship|sector|code|macro|room|x|y|z|yaw|speed|stime|transfers
```

| Field | Meaning |
| --- | --- |
| `state` | `1` on foot in a shareable context, `0` otherwise (`kind=none`, other fields `-`/0) |
| `kind` | `ship` (shared ship), `station`, `none` |
| `ship` | shared ship index 1..ship_count (`Arena.$ships`, wire entity id); 0 unless `kind=ship` |
| `sector`,`code`,`macro` | station identity: sector macro, ID code (`ABC-123`), station macro; `-` otherwise |
| `room` | room macro of the sender (diagnostic only; `-` if none) |
| `x,y,z`,`yaw` | metres / degrees relative to the container (station or ship) |
| `speed` | m/s, smoothed, used for the send rate |
| `stime` | sender clock (s), for interpolation |
| `transfers` | count of finished transporter (elevator) rides of the sender |

Relay (both relays, byte-identical): authenticates lease and address, validates every
field (atoms `[A-Za-z0-9_.-]{1,96}`, ship <= session ship count, consistent kind/state,
|xyz| <= 100 km, |yaw| <= 360, speed 0..100, stime 0..1e9, monotonic seq), keeps the
latest row per lease and forwards it to **every other peer** while younger than 1 s:

```
WALK2|1|receiverLease|senderLease|senderSlot|seq|...|transfers
```

once per new sequence and at most every 0.2 s otherwise (loss repair). The relay adds
the sender's slot, so clients key avatars by slot (`p2..p8`), not by "the other
player". A changed lease for a slot (reconnect) resets that slot's history. The
legacy `WALK` (context 1..4, two players) stays accepted by both relays for parity but
is no longer sent; new clients drop it with `WALK_DROP reason=legacy_walk`.

Python relay: two players, as before. Rust relay: every session size. Parity:
`src/rust/parity` hand-written and randomized scenarios include WALK2 (2000 randomized
scenarios identical; WALK2 appears in 157 of their outputs).

## 3. Context identity and placement

**Why not the old 4-entry list.** Indexes into `[ship1, ship2, market1, market2]` only
covered two ships and two stations. The identity must be resolvable from data every
client shares:

- *Shared ships* are created per world at session start, so their native IDs and codes
  differ; their **wire index** (1..N) is the mod's existing shared identity.
- *Stations* come from the shared starting save, so each keeps its saved **ID code** in
  every client's world. Code + station macro + sector macro is the same identity
  `x4lan_npc_combat.xml` uses for NPC ships. MD resolves it with
  `find_sector`/`find_station` (macro-filtered, then code-matched) and caches the result;
  misses retry at most every 5 s. A station built later in only one world has no
  counterpart: `reason=context`, nothing drawn. A station without an ID code fails
  closed on the sender (`state=0`, reason `nocode`).

**Room.** Rooms are not sent as identities: dynamic interiors (bar, offices, corridors)
are generated per world by MD and need not exist or match. The receiver asks
`get_floor_position` for the container-relative position and places the avatar in the
local room at that floor (`add_actor_to_room` with `position object=context
space=room`, the vanilla `npc_instantiation` pattern). No floor -> `reason=floor`. A
different room macro than the sender's is placed but logged (`WALK_ROOM_MISMATCH`).
Yaw is converted from container to room space with the room's relative rotation.

**Motion.** Lua keeps up to 10 samples per slot, maps the sender clock with a minimum-
delay offset that creeps up slowly for drift, renders 0.2 s behind the newest sample
(linear position, shortest-arc yaw, never extrapolated) and never blends across a
change of context, room or transporter count. Every 0.2 s per avatar it hands MD the
next target with a speed of distance/0.2 s (0.3..7 m/s). MD walks the avatar there with
`start_actor_walk` when it is visible in the same room and at most 6 m away, otherwise
re-places it. A refused placement is retried by the 2 s refresh, not in a loop.

**Hidden when** (MD/Lua reason): not on foot, stale > 1 s, context unresolved, other
sector than the local player, no floor, avatar creation failed, forget after 30 s.

## 4. On-foot interactions (elevators, doors, triggers)

Observation (native, 26 Oct): with both clients docked, clicking the elevator on one
client's ship changes nothing on the other. Decision:

| Object | Synchronized? | How |
| --- | --- | --- |
| Elevator / transporter ride | yes: the *ride of the avatar* | `transfers` counter in WALK2 |
| Elevator door/animation | follows from the avatar's ride | engine, local |
| Proximity doors | no (local cosmetics) | open for the local avatar if the engine lets NPCs open them |
| Platform triggers (mission doors, vents, terminals) | no | logged `WALK_TRIGGER` on the sender only |

Elevators travel in the walk channel instead of a separate event: the sender counts
finished rides (`event_entity_transport_finished` for the player entity). A count is
state, so it survives loss and arrives ordered with the pose that follows the ride.
On a changed count within the same context the receiver's MD finds the NPC transport
waypoints (`find_npc_waypoint tags=tag.npctransport`) nearest the avatar in its current
room and nearest the new pose in the destination room, stands the avatar in the start
slot and calls `activate_transporter`; the engine then plays the transporter for that
avatar. Pose updates for that slot wait until `event_entity_transport_finished` (or 8 s).
No slots, a refused ride or a missing destination interior fall back to placement or
hide, each with a `WALK_TRANSPORT` line.

Platform triggers are not replayed: their group is a tag, and MD offers no tag lookup
from a name, so a receiver cannot name the same trigger reliably. They belong to
missions that only the server's world runs. The `WALK_TRIGGER` line records the
component macro and group so a native session can decide whether a mapped subset
(e.g. one door type) is worth an explicit `WACT|1|lease|seq|context...|room|group`
event later.

## 5. Several players on one ship (design; groundwork done)

Groundwork in this change: `kind=ship` with any ship index 1..N is accepted by both
relays and resolved by every client; poses are relative to the ship, so they stay
correct while the ship flies, docks or jumps; every client draws every other client's
avatar inside any shared ship.

Design for the rest:

- **Ownership.** The ship stays a shared ship with exactly one movement authority: the
  pilot (existing CREW/AUTH epochs). Passengers never publish NAV2 for it. The relay
  already lets at most one client own a ship; a passenger who takes the free pilot seat
  goes through the normal CREW handoff.
- **What passengers see.** The ship is a replica on every non-pilot client and moves by
  NAV2. A passenger standing inside it is *native* on its own client: its player entity
  is in the local replica's interior. For that to be stable, the replica must keep
  carrying the player while it is repositioned by `SetObjectSectorPos`/warps. This is
  the main native risk: a warp of a replica with the local player aboard may drop or
  teleport the player. A native probe must answer it before multi-crew is enabled.
- **Seats and crew roles.** Seats other than the pilot seat (`player.occupiedship` is
  null) count as on foot today; the avatar stands at the seat position. Next step:
  add `seat` (control-position name or chair slot, `-` when standing) so receivers seat
  the avatar with `add_actor_to_room slot=...` / `set_actor_current_chair`. Crew roles
  (gunner, co-pilot) need their own authority rules (who fires turrets) and are out of
  scope until turrets are replicated.
- **Undock and travel.** Nothing changes for passengers: their context is the ship, the
  pose is ship-relative, and the ship's motion arrives by NAV2/NMODE. Highway and
  travel modes leave `player.sector` null on the passenger's client, so receivers in a
  sector hide the avatar (`elsewhere`) until it is back in a sector; passengers inside
  the same ship see each other because both resolve the ship by index.
- **Boarding a ship that someone else pilots.** Teleport or walk to the replica, enter
  through the transporter; the passenger must not be able to take controls while the
  pilot holds authority (existing occupied-control protection).
- **Limits.** Only shared ships (one per client) are contexts. NPC or captured ships
  would need a universe identity (UOBJECT network id) as a third `kind`; the field
  layout leaves room for `kind=object` with the network id in `code`.

## 6. EVA / spacesuit (design only)

Today: a remote spacesuit is a spawned suit replica driven by `BODY2` (sector macro +
absolute pose, independent generation), one per remote client (`life_game.lua`). The
server reports a suit kill only for the most recent remote suit and only when its
generation is unique (`SUITKILL` names a generation, not a slot; MD tracks one remote
suit, `x4lan_life.xml`).

Design:

- **Visibility** stays on BODY2: `mode=suit` already shows a suit in space at the right
  pose for every client. WALK2 sends `state=0` while suited, so no on-foot avatar and
  no suit can coexist for one player.
- **Kill reporting per slot.** Replace `SUITKILL|1|lease|generation` with
  `SUITKILL2|1|lease|slot|generation` (server -> relay -> named client). MD keeps one
  remote-suit table keyed by slot (`$remoteSuits.{slot}` with the generation) and a
  group for `event_object_destroyed`; the destroyed suit's slot comes from the table,
  not from "the newest suit". The relay validates that the named slot's current BODY2
  is `suit` with that generation, then forwards only to that slot. Old clients keep
  SUITKILL until both sides are upgraded (no protocol switch mid-session).
- **Transitions.** Leaving a ship/station into space: WALK2 `state=0` (reason `suit`)
  hides the avatar at once; BODY2 `suit` with a new generation spawns the suit.
  Entering a ship/station: BODY2 leaves `suit` (replica removed) and WALK2 resumes
  with the new context. Both channels are state-based, so order and loss only delay.
- **Suit weapons, repair laser, tether** are not replicated and stay out of scope.

## 7. Open native checks (risks)

1. `add_actor_to_room` with a `get_floor_position` result places a cue actor visibly
   on station dock areas and on landing pads (W01). Unverified.
2. `start_actor_walk` animates a cue actor without an NPC state machine, at the given
   speed, to a room-space position (W01). If it slides or freezes, fall back to
   placement only (raise the 6 m walk limit to 0) and log the result.
3. Container-relative yaw minus room rotation gives the correct heading in rotated
   station modules.
4. Station ID codes are identical in every client's world for stations of the shared
   save (W01 with an NPC station). `WALK_HIDDEN reason=context` would disprove it.
5. `event_entity_transport_finished` fires for the player entity; `activate_transporter`
   moves a cue actor between NPC transport slots and plays the elevator (W03).
6. Avatar NPCs are not claimed by other MD (crew assignment, conversations) and do
   not block landing pads or doors for NPCs.
7. Cost: MD work per avatar is one action block every 0.2 s; FRAMECOST shows it under
   `X4LAN_Walk.Move` in `FRAMECOST_MD`.
