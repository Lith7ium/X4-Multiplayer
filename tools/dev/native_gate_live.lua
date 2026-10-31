-- Loopback input capture for the opt-in disposable native fixture.
-- Capture happens in the client; the regular immutable barrier admits it later.
local cfg=X4LAN_CONFIG
if not cfg or cfg.native_gate_command_plan~='live' then return end
local connection,last_sequence,last_tick,previous_tick,previous_command=nil,0,0,-1,'00000000'
X4LAN_NATIVE_LIVE_INPUT=function(tick)
 assert(tick>=previous_tick,'live input capture moved backwards')
 if tick==previous_tick then return previous_command end
 if not connection then
  assert(cfg.host=='127.0.0.1' and (cfg.slot=='p1' or cfg.slot=='p2'),'live input requires loopback player slot')
  local net=assert(package.loadlib('./extensions/x4_lan_lab/native/x4lan_net_64.dll','luaopen_x4lan_net'))()
  connection=net.open(cfg.host,cfg.port)
 end
 local slot=cfg.slot:sub(2)
 assert(connection:send('NGPOLL|1|'..cfg.token..'|'..slot..'|'..last_sequence..'|'..last_tick),'live input poll failed')
 local command='00000000'
 for _=1,32 do
  local packet=connection:receive()
  if not packet then break end
  local token,player,sequence,value=packet:match('^NGNEXT|1|([0-9a-f]+)|([12])|(%d+)|([0-9a-f]+)$')
  if token==cfg.token and player==slot then
   local number=tonumber(sequence)
   assert(number and number>=1 and number<=4096 and tostring(number)==sequence and #value==8,'invalid live command packet')
   assert(number<=last_sequence+1,'live input sequence gap')
   if number==last_sequence+1 then
    last_sequence,last_tick,command=number,tick,value
    DebugError('[X4LAN] NATIVE_GATE INPUT_CAPTURE slot='..slot..' seq='..number..' tick='..tick..' command='..value)
    break
   end
  end
 end
 previous_tick,previous_command=tick,command
 return command
end
