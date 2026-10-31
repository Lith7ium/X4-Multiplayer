-- Log-only NPC targeting diagnostic (npc_target_diag). Starts the MD sampler once the
-- shared ships exist and writes NPCDIAG lines; it never changes game objects.
local cfg=X4LAN_CONFIG
if not cfg or not cfg.npc_target_diag then return end
local d={ready=false,nextTry=0}
X4LAN_NPC_TARGET_DIAG=d
local function log(kind,p) DebugError('[X4LAN] NPCDIAG '..kind..' slot='..tostring(cfg.slot)..' '..tostring(p or '')) end
RegisterEvent('X4LAN_NpcTargetDiag.Ready',function() d.ready=true;log('READY') end)
RegisterEvent('X4LAN_NpcTargetDiag.Ship',function(_,p) log('SHIP',p) end)
RegisterEvent('X4LAN_NpcTargetDiag.Npc',function(_,p) log('NPC',p) end)
RegisterEvent('X4LAN_NpcTargetDiag.Summary',function(_,p) log('SUMMARY',p) end)
-- Resend Start until MD answers Ready: an event sent before the game world is up is lost.
SetScript('onUpdate',function()
 if d.ready then return end
 local now=GetCurRealTime()
 if now<d.nextTry then return end
 d.nextTry=now+5
 AddUITriggeredEvent('X4LAN_NpcTargetDiag','Start')
end)
