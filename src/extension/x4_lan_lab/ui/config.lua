-- Generated client copies override this file. The source extension is inert.
X4LAN_CONFIG = {
    enabled = false,
    bubble_actions = false,
    trading = false,
    fire2 = false, -- Matched internal packages select FIRE2 before any shot is played.
    pilot_health = false, -- Dev only: pilot-reported co-op ship health (health_game.lua).
    projectile_visuals = false, -- Dev only: observed native shots as harmless graphics; adapter requires full opt-in stack.
    shot_visual = false, -- Dev only: verified harmless FIRE2 projectiles; requires fire2/SHOT/HEALTH.
    shot_claims = false, -- Dev only, needs pilot_health: shooter-confirmed hit counts (shot_game.lua).
    shot_capture = false, -- Dev only, needs shot_claims/shot_visual: native capture of ordinary stock hits.
    shot_undo = false, -- Diagnostic only, needs shot_capture: replayed-hit undo (delays shield recharge).
    faction_missions = false, -- INTERNAL opt-in: native shared faction mission journal.
    npc_combat = false, -- INTERNAL opt-in: host NPC health and acknowledged guest stock-gun hits.
    dedicated = false, -- Dev only: unattended Linux authority lifecycle and local control.
    dedicated_session = false, -- Matched dedicated packages: server p1 plus clients p2..p<max_players>.
    -- max_players: dedicated packages set 2..8 slots including the server (absent = legacy 3).
    local_ship = 0, -- 0 derives p1->1, p2->2, p3->1, pK->K-1.
    dedicated_control_path = "", -- Installed server config supplies an absolute local path.
    dedicated_heartbeat_interval = 5,
    player_factions = false, -- INTERNAL: own faction per human (pool x4lan_playerN), relations, sector owners.
    frame_profile = false, -- Diagnostic only: per-second FRAMECOST wall time of the mod's UI Lua by subsystem.
    net_probe = false, -- Diagnostic only: RTTPING/RTTPONG round trips to the relay, NETRTT every 5 s.
    npc_target_diag = false, -- Diagnostic only: logs NPC orders, targets, attention and radar near the shared ships.
    npc_motion_diagnostics = false, -- Diagnostic only: per-interval NPC_DIAG replica/motion counts (seconds, or true = 1 s).
    frame_diet = false, -- Windows dedicated server (dedicated p1) only: skip presentation the simulation does not read.
    rng_split = false, -- Windows: render-list RNG draws on a private stream (server and clients, for RNG parity).
    interaction_test = false, -- Destructive fixtures ONLY in the disposable test world.
    motion_diagnostics = false, -- Diagnostic only: rate-limited MOTION_DIAG rows (seconds, or true = 0.5 s) for remote co-op ships.
    test_profile = "45841",
    host = "127.0.0.1",
    port = 45841,
    token = "",
    build = "SET_EXACT_BUILD",
    bubble = "bubble-01",
    slot = "p1",
    mode = "telemetry", -- "proxy" attempts experimental spawn/move calls.
    proxy_macro = "ship_arg_s_scout_01_a_macro",
    -- Shared co-op ship for both players and both proxies (requires X4: Timelines).
    ship_macro = "ship_ter_s_xperimental_01_a_macro",
    ship_loadout = "x4lan_coop_xperimental",
    ship_weapons = 6, -- the 7th slot is a missile launcher; the hull stores no missiles
}
