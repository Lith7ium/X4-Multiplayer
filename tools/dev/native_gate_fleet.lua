-- Native commander/assignment state using fixture identities, never local IDs.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.native_gate_fleet_state then return end
X4LAN_NATIVE_FLEET=function(object,actors)
 local raw=GetCommander(object)
 local commander=raw and ConvertIDTo64Bit(raw)
 if not commander or commander==0 then return 'FLEET_1;0;none;0' end
 local identity
 for i=1,12 do if actors[i]==commander then identity=i;break end end
 assert(identity,'commander is outside registered fixture')
 assert(commander~=object,'native commander cycle')
 local assignment,group=GetComponentData(object,'assignment','subordinategroup')
 assert(type(assignment)=='string' and #assignment>0 and #assignment<=48 and
        not assignment:find('[^%w_.%-]'),'invalid native assignment')
 group=tonumber(group)
 assert(group and group>=0 and group<=10 and group==math.floor(group),'invalid native subordinate group')
 return string.format('FLEET_1;%d;%s;%d',identity,assignment,group)
end
