from pathlib import Path
import xml.etree.ElementTree as E
# DO NOT regenerate x4lan_bubble.xml from this script without reconciling first: it
# already drifted (see commit cd0e8e2), and the N-player changes were applied to the
# committed MD by hand only: PrepareShips count/ownIndex/$ordered/extras and
# PreparedExtra.<k>, Arena per-ship lists and ship loop, $weapons3..7 + Shots3..7 with
# the ExtraShot library, the XShip publication and the ApplyExtra cue.
# Legacy configs copy the local loadout onto the proxy. With a shared ship spec,
# PrepareShips has already equipped both ships identically before Arena setup.
# PrepareShips requires the configured weapon count exactly. A weapon slot may stay
# empty only when the hull has no missile storage (the Xperimental's 7th slot is a
# missile launcher with missile capacity 0).
from life_guards import guard_bubble
from coop_guards import bubble as coop_bubble
R=Path(__file__).resolve().parents[2]/'src/extension/x4_lan_lab'
xml='''<?xml version="1.0" encoding="utf-8"?>
<mdscript name="X4LAN_Bubble" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="md.xsd">
<cues><cue name="Arena" namespace="this">
<conditions><event_ui_triggered screen="'X4LAN_Bubble'"/><check_value value="event.param2 == 'SetupHost' or event.param2 == 'SetupGuest'"/></conditions>
<actions>
 <set_value name="$host" exact="event.param2 == 'SetupHost'"/>
 <set_value name="$local" exact="if md.X4LAN_Bubble.PrepareShips.$own? then md.X4LAN_Bubble.PrepareShips.$own else player.ship"/><set_value name="$proxy" exact="event.param3"/>
 <do_if value="$host"><set_value name="$ships" exact="[$local,$proxy]"/><set_value name="$localSlot" exact="1"/></do_if>
 <do_else><set_value name="$ships" exact="[$proxy,$local]"/><set_value name="$localSlot" exact="2"/></do_else>
 <set_value name="$targets" exact="[]"/><set_value name="$drops" exact="[]"/>
 <set_value name="$shots" exact="[0,0]"/><set_value name="$busy" exact="false"/><set_value name="$docking" exact="[false,false]"/>
 <do_if value="not md.X4LAN_Bubble.PrepareShips.$own?"><apply_loadout object="$proxy" loadout="$local.loadout"/></do_if>
 <get_loadout loadout="'x4ep1_gamestart_trade_mercury'" macro="macro.ship_arg_m_trans_container_01_a_macro" result="$targetLoadout"/>
 <do_all exact="2" counter="$i">
  <set_value name="$ship" exact="$ships.{$i}"/>
  <set_object_name object="$ship" name="'LAN Player %s'.[$i]"/>
  <reset_weapongroups object="$ship"/><activate_weapongroup object="$ship" weapongroup="0" primary="true"/>
  <warp object="$ship" sector="$local.sector"><position x="($i * 2 - 3) * 1km" y="10km" z="0m"/><rotation yaw="0deg" pitch="0deg" roll="0deg"/></warp>
  <set_object_velocity object="$ship"><linear x="0" y="0" z="0"/></set_object_velocity>
  <create_ship name="$target" macro="macro.ship_arg_m_trans_container_01_a_macro" sector="$local.sector">
   <owner exact="faction.ownerless"/><loadout loadout="$targetLoadout"/>
   <position x="($i * 2 - 3) * 1km" y="10km" z="600m"/>
  </create_ship>
  <set_object_name object="$target" name="'LAN Target %s'.[$i]"/><append_to_list name="$targets" exact="$target"/>
 </do_all>
 <add_to_group groupname="$weapons1" list="$ships.{1}.weapons.operational.list"/>
 <add_to_group groupname="$weapons2" list="$ships.{2}.weapons.operational.list"/>
 <do_if value="md.X4LAN_SeedDocks.State.$testStation?">
  <set_value name="$station" exact="md.X4LAN_SeedDocks.State.$testStation"/>
 </do_if>
 <do_else><create_station name="$station" macro="macro.station_gen_factory_base_01_macro" sector="$local.sector" owner="faction.player" state="componentstate.operational" constructionplan="'x4lan_market'" rawname="'LAN Test Station'">
  <position x="0m" y="10km" z="5km"/>
 </create_station></do_else>
 <do_if value="$host"><add_cargo object="$station" ware="ware.energycells" exact="200"/></do_if>
 <create_ship name="$donor" macro="macro.ship_arg_m_trans_container_01_a_macro" sector="$local.sector">
  <owner exact="faction.player"/><pilot><select race="race.argon"/></pilot><position x="0m" y="11km" z="0m"/>
 </create_ship>
 <add_cargo object="$donor" ware="ware.energycells" exact="40"/>
 <do_all exact="2" counter="$i">
  <drop_cargo object="$donor" ware="ware.energycells" exact="20" groupname="$newDrops"/>
  <append_to_list name="$drops" exact="$newDrops.{1}"/>
  <warp object="$newDrops.{1}" sector="$local.sector"><position x="($i * 2 - 3) * 1km + 150m" y="10km" z="60m"/></warp>
  <clear_group group="$newDrops"/>
 </do_all>
 <debug_text text="'[X4LAN] BUBBLE_READY host=%s p1=%s p2=%s station_stock=%s'.[$host,$ships.{1},$ships.{2},$station.cargo.{ware.energycells}.count]"/>
 <raise_lua_event name="'X4LAN_Bubble.Ready'"/>
</actions>
<cues>
 <cue name="Shots1" instantiate="true"><conditions><event_weapon_fired group="$weapons1"/></conditions><actions>
  <set_value name="$shots.{1}" operation="add" exact="1"/>
  <do_if value="$localSlot == 1"><raise_lua_event name="'X4LAN_Bubble.Fire'"/></do_if>
 </actions></cue>
 <cue name="Shots2" instantiate="true"><conditions><event_weapon_fired group="$weapons2"/></conditions><actions>
  <set_value name="$shots.{2}" operation="add" exact="1"/>
  <do_if value="$localSlot == 2"><raise_lua_event name="'X4LAN_Bubble.Fire'"/></do_if>
 </actions></cue>
 <cue name="FireOn" instantiate="true"><conditions><event_ui_triggered screen="'X4LAN_Bubble'" control="'FireOn'"/></conditions><actions><shoot object="$proxy"/></actions></cue>
 <cue name="FireOff" instantiate="true"><conditions><event_ui_triggered screen="'X4LAN_Bubble'" control="'FireOff'"/></conditions><actions><stop_shooting object="$proxy"/></actions></cue>
 <cue name="Publish" instantiate="true" checkinterval="0.2s">
  <conditions><check_value value="$host"/></conditions>
  <actions>
   <set_value name="$t1h" exact="0"/><set_value name="$t1s" exact="0"/><set_value name="$t2h" exact="0"/><set_value name="$t2s" exact="0"/>
   <do_if value="$targets.{1}.exists"><set_value name="$t1h" exact="$targets.{1}.hullpercentage"/><set_value name="$t1s" exact="$targets.{1}.shieldpercentage"/></do_if>
   <do_if value="$targets.{2}.exists"><set_value name="$t2h" exact="$targets.{2}.hullpercentage"/><set_value name="$t2s" exact="$targets.{2}.shieldpercentage"/></do_if>
   <set_value name="$d1" exact="if $ships.{1}.dock or $docking.{1} then 1 else 0"/><set_value name="$d2" exact="if $ships.{2}.dock or $docking.{2} then 1 else 0"/>
   <raise_lua_event name="'X4LAN_Bubble.World'" param="'%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s'.[$ships.{1}.hullpercentage,$ships.{1}.shieldpercentage,$ships.{1}.cargo.{ware.energycells}.count,$ships.{2}.hullpercentage,$ships.{2}.shieldpercentage,$ships.{2}.cargo.{ware.energycells}.count,$t1h,$t1s,$t2h,$t2s,$station.cargo.{ware.energycells}.count,if $drops.{1}.exists then 1 else 0,if $drops.{2}.exists then 1 else 0,$d1,$d2,$shots.{1},$shots.{2}]"/>
  </actions>
 </cue>
 <cue name="Command" instantiate="true" namespace="this">
  <conditions><event_ui_triggered screen="'X4LAN_Bubble'" control="'Command'"/><check_value value="Arena.$host"/></conditions>
  <actions>
   <set_value name="$args" exact="event.param3"/>
   <set_value name="$slot" exact="$args.$slot"/><set_value name="$ship" exact="Arena.$ships.{$slot}"/>
   <do_if value="Arena.$busy"><raise_lua_event name="'X4LAN_Bubble.Result'" param="'%s|BUSY'.[$args.$request]"/></do_if>
   <do_elseif value="$args.$verb == 'PICKUP'">
    <set_value name="$drop" exact="Arena.$drops.{$slot}"/>
    <do_if value="$drop.exists and $ship.distanceto.{$drop} le 250m and ($ship.cargo.{ware.energycells}.max - $ship.cargo.{ware.energycells}.count) ge 20">
     <set_value name="$before" exact="$ship.cargo.{ware.energycells}.count"/>
     <collect_drop object="$ship" drop="$drop"/>
     <do_if value="$ship.cargo.{ware.energycells}.count == $before + 20">
      <raise_lua_event name="'X4LAN_Bubble.Result'" param="'%s|OK'.[$args.$request]"/>
      <debug_text text="'[X4LAN] HOST_PICKUP slot=%s before=%s after=%s'.[$slot,$before,$ship.cargo.{ware.energycells}.count]"/>
     </do_if>
     <do_else><raise_lua_event name="'X4LAN_Bubble.Result'" param="'%s|REJECTED'.[$args.$request]"/></do_else>
    </do_if>
    <do_else><raise_lua_event name="'X4LAN_Bubble.Result'" param="'%s|REJECTED'.[$args.$request]"/></do_else>
   </do_elseif>
   <do_elseif value="$args.$verb == 'TRADE' and $ship.distanceto.{Arena.$station} lt 10km and Arena.$station.cargo.{ware.energycells}.count ge 30 and ($ship.cargo.{ware.energycells}.max - $ship.cargo.{ware.energycells}.count) ge 30">
    <set_value name="Arena.$busy" exact="true"/><set_value name="Arena.$docking.{$slot}" exact="true"/><do_if value="$ship.owner != faction.player"><set_owner object="$ship" faction="faction.player"/></do_if>
    <start_script object="Arena.$donor.pilot" name="'x4lan.bubble.trade'">
     <param name="ship" value="$ship"/><param name="station" value="Arena.$station"/><param name="request" value="$args.$request"/><param name="slot" value="$slot"/>
    </start_script>
   </do_elseif>
   <do_else><raise_lua_event name="'X4LAN_Bubble.Result'" param="'%s|REJECTED'.[$args.$request]"/></do_else>
  </actions>
 </cue>
 <cue name="TradeFinished" instantiate="true"><conditions><event_ui_triggered screen="'X4LAN_Bubble'" control="'TradeFinished'"/></conditions><actions><set_value name="$busy" exact="false"/><set_value name="$docking.{event.param3}" exact="false"/></actions></cue>
 <cue name="Apply" instantiate="true" namespace="this">
  <conditions><event_ui_triggered screen="'X4LAN_Bubble'" control="'Apply'"/><check_value value="not Arena.$host"/></conditions>
  <actions>
   <set_value name="$v" exact="event.param3.$values"/>
   <do_all exact="2" counter="$i">
    <set_value name="$ship" exact="Arena.$ships.{$i}"/>
    <set_object_hull object="$ship" exact="$v.{($i - 1) * 3 + 1}"/><set_object_shield object="$ship" exact="$v.{($i - 1) * 3 + 2}"/>
    <set_value name="$delta" exact="$v.{$i * 3} - $ship.cargo.{ware.energycells}.count"/>
    <do_if value="$delta gt 0"><add_cargo object="$ship" ware="ware.energycells" exact="$delta"/></do_if>
    <do_elseif value="$delta lt 0"><remove_cargo object="$ship" ware="ware.energycells" exact="-$delta"/></do_elseif>
    <do_if value="Arena.$targets.{$i}.exists">
     <set_object_hull object="Arena.$targets.{$i}" exact="$v.{5 + $i * 2}"/>
     <set_object_shield object="Arena.$targets.{$i}" exact="$v.{6 + $i * 2}"/>
    </do_if>
    <do_if value="$v.{11 + $i} == 0 and Arena.$drops.{$i}.exists"><destroy_object object="Arena.$drops.{$i}" explosion="false"/></do_if>
    <do_if value="$v.{13 + $i} == 1 and not $ship.dock">
     <do_if value="$ship.owner != faction.player"><set_owner object="$ship" faction="faction.player"/></do_if><do_if value="not $ship.assigneddock"><request_docking ship="$ship" container="Arena.$station" requiretrading="true" queuedresult="$queued" grantedresult="$granted"/></do_if>
     <do_if value="$ship.assigneddock"><dock ship="$ship"/></do_if>
    </do_if>
   </do_all>
   <set_value name="$delta" exact="$v.{11} - Arena.$station.cargo.{ware.energycells}.count"/>
   <do_if value="$delta gt 0"><add_cargo object="Arena.$station" ware="ware.energycells" exact="$delta"/></do_if>
   <do_elseif value="$delta lt 0"><remove_cargo object="Arena.$station" ware="ware.energycells" exact="-$delta"/></do_elseif>
  </actions>
 </cue>
 <cue name="Readback" instantiate="true" checkinterval="5s"><conditions><check_value value="true"/></conditions><actions>
  <debug_text text="'[X4LAN] WORLD_READBACK host=%s cargo1=%s cargo2=%s stock=%s target1_shield=%s target2_shield=%s shots1=%s shots2=%s drops1=%s drops2=%s dock1=%s dock2=%s'.[$host,$ships.{1}.cargo.{ware.energycells}.count,$ships.{2}.cargo.{ware.energycells}.count,$station.cargo.{ware.energycells}.count,@$targets.{1}.shieldpercentage,@$targets.{2}.shieldpercentage,$shots.{1},$shots.{2},if $drops.{1}.exists then 1 else 0,if $drops.{2}.exists then 1 else 0,if $ships.{1}.dock then 1 else 0,if $ships.{2}.dock then 1 else 0]"/>
 </actions></cue>
</cues></cue>
<!-- Shared ship spec: Lua spawns the local ship and the proxy from one configured
     macro. Both receive the same loadout before Arena setup; nothing is substituted. -->
<cue name="PrepareShips" namespace="this">
<conditions><event_ui_triggered screen="'X4LAN_Bubble'" control="'PrepareShips'" /></conditions>
<actions>
 <set_value name="$args" exact="event.param3" /><set_value name="$candidate" exact="null" /><set_value name="$peer" exact="null" />
 <set_value name="$macro" exact="macro.{$args.$macro}" />
 <set_value name="$refusal" exact="''" />
 <do_if value="not $macro or not $args.$sector.exists"><set_value name="$refusal" exact="'macro/sector (X4: Timelines required)'" /></do_if>
 <do_else>
  <get_loadout loadout="$args.$loadout" macro="$macro" result="$loadout" />
  <do_if value="not $loadout"><set_value name="$refusal" exact="'loadout'" /></do_if>
  <do_else>
   <!-- Full native creation is needed for sensor discovery. -->
   <create_ship name="$candidate" macro="$macro" sector="$args.$sector">
    <owner exact="faction.player"/><loadout loadout="$loadout"/>
    <position x="$args.$localX" y="10km" z="0m"/>
   </create_ship>
   <create_ship name="$peer" macro="$macro" sector="$args.$sector">
    <owner exact="faction.player"/><loadout loadout="$loadout"/>
    <position x="-$args.$localX" y="10km" z="0m"/>
   </create_ship>
   <do_if value="$candidate.software.dock != ware.software_dockmk2 or $peer.software.dock != ware.software_dockmk2 or $candidate.weapons.operational.count != $args.$weapons or ($candidate.weapons.operational.count lt $candidate.weapons.numslots and $candidate.ammostorage.missile.capacity != 0) or $candidate.weapons.operational.count != $peer.weapons.operational.count or $candidate.shields.operational.count == 0 or $candidate.shields.operational.count != $peer.shields.operational.count or $candidate.engines.operational.count == 0 or $candidate.engines.operational.count != $peer.engines.operational.count or $candidate.maxhull != $peer.maxhull or $candidate.maxshield != $peer.maxshield"><set_value name="$refusal" exact="'equipment'" /></do_if>
  </do_else>
 </do_else>
 <do_all exact="2" counter="$i">
  <set_value name="$ship" exact="if $i == 1 then $candidate else $peer" />
  <debug_text text="'[X4LAN] SHIP_SPEC role=%s macro=%s idcode=%s maxhull=%s maxshield=%s weapons=%s/%s expected=%s missilecapacity=%s weapon=%s shields=%s shield=%s engines=%s engine=%s dock=%s software=%s cargo=%s maxspeed=%s'.[if $i == 1 then 'local' else 'proxy',@$ship.macro.id,@$ship.idcode,@$ship.maxhull,@$ship.maxshield,@$ship.weapons.operational.count,@$ship.weapons.numslots,$args.$weapons,@$ship.ammostorage.missile.capacity,@$ship.weapons.operational.list.{1}.macro.id,@$ship.shields.operational.count,@$ship.shields.operational.list.{1}.macro.id,@$ship.engines.operational.count,@$ship.engines.operational.list.{1}.macro.id,@$ship.software.dock,@$ship.software.installed,@$ship.cargo.capacity.all,@$ship.maxspeed]" />
 </do_all>
 <do_if value="$refusal != ''">
  <debug_text text="'[X4LAN] SHIP_SPEC_REFUSED reason=%s macro=%s loadout=%s'.[$refusal,$args.$macro,$args.$loadout]" />
  <do_if value="$candidate.exists"><destroy_object object="$candidate" explosion="false" /></do_if>
  <do_if value="$peer.exists"><destroy_object object="$peer" explosion="false" /></do_if>
  <remove_value name="$candidate" />
  <raise_lua_event name="'X4LAN_Bubble.ShipsRefused'" param="$refusal" />
 </do_if>
 <do_else>
  <set_value name="$own" exact="$candidate" />
  <raise_lua_event name="'X4LAN_Bubble.PreparedOwn'" param="$candidate" />
  <raise_lua_event name="'X4LAN_Bubble.PreparedProxy'" param="$peer" />
  <teleport_player object="$own" instant="true" force="true" />
  <raise_lua_event name="'X4LAN_Bubble.ShipsReady'" />
 </do_else>
</actions>
</cue>
</cues></mdscript>
'''
(R/'md/x4lan_bubble.xml').write_text(coop_bubble(guard_bubble(xml)))
(R/'aiscripts/x4lan.bubble.trade.xml').write_text('''<?xml version="1.0" encoding="utf-8"?>
<aiscript name="x4lan.bubble.trade" version="1" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="aiscripts.xsd">
<params><param name="ship"/><param name="station"/><param name="request"/><param name="slot"/></params>
<attention min="unknown"><actions>
 <request_docking ship="$ship" container="$station" requiretrading="true" queuedresult="$queued" grantedresult="$granted"/>
 <wait exact="3s"/>
 <do_if value="$ship.assigneddock"><dock ship="$ship"/></do_if>
 <wait exact="1s"/>
 <set_value name="$result" exact="false"/>
 <set_value name="$before" exact="$ship.cargo.{ware.energycells}.count"/>
 <do_if value="$ship.dock and $ship.dock.container == $station">
  <create_trade_offer name="$offer" object="$station" seller="$station" ware="ware.energycells" amount="30" price="10Cr" playeronly="true" virtual="false" virtualmoney="true"/>
  <execute_trade tradeoffer="$offer" tradepartner="$ship" amount="30" custom="true" result="$result"/>
 </do_if>
 <do_if value="$result and $ship.cargo.{ware.energycells}.count == $before + 30">
  <raise_lua_event name="'X4LAN_Bubble.Result'" param="'%s|OK'.[$request]"/>
 </do_if>
 <do_else><raise_lua_event name="'X4LAN_Bubble.Result'" param="'%s|REJECTED'.[$request]"/></do_else>
 <raise_lua_event name="'X4LAN_Bubble.TradeFinished'" param="$slot"/>
 <debug_text text="'[X4LAN] HOST_TRADE slot=%s result=%s before=%s after=%s stock=%s'.[$slot,$result,$before,$ship.cargo.{ware.energycells}.count,$station.cargo.{ware.energycells}.count]"/>
</actions></attention></aiscript>
''')
for p in [R/'md/x4lan_bubble.xml',R/'aiscripts/x4lan.bubble.trade.xml']:E.parse(p)
print('Bubble scripts written and parsed.')
