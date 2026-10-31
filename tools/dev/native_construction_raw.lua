-- Parse passive native evidence. Raw bank transforms are not sector poses.
local M={}
local function fixedhex(value,length)
 assert(type(value)=='string' and #value==length and value:match('^[0-9a-f]+$'),'malformed passive raw hex')
 return value
end
function M.decode(raw,handle,slot,tick)
 assert(type(raw)=='string','missing passive raw motion')
 local h,s,t,rb,wb,ra,wa,unknown,flags,parent,pose,motion,timing=raw:match('^SHIP_RAW_MOTION_1;handle=([0-9a-f]+);slot=(%d+);tick=(%d+);sector_pose_supported=0;identity_ok=1;stable=1;read_before=([01]);write_before=([01]);read_after=([01]);write_after=([01]);unknown_references=(%d+);reference_flags=([0-9a-f]+);parent=([0-9a-f]+);raw_pose=([0-9a-f]+);motion=([0-9a-f]+);timing=([0-9a-f]+)$')
 assert(h==handle and tonumber(s)==slot and tonumber(t)==tick,'passive motion identity or tick differs')
 fixedhex(h,16);fixedhex(flags,8);fixedhex(parent,16)
 fixedhex(pose,272);fixedhex(motion,380);fixedhex(timing,90)
 assert(motion:sub(1,8)=='5811c502','unsupported passive controller')
 assert(rb==ra and wb==wa and rb~=wb,'passive motion selectors changed or overlap')
 local mask=tonumber(flags,16);assert(mask<=31,'unsupported passive reference flags')
 local count=0;for i=0,4 do if math.floor(mask/2^i)%2==1 then count=count+1 end end
 assert(tonumber(unknown)==count,'passive reference coverage differs')
 local parent_le='';for i=15,1,-2 do parent_le=parent_le..parent:sub(i,i+1) end
 assert(pose==parent_le..motion:sub(9,104)..motion:sub(153,184)..motion:sub(195,290)..motion:sub(339,370),'passive position/basis subset differs from motion')
 local references={parent,motion:sub(153,168),motion:sub(169,184),motion:sub(339,354),motion:sub(355,370)}
 for i,value in ipairs(references) do assert(math.floor(mask/2^(i-1))%2==0 or value=='0000000000000000','passive unresolved reference has invented identity') end
 return {pose=pose,flags=flags,parent=parent,read=rb,write=wb,unknown=count}
end
function M.emit(log,kind,tick,phase,data)
 assert(kind=='RAW' or kind=='STATE','invalid construction chunk family')
 assert(phase=='before' or phase=='after' or phase=='sample','invalid construction chunk phase')
 assert((kind=='STATE')==(phase=='sample') and type(tick)=='number' and tick==math.floor(tick) and tick>0 and tick<=4294967295,'invalid construction chunk identity')
 assert(type(data)=='string' and #data>0 and #data<=8192 and not data:find('[\r\n]'),'invalid construction chunk payload')
 local count=math.ceil(#data/700)
 for i=1,count do log('CONSTRUCTION_'..kind..'_PART tick='..tick..' phase='..phase..' part='..i..' count='..count..' data='..data:sub((i-1)*700+1,i*700)) end
 log('CONSTRUCTION_'..kind..'_END tick='..tick..' phase='..phase..' count='..count..' bytes='..#data)
end
function M.local_detail(raw)
 assert(type(raw)=='string','missing private passive identity detail')
 local runtime,refs=raw:match('^SHIP_RAW_MOTION_LOCAL_1;runtime=([0-9a-f]+);refs=([0-9a-f,]+)$')
 fixedhex(runtime,16);assert(runtime~='0000000000000000','null passive native lifetime')
 local count=0
 for value in refs:gmatch('[^,]+') do fixedhex(value,16);count=count+1 end
 assert(count==5 and #refs==84,'invalid private passive frame identities')
 return raw
end
function M.same_frame(before,after,private_before,private_after)
 assert(before.pose==after.pose and before.flags==after.flags and before.parent==after.parent and before.read==after.read and before.write==after.write and M.local_detail(private_before)==M.local_detail(private_after),'construction enrollment changed passive position/basis/reference fields')
 -- Motion velocity/mode/layer and timing are still exposed. They are not assumed
 -- unchanged by deterministic preparation, and no sector coordinates are derived.
end
X4LAN_NATIVE_CONSTRUCTION_RAW=M
return M
