# x4mp-save: savegame analysis, per-client trimming and verification

Offline tooling for fast hot join. It covers design items 7 and 8 of section 7b in
`preparation/TARGET-ARCHITECTURE-PROPOSAL.md` (branch `contrib/ferrite-architecture-design`):
measure a save, and trim it per joining client by reference closure. It is a library
and a CLI. It never loads into X4, never touches the hooks crates, and only reads its
inputs.

Status: **built and tested offline only.** No trimmed save has been loaded by X4
yet. The steps for that test are in [Native load test](#native-load-test-procedure-not-run-yet).

```
x4mp-save analyze <save> [--keep-owner <faction>] [--top N]
x4mp-save trim <save> --sector <macro> [--sector <macro>...] [--neighbours N]
               --keep-owner <faction> -o <out.xml.gz> [--script-refs weak|strong]
               [--client] [--top N]
x4mp-save verify <save> [--baseline <original save>] [--strict]
```

`<save>` can be `.xml.gz` (detected from the content) or plain XML. `trim` writes to
`<out>.tmp` and then renames it, and it refuses to overwrite its input. `--neighbours`
defaults to 1 and `--script-refs` defaults to `weak` (see below). `--client` adds the
client-only job neutralisation described in [Client mode](#client-mode---client).

This is the first crate in the workspace with third-party dependencies: `quick-xml`
0.37 (streaming parser and writer) and `flate2` (gzip). Both are limited to this
offline tool.

## How it works

A save is streamed twice and never held as a DOM:

1. **Index pass.** The tool records every component (id, class, macro, owner, enclosing
   sector), every connection id, every local id, every script-pool entry, and every
   attribute value that is a reference. It also checks well-formedness: matching end
   tags, a single root, attribute syntax, and no truncation.
2. **Plan.** It builds the keep set as a breadth-first closure over *units*, with the
   first reason each unit was kept.
3. **Write pass.** It copies the save event by event, verbatim. It skips the subtrees
   of dropped units and prunes the weak references that point at them. Gzip runs on a
   second thread.

**Units** are what the plan keeps or drops:

| Unit | What it is |
| --- | --- |
| persistent world (unit 0) | Everything outside a dynamic subtree. Always kept. |
| dynamic unit | The subtree of a component with a dynamic class, without any dynamic subtrees nested inside it. A docked fighter is its own unit inside its carrier. |
| script-pool entry | `savegame/script/refs/ref`: a shared list, table, group or string. |
| AI entity | `savegame/aidirector/entity`: an alias of the NPC or computer component with the same id. It is kept or dropped together with that component's unit. |

**Seeds** (units that are always kept):

- the persistent world;
- dynamic units in a target sector or within `--neighbours` gate or highway hops of one;
- units with a component owned by `--keep-owner`, or containing the `player` entity;
- dynamic units outside any sector (the job spawn queue);
- pool entries with no visible referrer;
- AI entities without a component.

**Closure.** The plan follows strong references from every kept unit, and a kept nested
unit keeps the unit that contains it. A kept container does *not* keep the units nested
inside it.

## Save structure findings (9.00, build 611726, `save_010`)

Top-level sections of `<savegame>`: `info`, `universe`, `economylog`, `log`,
`messages`, `missions`, `aidirector`, `operations`, `ventures`, `notifications`, `ui`,
`md`, `script`, `stats`, `fleetmanager`, `tickercache` and `signature`.

**`universe`** holds `factions` (relations, accounts, licences), `jobs`, `god`,
`diplomacy`, `controltextures`, `physics` and the component tree:
`galaxy > cluster > sector > zone > station/ship/gate/...`. Children sit under
`<connections><connection connection="..."><component .../>`, and component
attributes are `class`, `macro`, `id`, `connection`, `code`, `owner`, `spawntime` and
others. The tree also contains:

- 186 job-queue ships under `jobs/job/waiting/ship`;
- 123 NPC components in MD cue actors and instances.

**`aidirector`** holds the running AI scripts. There are 23,756 entities, and each
entity id equals the id of the NPC (10,507) or computer (13,133) component it drives.

**`script/refs`** is the script value pool: 349,780 entries across the types `string`,
`list`, `group`, `table`, `componentslot`, `macroslot`, `licence`, `loadout`, `vector`
and `rotation`. Entries are referenced by decimal id from typed values, for example
`<param type="list" value="7149"/>`. Group entries list their components as
`<members><member id="[0x..]"/>`.

**`md`** holds the MD cue tree, with cue variables as typed values.

### Id namespaces

Ids are written as `[0x..]`, but they belong to several namespaces whose numbers
collide. Treating them as one global id space is wrong. For example, 6,215 local ids
are written both as a ship's `order` and as a queue `entry`, and the connection,
order and trade numbers overlap as well.

| Namespace | Defined by | Referenced by |
| --- | --- | --- |
| component | `component@id` (441,971). Mirrored by `aidirector entity@id` and by `script member@id` | almost every other `[0x..]` attribute |
| connection | `connection@id` inside `<connections>` (92,298) | `connected@connection`. Links are recorded on both sides. |
| local, per kind | `entry@id` (construction-sequence entries), `order@id`, `build@id`, `network@id` | `component@construction` resolves to entries, `aidirector script@order` to orders, `buildtasks@build` and `build@order` to builds, `event@network` to networks |
| local, unreferenced | `trade`, `area`, `quota`, `quotalist`, `filter`, `reservation`, `account`, `weapon` (`@id`) | nothing found |
| script pool | `script/refs[@type]/ref@id` (decimal) | `type=<pool type> value=<decimal>` |

Some `id` attributes are *mentions* of a component, not definitions: `member`, `ship`
(job queue), `sector` (discovered), `object`, `target`, `filtered`, `actor`.

The `connected` links have these meanings:

- gate to gate: 306;
- zone to cluster highway: 884;
- region to sector, and zone to zone (in-sector structure);
- station modules to each other;
- NPC to its own ship;
- **commander to subordinates** (ship to ship, about 5,400 pairs, and station to ship).

The last kind makes fleets and station-assigned ships one connected group.

### Reference attributes in `save_010`

| element@attribute | count | targets |
| --- | ---: | --- |
| `value@value` (typed script values) | 619,817 | pool strings, lists and groups; dockingbay, station and ship components |
| `connected@connection` | 92,298 | connections |
| `listener@listener` (weak) | 92,045 | engines, stations, computers, ships |
| `key@value` (table keys) | 86,963 | dockingbays, strings, sectors |
| `group@context` | 74,401 | station modules |
| `member@id` (script groups, weak) | 58,524 | ships, station modules |
| `field@region` | 41,456 | regions |
| `param@value` (order parameters) | 38,211 | pool lists, ships, sectors |
| `post@component` | 25,259 | computers and NPCs (crew posts) |
| `component@construction` | 22,136 | local construction entries |
| `log@owner` (economy log) | 16,566 | stations, a few L/XL ships |
| `script@order` | 10,064 | local orders |
| `read@space`, `write@space`, `source@sector`, `source@zone` | about 10,000 each | zones and sectors |
| `attached@component`, `trade@buyer`/`seller`, `source@commander`, `wingman@leader`, `targetpoint@refobject`, `reservation@*`, `build@*`, `request@*`, ... | fewer than 7,300 each | stations, ships, zones |

`x4mp-save analyze` prints the full table, including the target classes for each
attribute and the unresolved counts.

The most referenced kinds, by incoming references:

| Kind | Incoming references |
| --- | ---: |
| dockingbay | 157k |
| pool string | 135k |
| pool list | 104k |
| connection | 92k |
| station | 75k |
| pool group | 75k |
| ship_s | 74k |
| ship_m | 56k |
| zone | 56k |
| npc | 49k |
| sector | 46k |

### Dangling references that X4 writes itself

The untouched `save_010` already has **3,581 references to 3,282 ids that are not in
the file**:

| Reference | Count |
| --- | ---: |
| script values (`value@value`) | 3,209 |
| order parameters | 216 |
| table keys | 117 |
| UI listeners | 20 |
| filter lists | 16 |
| others | 3 |

These are mostly script variables that still hold destroyed objects, and X4 loads them.
This is why `verify` compares against the original save (see Classification rules).

## Classification rules

All the tables are in `src/schema.rs`.

**Dynamic classes** are dropped in distant sectors:

- `ship_xs` to `ship_xl` and `spacesuit`;
- projectiles and deployables: `missile`, `bullet`, `mine`, `bomb`, `countermeasure`,
  `satellite`, `navbeacon`, `resourceprobe`;
- drops and wreck pieces: `collectable*`, `recyclable`;
- `asteroid`.

None of the projectile, mine or spacesuit classes occur in `save_010`, so those names
are unverified.

**Persistent** means everything else. This covers:

- galaxy, cluster, sector, zone and region;
- gates and highways;
- stations with all their modules, build storages and build plots;
- lockboxes, data vaults, signal leaks, anomalies and placed `object` wrecks;
- all non-component data: factions, relations, accounts, jobs, god, diplomacy, the
  economy log, MD, missions, operations, ventures, stats, UI and the script pool;
- anything owned by `--keep-owner`.

**Strong references** pin their target. These are all structural attributes:

- docking, commander and subordinate links (`connected`);
- crew posts, construction entries, AI-script orders;
- trades and reservations, fleet `member`/`commander`/`leader`;
- `attached`, `build`, `request`;
- mission targets and actors;
- economy-log owners.

**Weak references** never pin their target. When the target is dropped, the writer
prunes the referencing element:

- `listener@listener`;
- `member@id` inside script-pool groups. Groups drop destroyed members, and the
  original save has no unresolved group members.

**Typed script values** are `value`, `key`, `param`, `param2` and `param3` with a
`type`, plus `filtered@id`. With `--script-refs strong` they pin their target like any
strong reference. With `--script-refs weak` (the default) they do not pin it: the value
stays and points at a missing id, which is the state X4 already writes for a destroyed
object. Pool entries that become unreachable are dropped.

`verify --baseline <original>` treats dangling references as follows:

- **Structural** reference that dangles and resolved in the original: always a failure.
- **Script value** that newly dangles: reported, and a failure only with `--strict`.
- Duplicate component, connection, pool or entity ids: always a failure.
- Malformed XML: always a failure.

**Neighbours** are found by a breadth-first search over `connected` links where at
least one end belongs to a gate, highway, highway entry gate or highway exit gate. A
link is mapped to the sector that encloses each end. A cluster-level highway is a
zero-cost pass-through, so a superhighway counts as one hop.

## Measured on `save_010` (prepared shared start, game time 8.5 s)

### File

| Measure | Value |
| --- | --- |
| Size on disk (gzip) | 27,560,722 bytes |
| Uncompressed XML | 346.6 MB |
| Elements | 6.70 M |
| Attributes | 14.0 M |
| Components | 441,971 |
| Sectors | 152 |
| Clusters | 127 |

### Objects

| Object | Count |
| --- | ---: |
| Stations | 1,436 |
| Build storages | 1,381 |
| NPC ships, size S | 6,181 |
| NPC ships, size M | 3,090 |
| NPC ships, size L | 790 |
| NPC ships, size XL | 267 |
| Player-owned ships | 5 (3 S, 1 M, 1 XL) |
| Gates | 323 |
| Highways | 106, with 106 entry and 106 exit gates |
| Lockboxes | 304 |
| Recyclables (scrap) | 293 |
| Asteroids | 271 |
| Wares containers | 49 |
| Data vaults | 29 |
| Satellites | 15 |
| Wrecked ships | 0 |
| Projectiles | 0 |

The biggest component classes are sub-parts:

| Class | Count |
| --- | ---: |
| turret | 97k |
| shieldgenerator | 80k |
| cargobay | 58k |
| dockingbay | 57k |
| engine | 22k |
| weapon | 19k |

### Dynamic share

| Measure | Value |
| --- | --- |
| Dynamic units | 10,966 |
| Components in dynamic units | 159,729 (36.1%) |
| XML in dynamic units | 102 MB (29.5%) |
| Script-pool entries | 349,780 |
| AI entities | 23,756 |

### Per sector (152 sectors)

| Per sector | Median | p90 | Max |
| --- | ---: | ---: | ---: |
| Stations | 7 | 20 | 30 |
| NPC ships | 66 | 131 | 253 (`cluster_501_sector001`) |
| Dynamic units | 67 | 134 | 346 |
| Components | 2,583 | 5,214 | 9,509 |

The dynamic share of a sector's components ranges from 0% to 99%, with a median of
34%. Eight sectors have no ships.

The player starts in `cluster_14_sector001_macro`, which has 17 stations, 113 NPC
ships, 114 dynamic units and 5,986 components.

### Trim of `cluster_14_sector001_macro`, `--keep-owner player`

| Mode | Neighbour sectors | Dynamic units kept / dropped | Components dropped | XML | gzip | verify |
| --- | --- | --- | ---: | --- | --- | --- |
| weak, `--neighbours 0` | none | 1,527 / 9,439 | 130,353 | −34.3% (228 MB) | −39.3% (16.7 MB) | OK |
| **weak, `--neighbours 1`** | 4 | **1,662 / 9,304** | **128,415** | **−33.7% (230 MB)** | **−38.7% (16.9 MB)** | OK |
| weak, `--neighbours 2` | 13 | 2,328 / 8,638 | 119,404 | −31.2% (239 MB) | −35.9% (17.7 MB) | OK |
| strong, `--neighbours 1` | 4 | 10,342 / 624 | 624 | −0.1% | −5.6% | OK, also with `--strict` |

**Weak mode, `--neighbours 1`, in detail:**

| Dropped | Count |
| --- | ---: |
| ship_s | 5,360 |
| ship_m | 2,546 |
| ship_l | 550 |
| ship_xl | 218 |
| recyclables | 293 |
| asteroids | 271 |
| containers | 46 |
| satellites | 15 |
| collectable blueprints | 5 |
| AI entities | 15,296 |
| script-pool entries | 90,112 |
| pruned group members | 770 |
| pruned listeners | 0 |

Elements fall from 6.70 M to 4.34 M. Why the 1,662 kept dynamic units stayed (first
reason):

| Reason | Units |
| --- | ---: |
| commander/subordinate chains from kept ships | 821 |
| job queue, outside sectors | 187 |
| economy-log owner (L/XL traders and miners) | 169 |
| in a neighbour sector | 161 |
| subordinate of a station | 151 |
| in the target sector | 114 |
| build task | 38 |
| reservation | 8 |
| MD actors | 7 |
| owned by the player | 4 |
| mission target | 1 |
| MD object | 1 |

`verify` found 0 new structural dangling references and 104,418 newly dangling
script values (tolerated), and no duplicate ids.

**Strong mode** is pure reference closure through every script value:

| Pinned by | Units |
| --- | ---: |
| MD cue variables | 7,729 |
| script-pool lists | 1,869 |
| AI-script variables | 41 |

Only the asteroids, recyclables, containers and satellites go. This strict
interpretation of section 7b does not reduce the load: MD faction and job logic holds
a variable for nearly every NPC ship.

### Performance

Measured with a release build on the input above.

| Command | WSL Ubuntu 24.04 (save on tmpfs) | Windows 11 |
| --- | --- | --- |
| `analyze` | 1.4 s wall, peak RSS 140 MB | 1.8 s |
| `trim` (weak, `--neighbours 1`) | 2.9 s total: index 1.1 s, plan 0.1 s, write and gzip 1.6 s; peak RSS 170 MB | 3.2 s |
| `verify --baseline` | 2.1 s (indexes both files), peak RSS 140 MB | — |

The index pass covers gunzip and parsing of 347 MB of XML. When the save is read
from `/mnt/c` instead, a trim takes 7–10 s because of the I/O. A DOM was not needed,
and memory stays well under the size of the uncompressed XML.

## Client mode (`--client`)

Puppet clients switch NPC jobs and faction logic off at runtime, but only *after*
loading (see `reports/PUPPET-CLIENT.md` on `contrib/ferrite-puppet-client`). The `jobs`
load stage is the largest single stage, about 11 s of a 37 s load, and mod code cannot
act during it. `--client` therefore neutralises the job state in the save itself.
It is never meant for a save the server keeps.

### What the save stores about jobs (`save_010`)

The job definitions (quotas, ship macros, spawn locations) are not in the save. They
come from the game's `libraries/jobs.xml`. The save holds only the runtime state in
`savegame/universe/jobs`, which is 1.3 MB of XML.

| Element and attribute | Meaning (inferred) | Count in `save_010` |
| --- | --- | ---: |
| `<jobs enabled="1" masstraffic="1">` | global switches for the job engine and mass traffic | 1 |
| `<job id="..."/>` | one per job id in the library | 1,211 |
| `job@active="1"` / `"0"` | runtime override of the library's start state | 91 / 8 |
| `job@firsttime="1"` | initial spawn still pending | 405 |
| `job@time="..."` | game time of the next spawn (timer) | 25 |
| `job/waiting/ship/component` | built ships queued for launch, outside any sector | 187 ships, 2,547 components |
| `job/waiting/ship@id` | mentions of ships already in space | 53 |
| `job/requested/request@ship,@requester,@timeout` | a station's request for a queued ship | 184 |
| ship `<source job="..." sector=.. zone=.. commandeerable=..>` | link from a ship in space to its job; this lives in the ship, not in `jobs` | 9,909 |

A missing `active` attribute does **not** mean the job is inactive. 651 jobs own 9,820
ships in space and have neither `active` nor `firsttime`; only 9 active and 7
first-time jobs own ships. So `active` stores an override, and `active="0"` is a value
the loader already reads, since 8 jobs carry it. The 405 `firsttime` jobs own almost no
ships (34), so a fresh start still has a large initial spawn pending. No job belongs to
the player faction: the job id prefixes are NPC factions, `masstraffic` and `dummy`.

### Transformations

All of these are behind `--client`:

1. `<jobs enabled="1">` becomes `enabled="0"`: the global job switch.
2. Every `<job>` gets `active="0"`, and `firsttime` and `time` are removed. This covers
   the case where the loader ignores the global switch: no job is active, none has a
   pending first spawn, and no timer is due.
3. `job/requested` is removed: 24 elements with 184 requests.
4. `job/waiting/ship` is removed for queued ships the closure drops (186 ships) and for
   the 53 bare mentions.
   - References that sit only inside the queue do not pin anything.
   - A queued ship that something outside the queue references is kept. In `save_010`
     this is 1 ship that a shipyard `build@component` task is constructing.
5. The rest of the trim is unchanged.

**Result on `save_010`** (`cluster_14_sector001_macro`, `--neighbours 1`, weak):

| Measure | Value |
| --- | --- |
| Jobs rewritten | 1,211 |
| Job-queue elements removed | 263 |
| Components dropped | 130,910 (2,495 more than without `--client`) |
| XML | −34.1% (228.5 MB) |
| gzip | −39.0% (16.8 MB) |
| verify | new structural dangling references 0, result OK |
| Time | 2.8 s, the same as without `--client` |

**Risks (unverified until the native load test):**

- It is not known whether the loader honours `enabled="0"`, or whether the jobs stage is
  dominated by evaluating 1,211 library jobs regardless of their state. If the stage
  stays long, the time goes to the library and only an engine hook can help.
- Removing `firsttime` might mean "the first spawn already happened" or "spawn
  normally". Either way no job is active, but an engine that ignores both switches
  could still spawn on its normal timers.
- A station that loses its pending request may re-request or log errors once jobs come
  back on. Clients never switch them back on.
- Ships keep their `source job=...` link. Any job that runs after load sees its ships
  as alive.
- **A client save must never be loaded by the server** or used as a snapshot: its job
  state is deliberately broken.

### Other client-irrelevant subsystems

Measured in `save_010` and in the client trim above. Nothing beyond the job
transformations is implemented yet.

| Subsystem | Size (original → client trim) | Content | Assessment |
| --- | --- | --- | --- |
| `md` (MD cue state) | 36.1 MB → 36.1 MB | 326 scripts, 696,733 cues. **`NPC_State_Machines`: 31.8 MB, 627,894 cues, 11,561 cue instances**; `NPC_Itemtrader` 0.5 MB; FactionLogic, FactionGoal and FactionSubgoal under 1 MB together; Story scripts under 0.2 MB each | The best candidate for the `scripts*` stage. The state machines drive walking station NPCs, which matter visually but not to a puppet. They cannot be stripped safely yet: **41,869 `type="cue"` values** (29,477 in the script pool, 11,606 in the universe, 786 in MD) refer to cue ids. This tool does not track the cue namespace, so `verify` could not prove the result. The next step is to index cue ids, then drop `NPC_State_Machines` instances whose actor is outside the detail sectors. |
| `aidirector` (running AI scripts) | 36.9 MB → 8.9 MB | 23,756 entities (orders 10k, fight 5.5k, engineer 5.4k, move 4.5k, build 1.5k, trade 1.4k) → about 8,460 after the trim | The trim already removes 76%. Dropping the rest for NPC ships would make X4 restart their order scripts from scratch, as it does after a script version change. Puppet mode stops them anyway, but a restart may first issue movement. Not done. |
| `script/refs` (value pool) | 50.0 MB → 43.9 MB | 349,780 entries, 90,112 dropped as unreachable | Mostly referenced from MD and AI state; shrinks with the two items above. |
| `economylog` | 1.7 MB | 18,758 trade log entries, 69 removed objects | Small. It is economy history for the UI graphs; stripping it saves little. |
| `universe/god` | empty (`<god/>`) | none | God (station placement and planning) state is not saved; nothing to strip. |
| `universe/factions`, `diplomacy` | 0.1 MB | relations, accounts, licences, moods, 22 diplomacy actions | Needed: relations and ownership are persistent state. |
| `missions` (offers) | under 0.1 MB | 32 offers | Negligible. Offer generators live in MD (`GM_*` and `GMC_*` scripts, under 0.05 MB each). |
| `log`, `messages`, `notifications`, `stats`, `tickercache`, `ventures`, `operations`, `ui` | under 0.1 MB each | | Not worth touching. |

Load-time attribution per stage needs the native test. The sizes suggest:

- `map` is driven by the component tree, which the trim reduces by 38% (`universe` 221.8 MB to 138.0 MB);
- `scripts*` by MD plus the AI director plus the pool: 123 MB to 89 MB, dominated by
  `NPC_State_Machines`;
- `jobs` by the job engine, not by save size.

## Open risks

1. **The engine load is unproven.** No trimmed save has been loaded by X4 yet.
2. **Dangling script values.** These risks apply to weak mode, the useful one:
   - MD and AI scripts that hold a dropped ship will see it as destroyed. On a client
     that is not in puppet mode, faction, job and fleet logic may react, for example by
     respawning replacements. The client must run in puppet mode before it loads a
     trimmed save.
   - Missions whose targets live only in cue variables may abort. Mission-section
     targets and actors are strong and are kept.
   - Objects streamed back later with `create_ship` get **new ids**, so old script
     handles never re-attach. Remapping or replaying would be needed if that matters.
3. **Objects X4 might not tolerate losing:**
   - asteroids and Terran `recyclable` scrap: mining regions regenerate asteroids, and
     it is not clear whether scrap fields come back;
   - station-assigned ships: kept today through `connected`, but a variant that drops
     them would orphan the station's subordinates;
   - ships with active trade reservations: kept through `trade` and `reservation`;
   - the job queue: kept.
4. **The save signature.** The original `<signature>` is kept and no longer matches the
   content. This save already has `modified="1"`; the load test must confirm that X4
   accepts it.
5. **Data that isn't component-based is kept whole and not rewritten:**
   - `economylog`, `log`, `messages`, `notifications`, `tickercache`, `stats`;
   - `ui/addons`: the Lua UI state may hold component ids as decimal numbers;
   - mass-traffic networks;
   - strings that name ships by code.

   The tool detects only `[0x..]` attribute values and pool ids. References encoded
   any other way are invisible to the closure and to `verify`.
6. **The classification is inferred from one fresh start** (8.5 s of game time): no
   wrecks, projectiles, mines, player fleets, boarding or active combat, and only 32
   mission offers. The rules and the numbers need a progressed save. Owner-supplied
   test saves with hours of play would show real wreck, projectile and player-asset
   counts. No personal saves were used.
7. **Fleets stay together** through commander/subordinate `connected` links, so one
   kept escort keeps its whole chain. This accounts for 821 of the kept distant units.
   A future option could cut fleets that are entirely distant.

## Native load test procedure (not run yet)

Run this only with the agreement of the session that owns the PC and the X4 profile.

1. **Preconditions** (from `AGENTS.md`):
   - X4 is closed;
   - you use the disposable world in profile 45841 with autosave disabled;
   - you never load, delete or overwrite normal saves.

   Choose a save slot that is unused and agreed with the owner, for example
   `save_090`. Do not reuse `save_010`.
2. **Produce and check the save** in this worktree:
   ```
   x4mp-save trim .local/saves/save_010.xml.gz --sector cluster_14_sector001_macro \
       --neighbours 1 --keep-owner player -o .local/out/save_090.xml.gz
   x4mp-save verify .local/out/save_090.xml.gz --baseline .local/saves/save_010.xml.gz
   ```
   `verify` must print `result: OK`. Optionally build a strong-mode file as a control.
3. **Install it.** The profile owner copies `save_090.xml.gz` into the profile's `save/`
   folder. The in-game name is unchanged, so identify the save by its slot and time.
4. **Measure the baseline.** Launch X4 the usual way for the test profile, with
   `-logfile x4trim_full.log` (the name only). Load the original `save_010`. Time it
   from the click on Load to the first frame in which the player can control the ship.
   Note the log size. Quit.
5. **Measure the trimmed save.** Launch again with `-logfile x4trim_cut.log` and load
   `save_090`, timed the same way. Repeat steps 4 and 5 once more to average out
   file-cache effects.
6. **Check the game log** (in the profile folder). Compare the trimmed log with the
   full-save log:
   - any crash, abort or "savegame"/"load" error;
   - lines containing `Error`, `failed`, `invalid` or `could not`, counted per run;
   - component lookup failures such as "Component ... not found", "unknown id", "null
     component", "property lookup failed", and AI-script errors that name ids from the
     trimmed save;
   - MD errors from cues that hold dropped ships (expected to rise; record which
     scripts are involved);
   - any message about the signature or modification flag.
7. **Check the game itself:**
   - the player ship, position and money are unchanged;
   - Argon Prime and its four neighbours are fully populated;
   - in distant sectors the map shows stations but no NPC traffic;
   - station trade offers and the economy UI work;
   - player missions and offers are still listed.
8. **Repeat with a client save.** Produce one with `--client` as well
   (`save_091`) and load it the same way. Compare the duration of the `jobs` stage and
   the total load time, using the stage timestamps in the log if the build prints
   them. Count NPC ships spawned during or right after the load near gates and
   shipyards: with `--client` this should be zero. Check the log for job-related
   errors, for example requests or a missing queue.
9. **Run it.** Let it run unpaused for 5 minutes. Watch for crashes and the rate of
   errors per minute, and note respawned NPC ships in distant sectors (expected
   without puppet mode).
10. **Re-save and compare.** Save to a second agreed slot. Copy it back to
   `.local/saves/`, then run `x4mp-save verify` and `x4mp-save analyze` on it to see
   what the engine regenerated (jobs, traffic) or removed.
11. **Record** the load times (full against trimmed), the error-line differences, and
    the observations in a report under `reports/`. Remove the test slots afterwards,
    with the owner's agreement.

## Tests

```
cargo test --release -p x4mp-save        # debug works too; the save_010 test then takes about 75 s
```

`tests/synthetic.rs` builds a five-sector save and tests:

- gate and highway neighbour expansion;
- weak and strong closure, commander chains, and a carrier kept by a player ship
  docked inside it;
- local-namespace separation;
- writer pruning of listeners, group members, AI entities and pool entries;
- client mode: the job switch, per-job overrides, and queue and request removal that
  keeps a queued ship referenced by a build task;
- detection of dangling structural and connection references, duplicate ids, malformed
  XML and truncated XML.

`tests/save_010.rs` trims `.local/saves/save_010.xml.gz` (or `$X4MP_SAVE_010`) in
three ways (weak, strong, and weak with `--client`). It verifies each result against
the original and prints the size reduction. When
the save is not present, it skips with a message.
