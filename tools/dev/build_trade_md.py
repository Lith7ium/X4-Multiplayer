from pathlib import Path
import xml.etree.ElementTree as E
from life_guards import guard_trade
from coop_guards import trade as coop_trade
R=Path(__file__).resolve().parents[2]/'src/extension/x4_lan_lab'
xml='''<?xml version="1.0" encoding="utf-8"?>
<mdscript name="X4LAN_Trade" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="md.xsd">
<cues><cue name="Market" namespace="this">
<conditions><event_ui_triggered screen="'X4LAN_Trade'" control="'Setup'"/></conditions>
<actions>
 <set_value name="$arena" exact="md.X4LAN_Bubble.Arena"/>
 <set_value name="$host" exact="$arena.$host"/><set_value name="$ships" exact="$arena.$ships"/>
 <set_value name="$arena.$economy" exact="true"/><set_value name="$busy" exact="false"/><set_value name="$lastRemoteDock" exact="-1"/>
 <set_value name="$wares" exact="[ware.energycells,ware.foodrations,ware.medicalsupplies,ware.hullparts,ware.refinedmetals,ware.siliconwafers]"/>
 <set_value name="$markets" exact="[$arena.$station]"/><set_value name="$offers" exact="[[],[]]"/>
 <set_value name="$bids" exact="[[12Cr,16Cr,52Cr,167Cr,118Cr,239Cr],[17Cr,23Cr,72Cr,229Cr,162Cr,328Cr]]"/><set_value name="$asks" exact="[[14Cr,18Cr,59Cr,188Cr,133Cr,269Cr],[19Cr,25Cr,79Cr,250Cr,177Cr,358Cr]]"/>
 <set_owner object="$markets.{1}" faction="faction.argon"/>
 <set_object_name object="$markets.{1}" name="'LAN Trading Station Alpha'"/>
 <do_if value="md.X4LAN_SeedDocks.State.$testStation2?">
  <set_value name="$station2" exact="md.X4LAN_SeedDocks.State.$testStation2"/>
 </do_if>
 <do_else><create_station name="$station2" macro="macro.station_gen_factory_base_01_macro" sector="player.ship.sector" owner="faction.player" state="componentstate.operational" constructionplan="'x4lan_market'" rawname="'LAN Trading Station Beta'">
  <position x="8km" y="10km" z="5km"/>
 </create_station></do_else>
 <set_owner object="$station2" faction="faction.argon"/>
 <set_object_name object="$station2" name="'LAN Trading Station Beta'"/>
 <append_to_list name="$markets" exact="$station2"/>
 <set_object_account object="$arena.$proxy"/>
 <reward_player money="2200000Cr - player.money"/>
 <transfer_money from="faction.player" to="$arena.$proxy" amount="100000Cr"/>
 <do_all exact="2" counter="$m">
  <set_value name="$station" exact="$markets.{$m}"/>
  <set_object_docking_enabled object="$station" enabled="true"/>
  <set_known object="$station" known="true"/><add_trade_subscription object="$station"/>
  <set_object_account object="$station"/><transfer_money from="faction.player" to="$station" amount="1000000Cr"/>
  <do_all exact="$wares.count" counter="$w">
   <set_value name="$ware" exact="$wares.{$w}"/>
   <remove_cargo object="$station" ware="$ware" exact="$station.cargo.{$ware}.count"/>
   <do_if value="$host"><add_cargo object="$station" ware="$ware" exact="200 + $m * 100"/></do_if>
   <set_value name="$bid" exact="$bids.{$m}.{$w}"/><set_value name="$ask" exact="$asks.{$m}.{$w}"/>
   <create_trade_offer name="$sell" object="$station" seller="$station" ware="$ware" amount="1000000" price="$ask" missioncue="Market" playeronly="true" virtual="false" virtualmoney="true"/>
   <create_trade_offer name="$buy" object="$station" buyer="$station" ware="$ware" amount="2000" price="$bid" missioncue="Market" playeronly="true" virtual="false" virtualmoney="true"/>
   <append_to_list name="$offers.{$m}" exact="[$buy,$sell]"/>
  </do_all>
 </do_all>
 <raise_lua_event name="'X4LAN_Trade.Market1'" param="$markets.{1}"/>
 <raise_lua_event name="'X4LAN_Trade.Market2'" param="$markets.{2}"/>
 <raise_lua_event name="'X4LAN_Trade.Ready'"/>
 <debug_text text="'[X4LAN] TRADE_READY host=%s player_money=%s proxy_money=%s markets=%s,%s'.[$host,player.money,$arena.$proxy.money,$markets.{1}.money,$markets.{2}.money]"/>
</actions>
<cues>
 <cue name="Tick" instantiate="true" checkinterval="0.5s"><conditions><check_value value="not $busy"/></conditions><actions>
  <set_value name="$localdock" exact="'none'"/>
  <do_all exact="2" counter="$m">
   <set_value name="$station" exact="$markets.{$m}"/>
   <do_if value="$arena.$local.dock and $arena.$local.dock.container == $station"><set_value name="$localdock" exact="'m%s'.[$m]"/></do_if>

  </do_all>
  <raise_lua_event name="'X4LAN_Trade.LocalDock'" param="$localdock"/>
  <do_if value="$host">
   <set_value name="$snapshot" exact="'M,%s,%s,%s,%s'.[(player.money / 1ct)i,($ships.{2}.money / 1ct)i,($markets.{1}.money / 1ct)i,($markets.{2}.money / 1ct)i]"/>
   <do_all exact="$wares.count" counter="$w">
    <set_value name="$ware" exact="$wares.{$w}"/>
    <set_value name="$snapshot" exact="$snapshot + ';W,%s,%s,%s,%s,%s,%s,%s,%s,2000,%s,%s,2000'.[$ware.id,$ware.volume,$ships.{1}.cargo.{$ware}.count,$ships.{2}.cargo.{$ware}.count,$markets.{1}.cargo.{$ware}.count,$markets.{2}.cargo.{$ware}.count,($bids.{1}.{$w}/1ct)i,($asks.{1}.{$w}/1ct)i,($bids.{2}.{$w}/1ct)i,($asks.{2}.{$w}/1ct)i]"/>
   </do_all>
   <raise_lua_event name="'X4LAN_Trade.Snapshot'" param="$snapshot"/>
  </do_if>
 </actions></cue>
 <cue name="RemoteDock" instantiate="true" namespace="this"><conditions><event_ui_triggered screen="'X4LAN_Trade'" control="'RemoteDock'"/></conditions><actions>
  <set_value name="$m" exact="event.param3"/><set_value name="$ship" exact="Market.$arena.$proxy"/>
  <do_if value="$m == 0">
   <do_if value="$ship.dock"><undock ship="$ship"/></do_if>
   <remove_docking_request ship="$ship" container="Market.$markets.{1}" immediate="true"/>
   <remove_docking_request ship="$ship" container="Market.$markets.{2}" immediate="true"/>
  </do_if>
  <do_elseif value="$m ge 1 and $m le 2">
   <set_value name="$station" exact="Market.$markets.{$m}"/><set_value name="$queued" exact="false"/><set_value name="$granted" exact="false"/>
   <do_if value="$ship.dock and $ship.dock.container != $station"><undock ship="$ship"/></do_if>
   <do_if value="$ship.assigneddock and $ship.assigneddock.container != $station"><remove_docking_request ship="$ship" container="$ship.assigneddock.container" immediate="true"/></do_if>
   <do_if value="$ship.distanceto.{$station} lt 8km and not $ship.dock">
    <do_if value="$ship.owner != faction.player"><set_owner object="$ship" faction="faction.player"/></do_if><do_if value="not $ship.assigneddock"><request_docking ship="$ship" container="$station" requiretrading="true" allowplayeronly="true" queuedresult="$queued" grantedresult="$granted"/></do_if>
    <do_if value="$ship.assigneddock"><dock ship="$ship"/></do_if>
   </do_if>
   <do_if value="Market.$lastRemoteDock != $m">
    <debug_text text="'[X4LAN] REMOTE_DOCK market=%s dock=%s assigned=%s distance=%s queued=%s granted=%s enabled=%s allowed=%s'.[$m,$ship.dock,$ship.assigneddock,$ship.distanceto.{$station},$queued,$granted,$station.dockingenabled,$station.dockingallowed.{$ship}]"/>
   </do_if>
  </do_elseif>
  <set_value name="Market.$lastRemoteDock" exact="$m"/>
 </actions></cue>
 <cue name="Apply" instantiate="true" namespace="this"><conditions><event_ui_triggered screen="'X4LAN_Trade'" control="'Apply'"/><check_value value="not Market.$host"/></conditions><actions>
  <set_value name="$args" exact="event.param3"/>
  <set_value name="$accounts" exact="[Market.$arena.$proxy,Market.$markets.{1},Market.$markets.{2}]"/>
  <set_value name="$balances" exact="[$args.$balances.{1},$args.$balances.{3},$args.$balances.{4}]"/>
  <do_all exact="3" counter="$j">
   <set_value name="$account" exact="$accounts.{$j}"/><set_value name="$delta" exact="$balances.{$j} * 1ct - $account.money"/>
   <do_if value="$delta gt 0"><reward_player money="$delta"/><transfer_money from="faction.player" to="$account" amount="$delta"/></do_if>
   <do_elseif value="$delta lt 0"><transfer_money from="$account" to="faction.player" amount="-$delta"/></do_elseif>
  </do_all>
  <reward_player money="$args.$money * 1ct - player.money"/>
  <do_all exact="$args.$rows.count" counter="$i">
   <set_value name="$r" exact="$args.$rows.{$i}"/><set_value name="$ware" exact="Market.$wares.{$r.$index}"/>
   <set_value name="$objects" exact="[Market.$ships.{1},Market.$ships.{2},Market.$markets.{1},Market.$markets.{2}]"/>
   <do_all exact="4" counter="$j">
    <set_value name="$object" exact="$objects.{$j}"/><set_value name="$delta" exact="$r.$cargo.{$j} - $object.cargo.{$ware}.count"/>
    <do_if value="$delta gt 0"><add_cargo object="$object" ware="$ware" exact="$delta"/></do_if>
    <do_elseif value="$delta lt 0"><remove_cargo object="$object" ware="$ware" exact="-$delta"/></do_elseif>
   </do_all>
  </do_all>
 </actions></cue>
 <cue name="Order" instantiate="true" namespace="this"><conditions><event_ui_triggered screen="'X4LAN_Trade'" control="'Order'"/><check_value value="Market.$host"/></conditions><actions>
  <set_value name="$args" exact="event.param3"/><set_value name="$status" exact="'OK'"/>
  <set_value name="$slot" exact="$args.$slot"/><set_value name="$m" exact="$args.$market"/>
  <set_value name="$ship" exact="Market.$ships.{$slot}"/><set_value name="$station" exact="Market.$markets.{$m}"/>
  <set_value name="$sellvalue" exact="0ct"/><set_value name="$buyvalue" exact="0ct"/><set_value name="$sellvol" exact="0"/><set_value name="$buyvol" exact="0"/>
  <set_value name="$lines" exact="[]"/>
  <do_if value="Market.$busy or Market.$arena.$busy"><set_value name="$status" exact="'BUSY'"/></do_if>
  <do_elseif value="not $ship.dock or $ship.dock.container != $station"><set_value name="$status" exact="'DOCK'"/></do_elseif>
  <do_all exact="$args.$lines.count" counter="$i">
   <set_value name="$line" exact="$args.$lines.{$i}"/><set_value name="$w" exact="$line.$index"/><set_value name="$ware" exact="Market.$wares.{$w}"/>
   <set_value name="$qty" exact="$line.$quantity"/><set_value name="$price" exact="(if $qty gt 0 then Market.$asks.{$m}.{$w} else Market.$bids.{$m}.{$w})"/>
   <do_if value="$line.$price * 1ct != $price"><set_value name="$status" exact="'PRICE'"/></do_if>
   <do_if value="$qty gt 0">
    <do_if value="$station.cargo.{$ware}.count lt $qty"><set_value name="$status" exact="'STOCK'"/></do_if>
    <set_value name="$buyvalue" operation="add" exact="$qty * $price"/><set_value name="$buyvol" operation="add" exact="$qty * $ware.volume"/>
   </do_if>
   <do_else>
    <do_if value="$ship.cargo.{$ware}.count lt -$qty or $station.cargo.{$ware}.count - $qty gt 2000"><set_value name="$status" exact="'STOCK'"/></do_if>
    <set_value name="$sellvalue" operation="add" exact="-$qty * $price"/><set_value name="$sellvol" operation="add" exact="-$qty * $ware.volume"/>
   </do_else>
   <append_to_list name="$lines" exact="[$ware,$qty,$price]"/>
  </do_all>
  <set_value name="$account" exact="if $slot == 1 then faction.player else $ship"/>
  <do_if value="$account.money + $sellvalue lt $buyvalue or $station.money lt $sellvalue"><set_value name="$status" exact="'FUNDS'"/></do_if>
  <do_if value="$ship.cargo.free.container + $sellvol lt $buyvol or $station.cargo.free.container lt $sellvol"><set_value name="$status" exact="'CAPACITY'"/></do_if>
  <do_if value="$status == 'OK'">
   <set_value name="Market.$busy" exact="true"/><set_value name="Market.$arena.$busy" exact="true"/>
   <start_script object="Market.$arena.$donor.pilot" name="'x4lan.market.trade'">
    <param name="ship" value="$ship"/><param name="station" value="$station"/><param name="request" value="$args.$request"/>
    <param name="lines" value="$lines"/><param name="account" value="$account"/><param name="coop_delivery" value="Market.$arena.$coop and $m == 2"/>
   </start_script>
  </do_if>
  <do_else><raise_lua_event name="'X4LAN_Trade.Result'" param="'%s|%s'.[$args.$request,$status]"/></do_else>
 </actions></cue>
 <cue name="Finished" instantiate="true"><conditions><event_ui_triggered screen="'X4LAN_Trade'" control="'Finished'"/></conditions><actions><set_value name="$busy" exact="false"/><set_value name="$arena.$busy" exact="false"/></actions></cue>
 <cue name="Notice" instantiate="true"><conditions><event_ui_triggered screen="'X4LAN_Trade'" control="'Notice'"/></conditions><actions><show_notification text="event.param3" timeout="8s" priority="8"/></actions></cue>
 <cue name="Readback" instantiate="true" checkinterval="5s"><conditions><check_value value="true"/></conditions><actions>
  <debug_text text="'[X4LAN] ECON_READBACK host=%s money=%s,%s stock=%s,%s cargo=%s,%s busy=%s'.[$host,player.money,$arena.$proxy.money,$markets.{1}.cargo.{ware.energycells}.count,$markets.{2}.cargo.{ware.energycells}.count,$ships.{1}.cargo.{ware.energycells}.count,$ships.{2}.cargo.{ware.energycells}.count,$busy]"/>
 </actions></cue>
</cues></cue></cues></mdscript>
'''
# Available quantity must not be forced to zero by a zero in a minimum list.
xml=xml.replace('[0,2000 - $station.cargo.{$ware}.count,','[2000 - $station.cargo.{$ware}.count,')
(R/'md/x4lan_trade.xml').write_text(coop_trade(guard_trade(xml)))
ai='''<?xml version="1.0" encoding="utf-8"?>
<aiscript name="x4lan.market.trade" version="1" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="aiscripts.xsd">
<params><param name="ship"/><param name="station"/><param name="request"/><param name="lines"/><param name="account"/><param name="coop_delivery" default="false"/></params>
<attention min="unknown"><actions>
 <set_value name="$status" exact="'OK'"/><set_value name="$completed" exact="0"/>
 <do_all exact="2" counter="$phase">
  <do_all exact="$lines.count" counter="$i">
   <set_value name="$line" exact="$lines.{$i}"/><set_value name="$ware" exact="$line.{1}"/><set_value name="$qty" exact="$line.{2}"/><set_value name="$price" exact="$line.{3}"/>
   <do_if value="$status == 'OK' and (($phase == 1 and $qty lt 0) or ($phase == 2 and $qty gt 0))">
    <set_value name="$before" exact="$ship.cargo.{$ware}.count"/><set_value name="$stockbefore" exact="$station.cargo.{$ware}.count"/>
    <set_value name="$amount" exact="if $qty gt 0 then $qty else -$qty"/>
    <set_value name="$result" exact="false"/><set_value name="$paid" exact="0ct"/>
    <set_value name="$moneybefore" exact="$account.money"/><set_value name="$stationmoneybefore" exact="$station.money"/>
    <do_if value="$ship.dock and $ship.dock.container == $station">
     <do_if value="$qty gt 0"><create_trade_offer name="$offer" object="$station" seller="$station" ware="$ware" amount="$amount" price="$price" playeronly="true" virtual="false" virtualmoney="true"/></do_if>
     <do_else><create_trade_offer name="$offer" object="$station" buyer="$station" ware="$ware" amount="$amount" price="$price" playeronly="true" virtual="false" virtualmoney="true"/></do_else>
     <execute_trade tradeoffer="$offer" tradepartner="$ship" amount="$amount" custom="true" result="$result"/>
     <remove_trade_offer object="$station" tradeoffer="$offer"/>
    </do_if>
    <do_if value="$ship.cargo.{$ware}.count == $before + $qty and $station.cargo.{$ware}.count == $stockbefore - $qty">
     <do_if value="$qty gt 0"><transfer_money from="$account" to="$station" amount="$amount * $price" result="$paid"/></do_if>
     <do_else><transfer_money from="$station" to="$account" amount="$amount * $price" result="$paid"/></do_else>
     <set_value name="$completed" operation="add" exact="1"/>
     <do_if value="$coop_delivery and $qty lt 0 and $ware == ware.energycells">
      <raise_lua_event name="'X4LAN_Quest.Delivery'" param="'%s|%s'.[$request, -$qty]"/>
     </do_if>
     <do_if value="$account.money != $moneybefore - $qty * $price or $station.money != $stationmoneybefore + $qty * $price"><set_value name="$status" exact="'PARTIAL'"/></do_if>
    </do_if>
    <do_else><set_value name="$status" exact="if $completed gt 0 or $ship.cargo.{$ware}.count != $before or $station.cargo.{$ware}.count != $stockbefore then 'PARTIAL' else 'REJECTED'"/></do_else>
    <debug_text text="'[X4LAN] MARKET_LINE request=%s ware=%s qty=%s result=%s before=%s after=%s station_before=%s station_after=%s paid=%s status=%s'.[$request,$ware.id,$qty,$result,$before,$ship.cargo.{$ware}.count,$stockbefore,$station.cargo.{$ware}.count,$paid,$status]"/>
   </do_if>
  </do_all>
 </do_all>
 <raise_lua_event name="'X4LAN_Trade.Result'" param="'%s|%s'.[$request,$status]"/>
</actions></attention></aiscript>
'''
(R/'aiscripts/x4lan.market.trade.xml').write_text(ai)
for p in [R/'md/x4lan_trade.xml',R/'aiscripts/x4lan.market.trade.xml']:E.parse(p)
# Keep the original movement/action release valid when the market adapter is disabled.
p=R/'md/x4lan_bubble.xml';root=E.parse(p)
a=root.find('./cues/cue/actions')
if not any(e.get('name')=='$economy' for e in a):a.insert(0,E.Element('set_value',name='$economy',exact='false'))
apply=root.find(".//cue[@name='Apply']/actions")
for parent in list(apply.iter()):
 children=list(parent);selected=[]
 for child in children:
  iscargo=child.tag=='set_value' and child.get('name')=='$delta' or child.tag in ('do_if','do_elseif') and child.get('value','').startswith('$delta')
  isdock=child.tag=='do_if' and '$v.{13 + $i}' in child.get('value','')
  if iscargo or isdock:selected.append(child)
 if selected:
  gate=E.Element('do_if',value='not Arena.$economy');pos=list(parent).index(selected[0]);parent.insert(pos,gate)
  for child in selected:parent.remove(child);gate.append(child)
root.write(p,encoding='utf-8',xml_declaration=True)
print('Market MD and AI scripts generated.')
