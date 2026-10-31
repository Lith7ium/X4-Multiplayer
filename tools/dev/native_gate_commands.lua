-- Disposable tactical-command adapter. This is not direct cockpit control.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.native_gate_command_test then return end
local mode=cfg.native_gate_command_test
assert(mode=='pvp' or mode=='coop','unknown native command test')
local plan=cfg.native_gate_command_plan or 'combat'
assert(plan=='combat' or plan=='maneuvers' or plan=='travel' or plan=='withdraw' or plan=='docking' or plan=='fleet' or plan=='live' or plan=='replay','unknown native command plan')
local dockexit=cfg.native_gate_docking_exit or 'diagonal'
assert(dockexit=='diagonal' or (dockexit=='above' and plan=='docking' and cfg.native_gate_docking_state),'invalid docking departure fixture')
local own=tonumber(assert(cfg.slot):sub(2))
local function decode(command,slot)
 assert(type(command)=='string' and #command==8 and command:match('^[0-9a-f]+$'),'invalid command encoding')
 if command=='00000000' then return end
 local op,actor,target,reserved=tonumber(command:sub(1,2),16),tonumber(command:sub(3,4),16),tonumber(command:sub(5,6),16),tonumber(command:sub(7,8),16)
 assert(reserved==0 and actor>=1 and actor<=12,'invalid command actor')
 local owner=mode=='pvp' and (actor<=6 and 1 or 2) or (actor<=6 and (actor%2==1 and 1 or 2) or 0)
 assert(owner==slot,'command actor belongs to another player')
 if op==8 then assert(cfg.native_gate_boost_diag,'withdraw requires native boost capture') end
 if op==9 then assert(cfg.native_gate_docking_state and target==slot,'dock requires native anchor state and own destination') end
 if op==6 or op==7 then assert(cfg.native_gate_fleet_state,'fleet commands require native fleet state capture') end
 assert(((op==1 or op==4 or op==6) and target>=1 and target<=12 and target~=actor) or
        ((op==2 or op==7 or op==8) and target==0) or ((op==3 or op==5 or op==9) and (target==1 or target==2)),'invalid tactical command')
 if op==1 then
  assert((actor<=6)~=(target<=6),'attack target is on the same team')
 elseif op==4 or op==6 then
  assert((actor<=6)==(target<=6),'escort target is on the other team')
 end
 return {op=op,actor=actor,target=target,slot=slot,mode=mode,dockexit=dockexit}
end
local plans={
 [1]={[60]='02010000',[120]='01010700',[240]='02030000',[300]='01030900'},
 [2]=mode=='pvp' and {[90]='02070000',[150]='01070100',[270]='02090000',[330]='01090300'}
                       or {[90]='02020000',[150]='01020800',[270]='02040000',[330]='01040a00'},
}
if plan=='maneuvers' then
 plans={
  [1]={[60]='03010100',[120]='04030100',[240]='02010000',[300]='01010700'},
  [2]=mode=='pvp' and {[90]='03070200',[150]='04090700',[270]='02070000',[330]='01070100'}
                        or {[90]='03020200',[150]='04040200',[270]='02020000',[330]='01020800'},
 }
end
if plan=='withdraw' then
 plans={
  [1]={[30]='08010000',[120]='04030100',[1200]='02010000',[1500]='03010100'},
  [2]=mode=='pvp' and {[45]='08070000',[150]='04090700',[1230]='02070000',[1530]='03070200'}
                        or {[45]='08020000',[150]='04040200',[1230]='02020000',[1530]='03020200'},
 }
end
if plan=='docking' then
 plans={
  [1]={[30]='09010100',[6000]='03010100',[9000]='09010100',[15000]='03010100'},
  [2]=mode=='pvp' and {[45]='09070200',[6030]='03070200',[9030]='09070200',[15030]='03070200'}
                        or {[45]='09020200',[6030]='03020200',[9030]='09020200',[15030]='03020200'},
 }
end
if plan=='travel' then
 plans={
  [1]={[60]='05010100',[120]='04030100',[1800]='05010200',[3300]='02010000'},
  [2]=mode=='pvp' and {[90]='05070200',[150]='04090700',[1830]='05070100',[3330]='02070000'}
                        or {[90]='05020200',[150]='04040200',[1830]='05020100',[3330]='02020000'},
 }
end
if plan=='replay' then plans=assert(cfg.native_gate_command_replay,'replay plan missing') end
if plan=='fleet' then
 plans={
  [1]={[60]='06030100',[120]='06050100',[180]='03010100',[1200]='07030000',[1500]='06030500',[2400]='07050000',[2700]='02010000'},
  -- Move the opposing leader before assembling its wing: it is otherwise
  -- destroyed by the opening battle before its delayed movement command.
  [2]=mode=='pvp' and {[30]='03070200',[90]='06090700',[150]='060b0700',[1230]='07090000',[1530]='06090b00',[2430]='070b0000',[2730]='02090000'}
                        or {[90]='06040200',[150]='06060200',[210]='03020200',[1230]='07040000',[1530]='06040600',[2430]='07060000',[2730]='02020000'},
 }
end
X4LAN_NATIVE_COMMANDS={
 proposal=function(tick)
  local command=plan=='live' and assert(X4LAN_NATIVE_LIVE_INPUT,'live input reader missing')(tick) or plans[own][tick] or '00000000'
  decode(command,own)
  return command
 end,
 apply=function(tick,a,b)
  -- Validate the complete batch before enqueueing either player's command.
  local actions={decode(a,1),decode(b,2)}
  for slot=1,2 do
   local action=actions[slot]
   if action then
    action.tick=tick
    AddUITriggeredEvent('X4LAN_NativeCommands','Apply',action)
   end
  end
  DebugError(string.format('[X4LAN] NATIVE_GATE INPUTS tick=%d mode=%s p1=%s p2=%s',tick,mode,a,b))
  return 'INPUTS_1:'..mode..':'..a..':'..b
 end,
}
