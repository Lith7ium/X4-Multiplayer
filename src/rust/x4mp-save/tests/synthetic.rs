//! Unit tests on a small synthetic save: reference closure, neighbour expansion,
//! weak-reference pruning, local-namespace separation and dangling detection.

use std::collections::BTreeSet;

use x4mp_save::index::{build_index, SaveIndex};
use x4mp_save::io::from_bytes;
use x4mp_save::trim::{neighbour_sectors, plan, write_trimmed, ScriptRefs, TrimOptions};
use x4mp_save::verify::{dangling_targets, verify};

/// Five sectors: A -gate- B -gate- C -cluster highway- D, and E with no connection.
/// Ships (all NPC unless noted):
/// 0x100 in A; 0x300 in C unreferenced (has an order whose id collides with a station
/// construction entry); 0x301 in C owned by the player; 0x302 in C referenced by a
/// station trade; 0x304 in C, commander of 0x302; 0x303 in C referenced only by an MD
/// variable, carrying docked 0x305; 0x307 carrier in D with the player's 0x306 docked;
/// 0x400 in E referenced by nothing. The job queue holds 0x500 (only requested) and
/// 0x501 (also referenced by the station's build task) plus a mention of 0x301.
const SAVE: &str = r#"<?xml version="1.0" encoding="UTF-8"?>
<savegame>
<info><save name="synthetic" date="0"/></info>
<universe>
<jobs enabled="1" masstraffic="1">
<job firsttime="1" id="j_a"/>
<job active="1" time="5.5" id="j_b">
<requested><request ship="[0x500]" requester="[0x200]" timeout="1800"/></requested>
<waiting>
<ship><component class="ship_m" macro="ship_m_macro" connection="space" owner="xenon" id="[0x500]"/></ship>
<ship><component class="ship_s" macro="ship_s_macro" connection="space" owner="xenon" id="[0x501]"/></ship>
<ship id="[0x301]"/>
</waiting>
</job>
</jobs>
<component class="galaxy" macro="galaxy_macro" connection="galaxy" id="[0x1]">
<connections>
<connection connection="clusters">
<component class="cluster" macro="cluster_1_macro" connection="galaxy" id="[0x2]">
<connections>
<connection connection="sectors">
<component class="sector" macro="sector_a_macro" connection="cluster" owner="argon" id="[0x10]">
<connections>
<connection connection="zones">
<component class="zone" macro="zone_a_macro" connection="sector" id="[0x11]">
<connections>
<connection connection="gates">
<component class="gate" macro="gate_macro" connection="space" id="[0x12]">
<connections><connection connection="gate" id="[0x13]"><connected connection="[0x23]"/></connection></connections>
</component>
</connection>
<connection connection="stations">
<component class="station" macro="station_macro" connection="space" owner="argon" id="[0x200]">
<listeners>
<listener listener="[0x310]" event="killed"/>
<listener listener="[0x110]" event="killed"/>
</listeners>
<construction><sequence><entry id="[0x2470]" index="1"/></sequence></construction>
<production><trade id="[0x900]" buyer="[0x302]" ware="energycells"/></production>
<build id="[0x77]" type="ship" component="[0x501]"/>
<connections>
<connection connection="modules">
<component class="production" macro="prod_macro" connection="space" construction="[0x2470]" id="[0x201]"/>
</connection>
</connections>
</component>
</connection>
<connection connection="ships">
<component class="ship_s" macro="ship_a_macro" connection="space" owner="argon" id="[0x100]">
<connections><connection connection="cockpit">
<component class="npc" macro="pilot_macro" connection="pilot" id="[0x110]"/>
</connection></connections>
</component>
</connection>
</connections>
</component>
</connection>
</connections>
</component>
<component class="sector" macro="sector_b_macro" connection="cluster" owner="argon" id="[0x20]">
<connections>
<connection connection="zones">
<component class="zone" macro="zone_b_macro" connection="sector" id="[0x21]">
<connections>
<connection connection="gates">
<component class="gate" macro="gate_macro" connection="space" id="[0x22]">
<connections>
<connection connection="gate" id="[0x23]"><connected connection="[0x13]"/></connection>
<connection connection="gate2" id="[0x24]"><connected connection="[0x33]"/></connection>
</connections>
</component>
</connection>
</connections>
</component>
</connection>
</connections>
</component>
<component class="sector" macro="sector_c_macro" connection="cluster" owner="xenon" id="[0x30]">
<connections>
<connection connection="zones">
<component class="zone" macro="zone_c_macro" connection="sector" id="[0x31]">
<connections>
<connection connection="hw" id="[0x35]"><connected connection="[0x51]"/></connection>
<connection connection="gates">
<component class="gate" macro="gate_macro" connection="space" id="[0x32]">
<connections><connection connection="gate" id="[0x33]"><connected connection="[0x24]"/></connection></connections>
</component>
</connection>
<connection connection="ships">
<component class="ship_m" macro="ship_m_macro" connection="space" owner="xenon" id="[0x300]">
<orders><order id="[0x2470]" order="Patrol"><param name="wares" type="list" value="5"/></order></orders>
<connections><connection connection="cockpit">
<component class="npc" macro="pilot_macro" connection="pilot" id="[0x310]"/>
</connection></connections>
</component>
<component class="ship_s" macro="ship_s_macro" connection="space" owner="player" id="[0x301]"/>
<component class="ship_m" macro="ship_m_macro" connection="space" owner="xenon" id="[0x302]">
<source job="j" commander="[0x304]"/>
</component>
<component class="ship_s" macro="ship_s_macro" connection="space" owner="xenon" id="[0x304]"/>
<component class="ship_l" macro="ship_l_macro" connection="space" owner="xenon" id="[0x303]">
<connections><connection connection="dock">
<component class="dockingbay" macro="bay_macro" connection="dock" id="[0x3a0]">
<connections><connection connection="ships">
<component class="ship_s" macro="ship_s_macro" connection="dock" owner="xenon" id="[0x305]"/>
</connection></connections>
</component>
</connection></connections>
</component>
</connection>
</connections>
</component>
</connection>
</connections>
</component>
<component class="sector" macro="sector_d_macro" connection="cluster" owner="teladi" id="[0x40]">
<connections>
<connection connection="zones">
<component class="zone" macro="zone_d_macro" connection="sector" id="[0x41]">
<connections>
<connection connection="hw" id="[0x45]"><connected connection="[0x52]"/></connection>
<connection connection="ships">
<component class="ship_xl" macro="ship_xl_macro" connection="space" owner="teladi" id="[0x307]">
<connections><connection connection="dock">
<component class="ship_s" macro="ship_s_macro" connection="dock" owner="player" id="[0x306]"/>
</connection></connections>
</component>
</connection>
</connections>
</component>
</connection>
</connections>
</component>
<component class="sector" macro="sector_e_macro" connection="cluster" owner="teladi" id="[0x60]">
<connections>
<connection connection="ships">
<component class="ship_s" macro="ship_s_macro" connection="space" owner="teladi" id="[0x400]"/>
</connection>
</connections>
</component>
</connection>
<connection connection="highways">
<component class="highway" macro="hw_macro" connection="cluster" id="[0x50]">
<connections>
<connection connection="entry" id="[0x51]"><connected connection="[0x35]"/></connection>
<connection connection="exit" id="[0x52]"><connected connection="[0x45]"/></connection>
</connections>
</component>
</connection>
</connections>
</component>
</connection>
</connections>
</component>
</universe>
<md>
<script name="test">
<cue name="c"><vars><value name="$watch" type="component" value="[0x303]"/><value name="$l" type="list" value="6"/></vars></cue>
</script>
</md>
<aidirector>
<entity id="[0x110]"><script name="move.idle"><vars><value name="$x" type="component" value="[0x100]"/></vars></script></entity>
<entity id="[0x310]"><script name="move.patrol" order="[0x2470]"/></entity>
</aidirector>
<script>
<refs type="list">
<ref id="5"><value type="component" value="[0x300]"/></ref>
<ref id="6"><value type="ware" value="energycells"/></ref>
</refs>
<refs type="group">
<ref id="7"><members><member id="[0x300]"/><member id="[0x100]"/></members></ref>
</refs>
</script>
</savegame>
"#;

