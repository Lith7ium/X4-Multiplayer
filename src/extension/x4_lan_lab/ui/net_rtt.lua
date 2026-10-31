-- Opt-in client<->relay round-trip probe (config flag net_probe). Nothing is wrapped or sent
-- unless enabled. Wire (ASCII, version-1 envelope; relays without support ignore RTTPING):
--   client -> relay  RTTPING|1|lease|seq|client_ms|echo_relay_ms|hold_ms
--   relay -> client  RTTPONG|1|lease|seq|client_ms|relay_ms
-- echo_relay_ms/hold_ms return the newest RTTPONG relay time and how long the client held
-- it, so the relay can measure RTT from its side as well ('-' when none is available).
-- RTT samples are read once per frame by the UI, so they include up to one frame interval.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.enabled or cfg.net_probe~=true then return end
local M=X4LAN_MEASURE
if not M or not X4LAN_CLIENT or not X4LAN_TRANSPORT then return end
local clock=M.clock
local interval=tonumber(cfg.net_probe_interval) or .25
if interval~=interval or interval<.05 then interval=.05 elseif interval>5 then interval=5 end
local TIMEOUT,REPORT,MAX_PENDING=2,5,256
local N={interval=interval,seq=0,pending={},pendingCount=0}
X4LAN_NET_PROBE=N
local function resetWindow(now)
 N.windowStart=now;N.pings,N.pongs,N.lost,N.late=0,0,0,0
 N.rtts,N.deviations={},{}
 N.tx,N.txBytes,N.rx,N.rxBytes=0,0,0,0
 N.messages,N.kinds=0,{}
end
resetWindow(clock())

-- Count every packet and byte through the mod's UDP transport.
local open=X4LAN_TRANSPORT.open
X4LAN_TRANSPORT.open=function(...)
 local t=open(...)
 local proxy={}
 function proxy:send(packet)
  N.tx=N.tx+1;N.txBytes=N.txBytes+(type(packet)=='string' and #packet or 0)
  return t:send(packet)
 end
 function proxy:receive()
  local packet=t:receive()
  if packet then N.rx=N.rx+1;N.rxBytes=N.rxBytes+#packet end
  return packet
 end
 function proxy:close() return t:close() end
 return proxy
end

local function forget(seq)
 if N.pending[seq] then N.pending[seq]=nil;N.pendingCount=N.pendingCount-1 end
end
function N.pong(client,packet,arrival)
 local lease,seq,clientMs,relayMs=packet:match('^RTTPONG|1|(%x+)|(%d+)|([%d.]+)|([%d.]+)$')
 if not lease or lease~=client.lease or #seq>10 or #relayMs>20 then return end
 seq=tonumber(seq);local relay=tonumber(relayMs)
 local sent=N.pending[seq]
 if not sent then
  if seq<=N.seq then N.late=N.late+1 end
  return
 end
 if math.abs(sent*1000-tonumber(clientMs))>1 then return end -- not our timestamp
 forget(seq)
 N.pongs=N.pongs+1
 N.rtts[#N.rtts+1]=arrival-sent
 -- Inter-arrival deviation against the relay's send spacing (relay->client jitter).
 if N.lastArrival and relay>=N.lastRelay then
  N.deviations[#N.deviations+1]=math.abs((arrival-N.lastArrival)-(relay-N.lastRelay)/1000)
 end
 N.lastArrival,N.lastRelay=arrival,relay
 N.echo,N.echoAt=relayMs,arrival
end

local function value(v) return v and string.format('%.3f',v*1000) or 'na' end
-- Received messages per second by kind (an XB bundle counts each message it carries;
-- rx_pps counts datagrams), largest first: rx_types=UMOVE:310.2,NHP:84.0,...
local function kinds(window)
 local list={}
 for kind,n in pairs(N.kinds) do list[#list+1]={kind,n} end
 table.sort(list,function(a,b) return a[2]>b[2] or a[2]==b[2] and a[1]<b[1] end)
 local out={}
 for i=1,math.min(#list,12) do out[i]=string.format('%s:%.1f',list[i][1],list[i][2]/window) end
 return #out>0 and table.concat(out,',') or 'na'
end
function N.report(now)
 local window=now-N.windowStart
 local answered=N.pongs+N.lost
 M.log(string.format('NETRTT v=1 side=client slot=%s window_s=%.3f pings=%d pongs=%d lost=%d late=%d loss_pct=%s '..
  'rtt_ms_min=%s rtt_ms_median=%s rtt_ms_p95=%s rtt_ms_max=%s jitter_ms_p95=%s '..
  'tx_pps=%.1f tx_Bps=%.0f rx_pps=%.1f rx_Bps=%.0f interval_s=%.3f clock=%s rx_msgs=%.1f rx_types=%s',
  tostring(cfg.slot),window,N.pings,N.pongs,N.lost,N.late,
  answered>0 and string.format('%.1f',100*N.lost/answered) or 'na',
  value(M.percentile(N.rtts,0)),value(M.percentile(N.rtts,.5)),value(M.percentile(N.rtts,.95)),value(M.percentile(N.rtts,1)),
  value(M.percentile(N.deviations,.95)),
  N.tx/window,N.txBytes/window,N.rx/window,N.rxBytes/window,interval,M.source,N.messages/window,kinds(window)))
 resetWindow(now)
end

function N.tick(client)
 local now=clock()
 for seq,sent in pairs(N.pending) do
  if now-sent>TIMEOUT then forget(seq);N.lost=N.lost+1 end
 end
 if now>=(N.nextPing or 0) and N.pendingCount<MAX_PENDING and N.seq<2147483647 then
  N.seq=N.seq+1
  local echo,hold='-','-'
  if N.echo then echo,hold=N.echo,string.format('%.3f',(now-N.echoAt)*1000);N.echo=nil end
  N.pending[N.seq]=now;N.pendingCount=N.pendingCount+1;N.pings=N.pings+1
  client.transport:send(string.format('RTTPING|1|%s|%d|%.3f|%s|%s',client.lease,N.seq,now*1000,echo,hold))
  N.nextPing=now+interval
 end
 if now-N.windowStart>=REPORT then N.report(now) end
end

local new=X4LAN_CLIENT.new
X4LAN_CLIENT.new=function(...)
 local c=new(...)
 if type(c)~='table' then return c end
 -- A new client (reconnect) cannot answer the old connection's outstanding pings.
 N.pending,N.pendingCount,N.echo,N.lastArrival,N.lastRelay={},0,nil,nil,nil
 local receive,update=c.receive,c.update
 if type(receive)=='function' then
  c.receive=function(self,packet,...)
   if type(packet)=='string' then
    local kind=packet:match('^([%u%d_]+)|')
    if kind and #kind<=32 then N.messages=N.messages+1;N.kinds[kind]=(N.kinds[kind] or 0)+1 end
   end
   if type(packet)=='string' and packet:sub(1,8)=='RTTPONG|' then
    if self.lease then N.pong(self,packet,clock()) end
    return
   end
   return receive(self,packet,...)
  end
 end
 if type(update)=='function' then
  c.update=function(self,...)
   local connected,reason=update(self,...)
   if connected and self.lease and self.transport then N.tick(self) end
   return connected,reason
  end
 end
 return c
end
M.log(string.format('NETRTT armed interval_s=%.3f report_s=%d timeout_s=%d',interval,REPORT,TIMEOUT))