fn idx(s: &str) -> SaveIndex {
    build_index(from_bytes(s.as_bytes())).expect("index")
}

fn opts(n: u32, mode: ScriptRefs) -> TrimOptions {
    TrimOptions {
        sectors: vec!["sector_a_macro".into()],
        neighbours: n,
        keep_owner: "player".into(),
        script_refs: mode,
        client: false,
        hard: false,
        prune_md: Vec::new(),
        masstraffic_off: false,
        space_overrides: Vec::new(),
        override_classes: Vec::new(),
    }
}

fn kept_ships(i: &SaveIndex, keep: &[bool]) -> BTreeSet<u64> {
    i.units
        .iter()
        .enumerate()
        .filter(|(n, u)| keep[*n] && u.kind == x4mp_save::index::UnitKind::Dynamic)
        .map(|(_, u)| u.id)
        .collect()
}

fn trim(src: &str, o: &TrimOptions) -> (String, x4mp_save::trim::WriteStats) {
    let i = idx(src);
    let p = plan(&i, o).unwrap();
    let (st, out) = write_trimmed(&i, &p, from_bytes(src.as_bytes()), Vec::new()).unwrap();
    (String::from_utf8(out).unwrap(), st)
}

#[test]
fn neighbour_expansion_by_gates_and_highways() {
    let i = idx(SAVE);
    let names = |h: u32| -> BTreeSet<String> {
        neighbour_sectors(&i, &[0x10], h).keys().map(|s| i.sector_macro(*s).to_string()).collect()
    };
    let set = |v: &[&str]| v.iter().map(|s| s.to_string()).collect::<BTreeSet<_>>();
    assert_eq!(names(0), set(&["sector_a_macro"]));
    assert_eq!(names(1), set(&["sector_a_macro", "sector_b_macro"]));
    assert_eq!(names(2), set(&["sector_a_macro", "sector_b_macro", "sector_c_macro"]));
    // The cluster highway is a zero-cost pass-through: C -> D is one hop.
    assert_eq!(names(3), set(&["sector_a_macro", "sector_b_macro", "sector_c_macro", "sector_d_macro"]));
    // E has no gate or highway.
    assert_eq!(names(9).len(), 4);
}

#[test]
fn closure_weak_script_values() {
    let i = idx(SAVE);
    let p = plan(&i, &opts(1, ScriptRefs::Weak)).unwrap();
    let kept = kept_ships(&i, &p.keep);
    // 0x100 target sector; 0x301 player; 0x302 station trade; 0x304 commander of 0x302;
    // 0x306 player's ship docked in 0x307, so the carrier stays too.
    assert_eq!(kept, BTreeSet::from([0x100, 0x301, 0x302, 0x304, 0x306, 0x307, 0x500, 0x501]));
}

#[test]
fn closure_strong_script_values() {
    let i = idx(SAVE);
    let p = plan(&i, &opts(1, ScriptRefs::Strong)).unwrap();
    let kept = kept_ships(&i, &p.keep);
    // The MD variable now pins 0x303. Its docked 0x305 is a unit of its own and is not
    // referenced, so it is still dropped (a kept container does not pin its contents).
    assert_eq!(kept, BTreeSet::from([0x100, 0x301, 0x302, 0x303, 0x304, 0x306, 0x307, 0x500, 0x501]));
    // 0x300 is still dropped: its order id collides with the station's construction
    // entry [0x2470], but construction refs resolve only to sequence entries.
    assert!(!kept.contains(&0x300));
}

#[test]
fn neighbours_keep_everything_in_range() {
    let i = idx(SAVE);
    let p = plan(&i, &opts(2, ScriptRefs::Weak)).unwrap();
    let kept = kept_ships(&i, &p.keep);
    for s in [0x300, 0x303, 0x305] {
        assert!(kept.contains(&s), "{s:x} in sector C (2 hops) must be kept");
    }
    assert!(!kept.contains(&0x400));
}

#[test]
fn writer_drops_and_prunes() {
    let (out, st) = trim(SAVE, &opts(1, ScriptRefs::Weak));
    for gone in ["[0x300]\"", "[0x303]\"", "[0x305]\"", "[0x400]\"", "id=\"[0x310]\"", "<ref id=\"5\">"] {
        assert!(!out.contains(&format!("id=\"{}", gone.trim_start_matches("id=\""))), "{gone} should be gone");
    }
    // Listener to the dropped pilot pruned, the other kept; group member pruned.
    assert!(!out.contains("listener=\"[0x310]\""));
    assert!(out.contains("listener=\"[0x110]\""));
    assert!(!out.contains("<member id=\"[0x300]\"/>"));
    assert!(out.contains("<member id=\"[0x100]\"/>"));
    // MD variable stays and now dangles (tolerated script value); pool list 6 kept.
    assert!(out.contains("value=\"[0x303]\""));
    assert!(out.contains("<ref id=\"6\">"));
    // The AI entity of the dropped pilot is gone, the kept one remains.
    assert!(out.contains("<entity id=\"[0x110]\">"));
    assert_eq!(st.skipped_entities, 1);
    assert_eq!(st.skipped_pool, 1);
    assert_eq!(st.pruned_listeners, 1);
    assert_eq!(st.pruned_members, 1);
    // ship_m 0x300 + npc, ship_l 0x303 + bay + ship_s 0x305, ship_s 0x400
    assert_eq!(st.skipped_components, 6);
}

#[test]
fn verify_passes_after_trim() {
    let base = idx(SAVE);
    let b = dangling_targets(&base);
    assert!(b.is_empty(), "synthetic save is fully resolved: {b:?}");
    // Strong mode: no new dangling of any kind.
    let (out, _) = trim(SAVE, &opts(1, ScriptRefs::Strong));
    let r = verify(&idx(&out), Some(&b), true);
    assert!(r.ok(), "{}", r.render());
    // Weak mode: only the MD script value dangles; strict fails, default passes.
    let (out, _) = trim(SAVE, &opts(1, ScriptRefs::Weak));
    let t = idx(&out);
    let r = verify(&t, Some(&b), false);
    assert!(r.ok(), "{}", r.render());
    assert_eq!((r.new_structural, r.new_script_values), (0, 1));
    assert!(!verify(&t, Some(&b), true).ok());
}

#[test]
fn verify_detects_dangling_structural_reference() {
    let b = dangling_targets(&idx(SAVE));
    // Remove the commander ship by hand: 0x302's `source@commander` now dangles.
    let broken = SAVE.replace(
        r#"<component class="ship_s" macro="ship_s_macro" connection="space" owner="xenon" id="[0x304]"/>"#,
        "",
    );
    let r = verify(&idx(&broken), Some(&b), false);
    assert!(!r.ok());
    assert_eq!(r.new_structural, 1);
    assert_eq!(r.dangling_by_attr["source@commander"], (1, 1, 0));
    // Without a baseline every dangling reference counts.
    assert!(!verify(&idx(&broken), None, false).ok());
    // A one-sided gate link is a dangling connection reference.
    let broken = SAVE.replace(r#"<connected connection="[0x13]"/>"#, r#"<connected connection="[0x99]"/>"#);
    let r = verify(&idx(&broken), None, false);
    assert_eq!(r.dangling_by_attr["connected@connection"].0, 1);
}

#[test]
fn verify_detects_duplicates_and_malformed_xml() {
    let dup = SAVE.replace(r#"id="[0x400]""#, r#"id="[0x100]""#);
    let r = verify(&idx(&dup), None, false);
    assert_eq!(r.dup_components, 1);
    assert!(!r.ok());
    let bad = SAVE.replace("</universe>", "</universes>");
    assert!(build_index(from_bytes(bad.as_bytes())).is_err());
    let truncated = &SAVE[..SAVE.len() / 2];
    assert!(build_index(from_bytes(truncated.as_bytes())).is_err());
}

#[test]
fn unknown_sector_is_an_error() {
    let i = idx(SAVE);
    let mut o = opts(1, ScriptRefs::Weak);
    o.sectors = vec!["sector_zz_macro".into()];
    assert!(plan(&i, &o).is_err());
}

#[test]
fn client_mode_neutralises_jobs() {
    let mut o = opts(1, ScriptRefs::Weak);
    o.client = true;
    let i = idx(SAVE);
    assert_eq!(i.jobs.jobs, 2);
    assert_eq!((i.jobs.active, i.jobs.firsttime, i.jobs.timers), (1, 1, 1));
    assert_eq!((i.jobs.queued_ships, i.jobs.queued_ship_refs, i.jobs.requests), (2, 1, 1));
    let p = plan(&i, &o).unwrap();
    let kept = kept_ships(&i, &p.keep);
    // 0x500 was only requested and is dropped; 0x501 is pinned by the build task.
    assert!(!kept.contains(&0x500));
    assert!(kept.contains(&0x501));
    assert_eq!(p.client_queue_kept, 1);
    let (out, st) = trim(SAVE, &o);
    assert!(out.contains(r#"<jobs enabled="0" masstraffic="1">"#));
    assert!(out.contains(r#"<job id="j_a" active="0"/>"#));
    assert!(out.contains(r#"<job active="0" id="j_b">"#));
    assert!(!out.contains("firsttime") && !out.contains("time=\"5.5\""));
    assert!(!out.contains("<requested>") && !out.contains("[0x500]"));
    assert!(out.contains(r#"id="[0x501]""#));
    assert!(!out.contains(r#"<ship id="[0x301]"/>"#));
    assert_eq!(st.client_jobs_rewritten, 2);
    // requested + queued ship 0x500 + mention of 0x301
    assert_eq!(st.client_queue_elements, 3);
    let r = verify(&idx(&out), Some(&dangling_targets(&idx(SAVE))), false);
    assert!(r.ok(), "{}", r.render());
    // Without --client the job section is copied unchanged.
    let (plain, _) = trim(SAVE, &opts(1, ScriptRefs::Weak));
    assert!(plain.contains(r#"<job active="1" time="5.5" id="j_b">"#));
}

#[test]
fn hard_mode_drops_distant_references_and_prunes() {
    // NPC state machine instances: one for the pilot of dropped 0x300, one for the kept 0x100.
    let src = SAVE.replace(
        "</md>",
        r#"<script name="NPC_State_Machines"><cue name="Base"><instances>
<cue id="1" state="complete"><vars><value name="$NPC" type="component" value="[0x310]"/></vars><cue id="2" base="9"/></cue>
<cue id="3" state="complete"><vars><value name="$NPC" type="component" value="[0x110]"/></vars><cue id="4" base="9"/></cue>
</instances></cue></script>
</md>"#,
    );
    let mut o = opts(1, ScriptRefs::Weak);
    o.hard = true;
    o.masstraffic_off = true;
    o.prune_md = vec![("NPC_State_Machines".into(), "$NPC".into())];
    let i = idx(&src);
    let p = plan(&i, &o).unwrap();
    let kept = kept_ships(&i, &p.keep);
    // Sector C is out of range: the station trade no longer pins 0x302 (nor its commander
    // 0x304). The player's ships and the carrier holding one of them stay.
    assert!(!kept.contains(&0x302) && !kept.contains(&0x304));
    assert!(kept.contains(&0x301) && kept.contains(&0x306) && kept.contains(&0x307) && kept.contains(&0x100));
    let (out, st) = trim(&src, &o);
    assert!(!out.contains("buyer=\"[0x302]\""), "trade with a dropped ship is pruned");
    assert_eq!(st.pruned_structural.get("trade@buyer"), Some(&1));
    assert!(out.contains(r#"<jobs enabled="1" masstraffic="0">"#));
    assert!(!out.contains(r#"value="[0x310]""#) && !out.contains(r#"<cue id="2""#));
    assert!(out.contains(r#"<cue id="3" state="complete"><vars><value name="$NPC" type="component" value="[0x110]"/></vars><cue id="4" base="9"/></cue>"#));
    assert_eq!(st.pruned_md.get("NPC_State_Machines"), Some(&1));
    let base = dangling_targets(&idx(&src));
    let r = verify(&idx(&out), Some(&base), false);
    assert!(r.ok(), "{}", r.render());
}