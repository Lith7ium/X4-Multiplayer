/* The one Linux X4 build the Linux gate supports, and everything it touches.
 * X4 Foundations 9.00 (611726), Steam build 23660954, Linux ELF64 x86-64 ET_EXEC
 * (fixed image base 0x400000, GCC 14.2). Addresses are ELF virtual addresses of
 * that file only; the gate refuses any other file (size, SHA-256, build ID, the
 * site hashes below). Raw engine bytes are not stored, only their SHA-256.
 *
 * Windows counterparts (tools/dev/lockstep_linux_known_pairs.json) and evidence
 * (reports/LOCKSTEP-LINUX-GATE-CORE-20261029.txt). Every data address below is
 * referenced rip-relative from a pinned site, so the site pins also pin it; the
 * offline check tools/dev/check_lockstep_gate_linux_sites.py decodes each of those
 * references in the ELF file. Parsed by that check: keep one entry per line. */
#define LX_IMAGE_BASE 0x400000u
#define LX_FILE_SIZE 60459280ull
#define LX_FILE_SHA "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20"
#define LX_BUILD_ID "77cf7ac0ff32d88ffd3db7eb6ea2e025aecb711d"
#define LX_TEXT_VA 0x656000u          /* PT_LOAD R E holding .text */
#define LX_TLS_VADDR 0x3aac780u       /* PT_TLS: .tdata 8 bytes + .tbss; static TLS offsets in lockstep_tls.h */
#define LX_TLS_MEMSZ 0x2740u
#define LX_TLS_ALIGN 0x10u
/* Frame gate. Windows CALL_RVA 0x12ae9d2 / WORD_RVA 0x12ae9d0 (8-byte word): on
 * Linux the call's rel32 crosses an 8-byte boundary, so the patch is one 16-byte
 * compare-exchange over the aligned window holding it (like the Windows path hook). */
#define LX_CALL_VA 0x72fb16u          /* main loop: call X4::FrameTick(FrameState*, loading) */
#define LX_CALL_WINDOW 0x72fb10u      /* 16-byte aligned; rel32 at window+7..+10 */
#define LX_TICK_VA 0x2024fc0u         /* X4::FrameTick, Windows TICK_RVA 0xfa2d70 */
#define LX_IN_FLIGHT_VA 0x3d8d97bu    /* main-loop byte: 1 from FrameTick's return until semSimDone (Windows 0x31c20cc) */
#define LX_VBI_TABLE_VA 0x3dbc4d8u    /* XLib::VbiTable*, Windows TABLE_RVA 0x6cf1570 */
#define LX_VBI_LOADING 0x130u         /* int loading flag in the table, both builds */
#define LX_GAME_VA 0x3db82b0u         /* TimerBase game time (f64), Windows GAME_RVA 0x6b60e38 */
#define LX_REAL_VA 0x3db82b8u         /* TimerBase real time (f64), Windows REAL_RVA 0x6b60e40 */
#define LX_SPEED_VA 0x3db82d8u        /* TimerBase speed factor (f64), Windows SPEED_RVA 0x6b60e60 */
/* Fixed dt. Windows NOPs the movie-capture branch (0xfa3302 -> Linux 0x2025135) and
 * sets the capture frame time (0x6adfc60 -> Linux 0x4533d40). Linux uses the
 * natively validated x4mp-agent fixed60 patch of the non-capture path instead:
 * dt = 1/60 exactly; the capture flag must stay 0 on both. */
#define LX_MOVIE_FLAG_VA 0x4533d38u   /* Windows 0x6adfc58 */
#define LX_MOVIE_DT_VA 0x4533d40u     /* Windows 0x6adfc60 (not written on Linux) */
#define LX_DT_LOAD_VA 0x2025260u      /* movsd xmm1,[rip+d] (the 1.0 clamp at 0x2e244d0) */
#define LX_DT_MIN_VA 0x2025268u       /* minsd xmm1,xmm0 ; pxor xmm0,xmm0 */
#define LX_ONE_SIXTIETH_VA 0x2e24578u /* .rodata double 1/60 */
#define LX_DT_LOAD_ORIGINAL 0x00dff2680d100ff2ull /* f2 0f 10 0d 68 f2 df 00 */
#define LX_DT_MIN_ORIGINAL 0xc0ef0f66c85d0ff2ull  /* f2 0f 5d c8 66 0f ef c0 */
#define LX_DT_MIN_PATCHED 0xc0ef0f6600401f0full   /* 0f 1f 40 00 66 0f ef c0 */
/* World identities and world hash (gate_world.h; reports/LOCKSTEP-LINUX-WORLD-HASH-20261029.txt).
 * Windows hooks its import function 0x79f720 (saved UniverseID -> runtime ID into the import
 * map 0x31c2110 via the map insert 0x4795a0); Linux inlines that function into its callers,
 * each calling the map insert 0x10c8140 with the import map 0x3e1cac0, so the Linux gate
 * hooks the insert and keeps only that map. Both load functions clear the map through the
 * map clear (Windows 0x9eeddb/0x9f0804, Linux 0x1cdd737/0x1cddf54), which frees its nodes, so
 * the gate also hooks the clear and reads the nodes there, not at the first live frame.
 * Find, map and bank selectors as on Windows. */
#define LX_IMPORT_INSERT_VA 0x10c8140u  /* map<u64,u64>::operator[] (Windows 0x4795a0); node key +0x20, value +0x28 */
#define LX_IMPORT_MAP_VA 0x3e1cac0u     /* saved UniverseID -> runtime ID import map (Windows 0x31c2110) */
#define LX_IMPORT_CLEAR_VA 0x11e0b30u   /* map clear, frees the node blocks (Windows 0x942540) */
#define LX_FIND_VA 0xa68df0u            /* component find (map, runtime, 4) (Windows 0xced70) */
#define LX_COMPONENT_MAP_VA 0x3daa860u  /* component registry pointer (Windows 0x6cf1500) */
#define LX_BANK_READ_VA 0x3e4fea8u      /* movement bank read selector (Windows 0x38f4ee4) */
#define LX_BANK_WRITE_VA 0x3d8e254u     /* movement bank write selector (Windows 0x2f4b6f4) */
#define LX_IMPORT_ENTRY_ORIGINAL 0x8949544155415641ull /* push r14; push r13; push r12; mov r12,rsi (first 8 bytes) */
#define LX_IMPORT_PROLOGUE 6u           /* the three pushes, relocated into the trampoline */
#define LX_IMPORT_CLEAR_ORIGINAL 0x578b4840ec834853ull /* push rbx; sub rsp,0x40; mov rdx,[rdi+8] (first 8 bytes) */
#define LX_IMPORT_CLEAR_PROLOGUE 5u     /* push rbx; sub rsp,0x40, relocated into the trampoline */
#define LX_SHIELD_SOURCE_VTABLE 0x3b8d6d8u /* U::ShieldRechargeSource (Windows 0x2b19700), stored by shield_group_construct */
/* Serial pathing and path determinism (gate_path.h, lockstep_path_determinism.h). */
#define LX_PATH_MANAGER_VA 0x3e0ced0u    /* PathingManager pointer (Windows 0x6cf21c8) */
#define LX_PATH_RELEASE_SITE 0x12125e3u  /* MoveWorker::Run: call of the release with manager+0x70 */
#define LX_PATH_RELEASE_ORIGINAL 0x008115f8e870c783ull /* the aligned word holding that call */
#define LX_SEM_RELEASE_VA 0x1a23be0u     /* semaphore release (semaphore, count) */
#define LX_SEM_TIMED_WAIT_VA 0x1a23d90u  /* semaphore wait (semaphore, ms): 1 taken, 0 timed out */
#define LX_PATH_REFRESH_VA 0x14498d0u    /* RefreshFromPathingState (Windows 0xea2a20) */
#define LX_PATH_SYNC_VA 0x1a3b900u       /* SyncThreadGameTime (Windows 0x14b8e20) */
#define LX_PATH_BUDGET 0x3a8u            /* manager: UpdateBatch budget in seconds (Windows +0x218) */
/* Hazard region updates (gate_region.h, lockstep_region_rng.h). */
#define LX_REGION_VTABLE 0x3d15408u         /* U::Region (Windows 0x2b2ad60) */
#define LX_REGION_SLOT_VA 0x3d16960u        /* its Move slot, +0x1558 (Windows 0x2b2c270, +0x1510) */
#define LX_REGION_MOVE_VA 0x12c7f80u        /* U::Region::Move (Windows 0x767410) */
#define LX_DAMAGE_FIELD_VTABLE 0x3b57b88u   /* U::Regions::DamageField (Windows 0x2c4b2f0) */
#define LX_DAMAGE_FIELD_UPDATE_VA 0x12d7a80u /* its per-frame update, slot +0x48 (Windows 0xee16a0, +0x40) */
/* AI Wait restores (gate_ai_wait.h, lockstep_ai_wait_rng.h). */
#define LX_AI_WAIT_SLOT_VA 0x3b2a680u       /* U::AI::WaitAction vtable 0x3b2a650, restore slot +0x30 (Windows +0x28) */
#define LX_AI_WAIT_RESTORE_VA 0x10c12b0u    /* WaitAction::Reattach (Windows 0x10a50d0) */
#define LX_AI_WAIT_RETURN_VA 0x10df36bu     /* the AI importer's return from it (Windows 0x1069bfc) */
#define LX_AI_CONTEXT_VTABLE 0x3b28df0u     /* U::AI::AIScriptContext (Windows 0x2c6a270) */
/* MD cue delays restored during the load (gate_md_resume.h, lockstep_md_resume_rng.h). */
#define LX_MD_RESUME_CALLER_VA 0xf77160u    /* Cue_ResumeAfterLoad (Windows 0x1115960) */
#define LX_MD_RESUME_SITE_VA 0xf77325u      /* its E8 to the delay evaluator (Windows 0x1115bad) */
#define LX_MD_RESUME_DELAY_VA 0xf5b380u     /* cue delay evaluator (Windows 0x111f8c0) */
#define LX_CUE_VTABLE 0x3b0ee70u            /* MD::Cue (Windows 0x2c75170) */
/* The boot hold (gate_boot_hold.h, lockstep_boot_hold.h). */
#define LX_BOOT_HOLD_CALL_VA 0xfeb2bcu      /* UniverseFrameUpdate -> AIDirector::Move, its only caller (Windows 0x9fe093) */
#define LX_BOOT_HOLD_MOVE_VA 0x10e27a0u     /* AIDirector::Move (Windows 0x106d250) */
#define LX_BOOT_HOLD_DIRECTOR_VA 0x3dea760u /* the director (Windows 0x6cf15f0) */
#define LX_BOOT_HOLD_ENTRY_ORIGINAL 0x56415741e5894855ull /* push rbp; mov rbp,rsp; push r15; push r14 (first 8 bytes) */
#define LX_BOOT_HOLD_PROLOGUE 6u            /* push rbp; mov rbp,rsp; push r15, relocated into the trampoline */
/* Portable event delivery order (gate_event_order.h, lockstep_event_order.h). */
#define LX_EVENT_DELIVER_VA 0x16c5db0u      /* DeliverEvent (Windows 0x99c470) */
#define LX_EVENT_BATCH_VA 0x3eedfa0u        /* the due batch, 0x38-byte slots (Windows 0x39943e0) */
#define LX_EVENT_COUNT_VA 0x3eedf90u        /* its count (Windows 0x38f4bc8) */
#define LX_EVENT_CALL_WORD_VA 0x16c6b60u    /* the aligned qword ending in the delivery call's rel32 */
#define LX_EVENT_CALL_VA 0x16c6b63u         /* ProcessDueEvents' E8 to DeliverEvent (Windows 0x99c3d1) */
#define LX_EVENT_CALL_WORD_ORIGINAL 0xfffff248e828758bull /* 8b 75 28 | e8 rel32(DeliverEvent) */
#define LX_UI_ELEMENT_VTABLE 0x3d3b040u     /* U::UIElement (Windows 0x2b6cc38) */
/* The Gravidar (gate_gravidar.h, lockstep_ai_schedule.h). */
#define LX_GRAVIDAR_WORKER_VA 0x3dfa4a0u    /* AI::Gravidar (Windows 0x6cf1650) */
#define LX_GRAVIDAR_VTABLE 0x3b2cf08u       /* its vtable (Windows 0x2c68860) */
#define LX_GRAVIDAR_PUBLISH_VA 0x1109380u   /* publish (Windows 0x10b12c0) */
#define LX_GRAVIDAR_CALL_VA 0x12125a9u      /* the MoveWorker's E8 to it (Windows 0xef45d9) */
#define LX_GRAVIDAR_CALL_WORD_VA 0x12125a8u /* the aligned qword holding its rel32 at +2 */
#define LX_GRAVIDAR_UNIT_APPLY_VA 0x10fd7b0u /* ActiveGravidarUnit apply (Windows 0x10acd70) */
#define LX_GRAVIDAR_UNIT_VTABLE 0x3b2e138u  /* ActiveGravidarUnit (Windows 0x2c67610) */
#define LX_GRAVIDAR_UNIT_SLOT_VA 0x3b2e178u /* its slot +0x40 (Windows 0x2c67648, +0x38) */
#define LX_GRAVIDAR_RUN_VA 0x1106ee0u       /* Run (Windows 0x10ae0e0) */
#define LX_MUTEX_LOCK_VA 0x1a23650u         /* X4's mutex: lock */
#define LX_MUTEX_UNLOCK_VA 0x1a236a0u       /* unlock */
#define LX_GAME_TIME_LOCK_VA 0x3db8280u     /* the global game time's lock (Windows 0x6b60e10) */
#define LX_GAME_TIME_GLOBAL_VA 0x3db82b0u   /* the global game time (Windows 0x6b60e38) */
#define LX_MACRO_VTABLE 0x3ae6860u          /* U::Macro (Windows 0x2ba5430) */
#define LX_MACRO_NAME_GETTER_VA 0xa38880u   /* its slot 19, returns +0x20 (Windows slot 18 0xefb10) */
#define LX_TEXT_END_VA 0x2c730a9u           /* the end of PT_LOAD R E */
/* Gravidar factor-pass deadlines (gate_gravidar_deadlines.h). */
#define LX_SECTOR_VTABLE 0x3d1d850u         /* U::Sector (Windows 0x2b1ea48) */
#define LX_SECTOR_MOVE_SLOT_VA 0x3d1eda8u   /* its slot +0x1558 (Windows 0x2b1ff58, +0x1510) */
#define LX_SECTOR_MOVE_VA 0x127cf40u        /* U::Sector::Move (Windows 0x784230) */
/* The wingman's travel decision from a snapshot (gate_travel_snapshot.h). */
#define LX_TRAVEL_READ_WORD_VA 0x1446b38u   /* the qword holding the leader read's disp32 low byte, byte 5 */
#define LX_TRAVEL_READ_VA 0x1446b3bu        /* cmp byte [rdx+0xf9b],0 (Windows 0xe918d5) */
#define LX_PATH_JOB_VA 0x13c45c0u           /* the batch job thunk (Windows 0xf29cd0) */
#define LX_PATH_JOB_LEA_A_VA 0x13e3db0u     /* 16-byte window holding the first LEA's disp32 */
#define LX_PATH_JOB_LEA_A_DISP 0xcu
#define LX_PATH_JOB_LEA_B_VA 0x13e3e30u     /* likewise the second */
#define LX_PATH_JOB_LEA_B_DISP 0x0u
/* The same runtime IDs on every client (gate_runtime_ids.h, lockstep_runtime_ids.h). */
#define LX_ID_MAP_VA 0x3daa860u             /* AutoIDMap pointer (Windows 0x6cf1500) */
#define LX_ID_REMOVE_VA 0xee3850u           /* Remove (Windows 0xcbe70) */
#define LX_ID_REMOVE_ENTRY_ORIGINAL 0x5441554156415741ull /* push r15; push r14; push r13; push r12 (first 8 bytes) */
#define LX_ID_REMOVE_PROLOGUE 6u            /* push r15; push r14; push r13, relocated into the trampoline */
#define LX_ID_ASSIGN_VA 0xeed870u           /* AssignNewID (Windows 0xcc220), EnsureFreeSlots inlined */
#define LX_ID_ASSIGN_ENTRY_ORIGINAL 0x4855544155415641ull /* push r14; push r13; push r12; push rbp; the first byte of mov rbp,rdi */
#define LX_ID_ASSIGN_PROLOGUE 6u            /* push r14; push r13; push r12, relocated into the trampoline */
#define LX_ID_COMPONENT_RETURN_VA 0x189e28cu /* the return from AssignNewID in the constructor every U::Positional goes through */
/* The script RNG (gate_script_rng.h, lockstep_script_rng.h). */
#define LX_EXEC_RUN_VA 0xd5fbb0u            /* ExecState_Run (Windows 0xb58bb0) */
#define LX_EXEC_RUN_ENTRY_ORIGINAL 0xec8348fb89485355ull /* push rbp; push rbx; mov rbx,rdi; the start of sub rsp,8 */
#define LX_EXEC_RUN_PROLOGUE 5u             /* push rbp; push rbx; mov rbx,rdi, relocated into the trampoline */
#define LX_EXEC_RANGE_VA 0xd5fc30u          /* the run range (Windows 0xb58ce0): rdi Exec, rsi program, rdx begin, rcx end, r8d mode */
#define LX_EXEC_RANGE_ENTRY_ORIGINAL 0x5441554156415741ull /* push r15; push r14; push r13; push r12 */
#define LX_EXEC_RANGE_PROLOGUE 6u           /* push r15; push r14; push r13, relocated into the trampoline */
#define LX_AI_EVENT_SLOT_VA 0x3b29fa0u      /* U::AI::AIScriptContext vtable, slot 566 (Windows slot 565, 0x2c6b418) */
#define LX_AI_EVENT_VA 0x10d4270u           /* its event handler (Windows 0x1066750), the slot its only holder */
#define LX_STACK_VTABLE 0x3aedd58u          /* AI::AICallStackContext (Windows 0x2c690b8) */
#define LX_STACK_EVENT_SLOT_VA 0x3aeef08u   /* its slot 566 (Windows slot 565, 0x2c6a260) */
#define LX_STACK_EVENT_VA 0x10d4060u        /* its handler (Windows 0x1062f70), the slot its only holder */
#define LX_CUE_EVENT_SLOT_VA 0x3b10020u     /* MD::Cue slot 566 (Windows slot 565, 0x2c76318) */
#define LX_CUE_EVENT_VA 0xf799c0u           /* its handler (Windows 0x111f990), a tail jump into the check */
#define LX_CUE_CHECK_VA 0xf76110u           /* Cue_CheckConditions (Windows 0x111c290) */
#define LX_CUE_TIMED_SITE_VA 0xf79d18u      /* MDScheduler_Update's call (Windows 0x112208b) */
#define LX_CUE_WAIT_SITE_VA 0xf7650du       /* Cue_EnterWaiting's call (Windows 0x111d296); its rel32 crosses a 16-byte boundary */
#define LX_CUE_RAISE_SITE_VA 0xf78d33u      /* Cue_RaiseCueEvent's call (Windows 0x111e505) */
/* find_* candidate lists (gate_find_order.h, lockstep_find_order.h). */
#define LX_FIND_ORDER_VTABLE 0x3b00a68u      /* Scripts::FindComponentAction (Windows 0x2c0f3c8) */
#define LX_FIND_ORDER_EXECUTE_VA 0xd87ad0u   /* its Execute (Windows 0xb87a30), slot 4 */
#define LX_FIND_ORDER_SITE_VA 0xd87b33u      /* its call of the result store (Windows 0xb87ae3) */
#define LX_FIND_ORDER_ASSIGN_VA 0x1d2b330u   /* the result store (Windows 0xd2ab50) */
/* Ship loadouts (gate_spawn_rng.h, lockstep_spawn_rng.h). */
#define LX_GENERATE_VA 0xd2a470u             /* U::ShipGenerator::Generate (Windows 0xc841b0) */
#define LX_GENERATE_ENTRY_ORIGINAL 0x56415741e5894855ull /* push rbp; mov rbp,rsp; push r15; the start of push r14 */
#define LX_GENERATE_PROLOGUE 6u              /* push rbp; mov rbp,rsp; push r15, relocated into the trampoline */
/* The navigation upkeep's deadlines (gate_nav.h, lockstep_nav_cadence.h). */
#define LX_NAV_MOVE_VA 0x144a9e0u            /* NavigationController::vfunc33 (Windows 0xea77f0) */
#define LX_NAV_MOVE_SLOT_A_VA 0x3b69928u     /* slot 40 of its vtable 0x3b697e8 (Windows 0x2c51260, slot 33) */
#define LX_NAV_MOVE_SLOT_B_VA 0x3b94cf0u     /* slot 40 of its second vtable 0x3b94bb0 */
#define LX_NAV_PARAMS_NEAR_VA 0x144aa6cu     /* ja, parameters within 1 km */
#define LX_NAV_PARAMS_FAR_VA 0x144aa79u      /* jbe, parameters beyond 5 km */
#define LX_NAV_PROBE_NEAR_VA 0x144ad1du      /* ja, probes within 1 km */
#define LX_NAV_PROBE_FAR_VA 0x144ad2au       /* jbe, probes beyond 5 km */
/* Objects' attention timer (gate_attention.h, lockstep_attention.h). */
#define LX_ATTENTION_VA 0x1465030u           /* PeriodicAttentionRefresh (Windows 0x6d1ff0) */
#define LX_ATTENTION_ENTRY_ORIGINAL 0xec8348fb89485355ull /* push rbp; push rbx; mov rbx,rdi; the start of sub rsp,0x18 */
#define LX_ATTENTION_PROLOGUE 5u             /* push rbp; push rbx; mov rbx,rdi, relocated into the trampoline */
#define LX_ATTENTION_DUE 0x2d8u              /* the object's next due (Windows +0x2e0) */
/* The Linear step trace (gate_step_trace.h, lockstep_step_trace.h; diagnostic). */
#define LX_ST_SLOT_VA 0x3b62558u             /* LinearFlightControlModel's vtable entry of its step (Windows 0x2c4f680, slot 14) */
#define LX_ST_STEP_VA 0x13ed050u             /* the step (Windows 0xeb27c0) */
/* FR8, the step's attitude block (gate_step_trace.h): three calls, each rel32 inside one 16-byte line. */
#define LX_SC_ASIN_TARGET_SITE_VA 0x13edc39u /* asinf of the target pitch (Windows 0xeb3892) */
#define LX_SC_ASIN_CUR_SITE_VA 0x13edcc5u    /* asinf of the current pitch (Windows 0xeb37ff) */
#define LX_SC_BUILDER_SITE_VA 0x13edecbu     /* the rotation builder (Windows 0xeb3912 -> 0x34bff0) */
#define LX_SC_ASINF_VA 0x6579c0u             /* asinf@plt */
#define LX_SC_BUILDER_VA 0x1e02200u          /* rdi rows (bank+0x10), xmm0 pitch, xmm1 yaw, xmm2 roll */
/* FR17: Formation::GetTargetPositionForWingMan (Windows 0xe99d70) at its two calls, each rel32 inside one 16-byte line. */
#define LX_SC_FM_VA 0x1e592d0u               /* rdi formation (+0x48 members' agents, +0x60 shape), rsi out, rdx index, xmm0 time */
#define LX_SC_FM_AGENT_SITE_VA 0x142862bu    /* in the agent's wrapper 0x14285e0 (formation agent+0xf40, index +0xf48; Windows 0xe8ba17) */
#define LX_SC_FM_OTHER_SITE_VA 0x1429795u    /* the other caller (Windows 0xe9a977) */
/* FR18: UpdatePathing 0x1431270 (Windows 0xe8c380) converting the agent's first path point ([rbx+0x6d0], rbx the agent)
 * for SetTarget 0x1431b5f; each rel32 inside one 16-byte line. */
#define LX_SC_PT_LOCAL_VA 0x14bd2d0u         /* rdi out, rsi point: its local transform (Windows 0xa32280) */
#define LX_SC_PT_LOCAL_SITE_VA 0x1431acau    /* ref id == frame id (or its parent's): out to SetTarget as it is */
#define LX_SC_PT_CONVERT_VA 0x141fa50u       /* rdi out, rsi point, rdx frame (Windows 0xe8bd20) */
#define LX_SC_PT_CONVERT_A_SITE_VA 0x14326d4u /* rdx r14, then to 0x1431acf */
#define LX_SC_PT_CONVERT_B_SITE_VA 0x14326ebu /* rdx r14, then the |pos| check to 0x1431b47 */
#define LX_SC_PT_MATH_VA 0x141f7e0u          /* rdi out, rsi velocity, rdx &ref, rcx &frame, r8d flag (Windows 0xe8a4a0) */
#define LX_SC_PT_MATH_SITE_VA 0x141fab0u     /* in 0x141fa50 (Windows 0xe8bdeb) */
/* FR19: the Linear model's SetTarget slot (vtable entry +0x20, Windows 0x2c4f628 -> 0xeaf620; no direct calls). */
#define LX_SG_SLOT_VA 0x3b62500u
#define LX_SG_FN_VA 0x141d140u               /* rdi model, rsi mat4 -> model+0x10, rdx -> +0x50, rcx != 0 -> +0x5c */
/* The flight parameter refresh (gate_fp_redirect.h, lockstep_fp_redirect.h; Option B slice 1). */
#define LX_FR_REFRESH_VA 0x14378c0u          /* the refresh (Windows 0xea1e80) */
#define LX_FR_ENTRY_ORIGINAL 0x5441554156415741ull /* push r15; push r14; push r13; push r12 */
#define LX_FR_PROLOGUE 6u                    /* push r15; push r14; push r13, relocated into the trampoline */
#define LX_FR_G4_SITE_VA 0x1437c0bu          /* its call of G4 (Windows 0xea22b3) */
#define LX_FR_G4_VA 0x15d1770u               /* G4, the engine getter for +0x4 (Windows 0x5f91b0) */
#define LX_FR_G0_SITE_VA 0x1437c1fu          /* its call of G0 (Windows 0xea22ca) */
#define LX_FR_G0_VA 0x15d18a0u               /* G0, for +0x0 (Windows 0x5f9340) */
#define LX_FR_M4_SITE_VA 0x14381e2u          /* its call of M for +0x4 (Windows 0xea28fd) */
#define LX_FR_M0_SITE_VA 0x1438213u          /* its call of M for +0x0 (Windows 0xea2932) */
#define LX_FR_M_VA 0x1429e80u                /* M (Windows 0xea3dc0) */
#define LX_FR_LERP_VA 0x1a48040u             /* L, called at 0x1437c79 (inlined on Windows) */
#define LX_FR_P_SITE_VA 0x1437911u           /* its first call, on every path (Windows 0xea1f2a) */
#define LX_FR_P_VA 0x148cb50u                /* that callee, writes +0x0..+0x2c, +0x100..+0x12c (Windows 0x6dc610) */
#define LX_FR_V_AT_VA 0x148d803u             /* in it: movsd xmm0,fs:-0x2728, the last instruction before the v call */
#define LX_FR_V_BACK_VA 0x148d812u           /* after the call 0x148d80d -> 0x18a15f0 (Windows 0x6dcce6 -> 0x3dc040) */
#define LX_FR_V_VA 0x18a15f0u                /* fills v at rsi = rsp+0x120 */
#define LX_FR_MULPS_AT_VA 0x148d940u         /* in it: the branch through 0x14811c0 (Windows 0x6dcbda -> 0x6db200) */
#define LX_FR_MULPS_BACK_VA 0x148d953u       /* after its call 0x148d94e -> the writer */
#define LX_FR_WRITER_VA 0x14811c0u           /* the writer (o20, D, A, slot) of +0x0..+0x44 (Windows 0x6db200) */
#define LX_FR_WRITER_AT_VA 0x148cd53u        /* the watched ships' path: mov rsi,[rsp+0x28] before its call (Windows 0x6dcaaf) */
#define LX_FR_WRITER_BACK_VA 0x148cd63u      /* after that call 0x148cd5e */
#define LX_FR_K_TABLE_VA 0x3db83c8u          /* the table pointer whose +0x318 is K (Windows [0x146cf1538]+0x300) */
/* Stations' loadout targets and macro order (gate_loadout_order.h, lockstep_loadout_order.h). */
#define LX_LL_WANTED_VA 0x1839ea0u           /* U::Container::GetWantedSupplyOrders (Windows 0x429ad0) */
#define LX_LL_WARES_SITE_VA 0x183a470u       /* its call of the ware sort (Windows 0x429c64) */
#define LX_LL_WARES_VA 0xb03680u             /* sorts and uniques the ware list by address (Windows 0x316f60) */
#define LX_LL_AMMO_SITE_VA 0x183ac7fu        /* its call of the ammunition target (Windows 0x42a40e) */
#define LX_LL_AMMO_VA 0x17e8bd0u             /* (Windows 0x505730) */
#define LX_LL_DEPLOYABLE_SITE_VA 0x183a537u  /* its call of the deployable target (Windows 0x42a6ee) */
#define LX_LL_DEPLOYABLE_VA 0x17e89d0u       /* (Windows 0x5046b0) */
#define LX_LL_UNIT_SITE_VA 0x183a6d7u        /* its call of the unit target (Windows 0x42a9a8) */
#define LX_LL_UNIT_VA 0x17e86d0u             /* (Windows 0x503c40) */
#define LX_LL_EXISTING_SITE_VA 0x17e9063u    /* the ammunition target's call of the existing-users walk (Windows 0x505ef8) */
#define LX_LL_EXISTING_VA 0x1def210u         /* (Windows 0x504970) */
#define LX_LL_PICK_SITE_VA 0x1defce1u        /* that walk's call of the candidate pick (Windows 0x504e20) */
#define LX_LL_PICK_VA 0x17b5f20u             /* (Windows 0x5012f0) */
#define LX_LL_REST_UNIT_VA 0x1e9e7b0u        /* the unit rest in map order, in 0x1e9e3d0 (Windows 0x503349 in 0x502ee0) */
#define LX_LL_REST_UNIT_EXIT_VA 0x1e9e6b2u   /* where that loop leaves: frees the list [rsp+0x60] if any, returns */
#define LX_LL_REST_DEPLOYABLE_VA 0x1df8193u  /* the deployable rest in map order, in 0x1df78e0 (Windows 0x5043ce in 0x504080) */
#define LX_LL_REST_DEPLOYABLE_EXIT_VA 0x1df8230u /* where that loop leaves: frees the list r14 if any, returns */
typedef struct {const char *name;uint32_t va,length;const char *sha256;} lx_site;
static const lx_site lx_sites[]={
    {"build_id_note",0x4002fc,0x24,"563a875a9b060e74466af5da02d3ec88586b7b3f4305c4840fe4b70452d0c464"},
    {"main_loop_in_flight_check",0x72f638,0x1a,"c525558c3a509c9f81e553154c26796d821c557107303ac26485f377f112d7f9"},
    {"main_loop_sim_done_wait",0x72f745,0x6c,"0db4aedf3402446323a03bfe2e8ad1ac494d73f78cbfd9cfbf2c11704ad2d74b"},
    {"main_loop_frame_tick_call",0x72faf0,0x32,"d2dc7882f6f30bd1b9272eae441199355f71ee8c92367d7e3df48f6d13e4b285"},
    {"move_worker_run_entry",0x1212490,0x35,"b09c6bed2ccf279993e1f55142bfa88c501af14f776503a2cdba90023afd3c5d"},
    {"move_worker_frame_start",0x12125a0,0x80,"57db462588f8cf62bf9dbac327343157a2e2484b2e367a261bca245102f29d5c"},
    {"move_worker_vbi_dispatch",0x1212653,0x5d,"7ff3eedea62f3e1d21f05452ceea7f367f4edf5ab2b9655245ae69d32940680f"},
    {"thread_rng_seed",0x1a47fe0,0x53,"24eec37fc10f071a3bcf4386c56a1ff585875e5b9a0472cf76e76809178566e3"},
    {"frame_tick_head",0x2024fc0,0x20,"e88caba8ade7761b37c02ad9971fdad98bf860c6cd148a845679ccfe94651a14"},
    {"frame_tick_movie_branch",0x2025126,0x15,"7480f8d493097421addcea88ed3819e301e72c257fe188442412a93deb96215e"},
    {"frame_tick_time_advance",0x2025260,0x95,"77e65fe042991e20e6af69ae8304a7222b7330fd1eae4ca9e6d85c473c9ae149"},
    {"rodata_one_sixtieth",0x2e24578,0x8,"c6c9a57771f2313713756c05b0b8a1413e5ef99d747cabf716952b4f6137f34b"},
    /* World identities: the map insert (hooked) and one inline import site of the UniverseID map. */
    {"import_map_insert",0x10c8140,0x32b,"28ba29a7ead169fc58d632a2adc101e3b5f4944e48901fafaac2d3cc150d9891"},
    {"import_universe_site",0x128872c,0x22,"982fd4a0819c16949aa25c67d743595d8b67368f0790a46362e55e514001c24d"},
    {"sem_release",0x1a23be0,0x6a,"2f7e3764d17dc9ff4269fd115a22ed3ec472a7612846d55b6b33a0e771c7f7bd"},
    {"sem_timed_wait",0x1a23d90,0xe7,"01c00188fce9c32961ce0c59f7a017169a8ebedc2e1eeb564b294b6b421a8671"},
    {"path_sync_time",0x1a3b900,0x54,"74821eeba01b307a4a3967130c7847ae14845e58c63f8c3112216dbce922bab6"},
    {"path_update_batch",0x13e32f0,0x160,"3dc45d0c7969a2aa202cebd4ef72cafad7d8f76e44aa1c54aec019a4b7bf7706"},
    {"path_run_batches",0x13e3d86,0x116,"bf41201f69cd1aac19d13349ef89eec5ae943392b05a4db95b76dcb6b340579c"},
    {"path_budget_store",0x13e92a7,0x19,"e4b19dc95620f502ba09c0e1c23dc429ebfc7854ca9b2e67f5ea4e842829a7db"},
    {"region_move",0x12c7f80,0x92,"3d1a19b30dc2fd1f44b9bae101d1d1aacec7813d1544270246263d7204f1c8e8"},
    {"region_field_update",0x12d7a80,0xb0,"89dbf485bcf9b7d386c6ab92b95c3deb6fc2e437bd8846263a4ff44e5d657c98"},
    {"ai_wait_restore",0x10c12b0,0x40,"0a7fa69af396d07109816dea7ef818b5d2147c7e052178be246da4a0971ff8bb"},
    {"ai_wait_import_call",0x10df34b,0x28,"f7636150265b6d5bdaf903531a258ca26a8b38b505c22a69e4ceabfc4bb1e98c"},
    {"md_resume_caller",0xf77160,0x210,"128861905b15dec0ab064dbe73d3c8d7a5665e30ccbb3b78dde319cc860b56a2"},
    {"md_resume_delay",0xf5b380,0x60,"3e518da36da3a1b4a6a89fb1ef66fffab7715516e5d69872627c95cdda6e5c61"},
    {"cue_name_walk",0xf68af4,0x70,"0879d082533fffd424e7eee5f7174549e252ef6c4e70b8ce863662a89307fb83"},
    {"cue_name_walk_up",0xf68c68,0x78,"87def51e5e415f6247901815477730d21e4d08629a820fdc7c0b0f1380d74539"},
    {"cue_module_name",0xf48cba,0x20,"1c1ace52dcad1f611617d3db74b214839debc38115af00400db99e69cc32ff7f"},
    {"boot_hold_call",0xfeb2a9,0x18,"a17c348e0eab113d7de29118cd8139aaaf564b90e38930d74152ad906764d3f5"},
    {"boot_hold_move_head",0x10e27a0,0x60,"ce934f2a5acdc39a4036bf8b98da31641a58eb5f5fca00d88a0b4e618cc8da0c"},
    {"event_delivery_loop",0x16c6b1b,0x6e,"3e5628a4ca1480546cb7b716889fb3d6f9e39fffe67b1e2956d951c078cd4fc9"},
    {"event_deliver_head",0x16c5db0,0x48,"9cd75f11838105a05707791b0ee376d3a0c64cbac3910515290e4c6e6d036dd2"},
    {"event_slot_release",0x16c37d0,0x40,"d7c0097cc0a4ea46222eea6e4aa547a4f33f6ab9098b186c4000932d25033fd4"},
    {"event_source_scan",0x16bb2ec,0x38,"bdf8ee0389af791142e0b31fd76c440552ab40b237d064e60c367460bfc89921"},
    {"gravidar_publish_head",0x1109380,0x70,"9cec27ac775d2cc5e7445a22d5e36ef6503903ab57d98b91da00bab2ca26382d"},
    {"gravidar_unit_apply_head",0x10fd7b0,0x30,"22b10b6b13f530dada31330ca1f94c9f43b046e23f7131a008769c804f231abd"},
    {"gravidar_run_head",0x1106ee0,0xf0,"4418563e8d7eecc497342e4dde372c35a0ef930451f0609ab788eeacbbab0b84"},
    {"gravidar_run_ready",0x1107020,0x38,"da4c2e44a3c7c3e23f3a03d8da9778576e0a2e7c2dc1b3f0757d5fc5e8b2bab0"},
    {"gravidar_round_robin",0x1108bc8,0xbc,"65ad5ddeda88bb6c108d6dd2901464f6b98fe767d0885b173385cadc938e2473"},
    {"gravidar_init_kick",0x110c808,0x14,"0b5dfdf8a6a1c82a0d5b04ef17bb92a55abc965cdd54b76fe72713da8f20388b"},
    {"x4_mutex_lock",0x1a23650,0x14,"038af7d8eb54c7ed6b55e30ca283090ef065f0c80d0622fc394eb9041748890d"},
    {"x4_mutex_unlock",0x1a236a0,0x14,"1748e005766dbd9545ae98502ba01e74e2e66656756e59a7c1865597bc3c00f4"},
    {"x4_mutex_recursive",0x1a23532,0x10,"c9eb90aad7498142cb0ec8c5267f8ccea08ef2cda023490bed6272b1d1979f9f"},
    {"macro_name_getter",0xa38880,0x5,"1bbfc61fec45130b4afe17e5efd62371ae676ae11fb9a3fa196df95327d962bc"},
    {"gravidar_list_lock",0x11087c3,0x10,"5827a69a2ff7297ad3e8a7ad787ff13303d95a82689452c5d332a1171776b1df"},
    {"sector_move_head",0x127cf40,0x60,"86931f83b684278351bc291b3d5301c4c1de9b365d1e19614ea21d3abbdd2fd8"},
    {"sector_move_schedule",0x127d0d0,0x7a,"a8aaededb498a572df7d44347899fcbca0f152748165b2b93e0173a4be90fb26"},
    {"travel_block",0x14458df,0x30,"404a99c4817174d3408fb1d0447daf2e472255a56aefc878bb37aee522f0a1a9"},
    {"travel_wingman_read",0x1446b2e,0x1a,"ba89f808b91d61b0bb782435ab4b6275450334538c87e54b18e2d87b2e973e78"},
    {"path_job_thunk",0x13c45c0,0x30,"9d2fe92b693d172f454ede0cbbf45112af18f309c9f6b6be5e8e73c32a303719"},
    {"path_fresh_list",0x13e37bd,0x10,"7ed1076574866b05ebbb7f845465539271129befbe5d9c655741819ab5316a0a"},
    {"id_remove",0xee3850,0x130,"4fc7c9013593aec0f7422271f58328ccc7099d2a1b7a7dc70f558123fde9b34c"},
    {"id_assign",0xeed870,0x240,"42f20383fa7c638d9536fcece3d03e5528e2db1b550cf15992b80bf0d1be7096"},
    {"nav_move_head",0x144a9e0,0x4c,"c31682e8f4977e36adad798bb52c33a5d6af349cddda5872e1963bb3dd41a0e1"},
    {"nav_params_block",0x144aa5d,0x4f,"59b4ad045f415d2a254af73447551b1783fc40b6b047776499cc2a9c214a8a08"},
    {"nav_probe_block",0x144ad0e,0x60,"3a76cc5c1712d543ae670bb7fdcef1072adefa6d8882edf64c4421f4a5c79b75"},
    {"nav_params_far",0x144b2f0,0x32,"ee2c1a62167795ceb5ff176f468bdf1ae8d73f0301f16cd3df547b6eb2baf3eb"},
    {"nav_probe_far",0x144b578,0x22,"ad3cacc2e2823d2e0fa59e577d7669cff003fe6eca3dbf6837e4c6c891ec8720"},
    {"attention_timer",0x1465030,0xf0,"6edf27dab4c745b10835ccc7a357f0932a176afbcce828ccd2f5a25db2093801"},
    {"ll_wares_call",0x183a470,0x5,"5430001687a6782074823ee887705c640e3b992c4edb2ea6ba4c0d3fac483c86"},
    {"ll_deployable_call",0x183a537,0x5,"cdb81bef15680e4cbd1381304d9ce77dcb3da3d55378a080e77b413118922a51"},
    {"ll_unit_call",0x183a6d7,0x5,"39c828e39d20766a856b17a983b81382237caeca4e4964cbe8bceca1d85f577d"},
    {"ll_ammo_call",0x183ac7f,0x5,"c682f725aa0f0b2e78dc8ffc9a05f417a39296d4cc3412106a5a925701afa5b3"},
    {"ll_existing_call",0x17e9063,0x5,"8cff4d0b9d1af891e7a1dd47a94fc9776e0f1197aa4640a9b977f1d88821f25b"},
    {"ll_pick_call",0x1defce1,0x5,"b5850b673fc0768af1bfd08c4578ddb15bc4de151bf07a8c676dd8aa40c6d9f4"},
    {"ll_rest_unit",0x1e9e7ad,0xf1,"75934bc056c71f6b8216701ddca6dbc1e44dd10f9c7ddebe92aa1904ac8a9446"},
    {"ll_rest_unit_exit",0x1e9e6b2,0x24,"2b38c5a371fc7792d11237b700c5cd3421f1875a0f8b41343db3b4db0e7501dc"},
    {"ll_rest_deployable",0x1df8187,0x12d,"cad223a2c9a29a015ebc98c65da29036bfa2e2cd1b80bfdb6395c486b4990734"},
    {"ll_rest_deployable_tail",0x1df8378,0x6,"87c100073f64384823bfb38deaee8ba4cb98c0c0a52e18554bd37068232f259d"},
    {"ll_rest_deployable_exit",0x1df7a02,0x12,"960d9436a602468076ecd0211e885fe3c07396c16a4c1b76d5b20bad272d93d5"},
    {"spawn_generate",0xd2a470,0x118,"68d3a7ddc2d7abd319bf25cb30636c9930d27423465d531181b61f974d347c97"},
    {"spawn_generate_core",0xd28fc0,0x50,"4a290f96608506efefe0d1b4699398bc192f1aacbc8e0aada945fa390645d2c5"},
    {"spawn_initialize_ship",0xd029a0,0x40,"3c9843531ecf2773b4a9bcbccdf09c10da69e0689c2d78f86cfdf67c60142df2"},
    {"gravidar_scan_groups",0x1caf009,0x30,"4bc72dbb1aaca2184158c8bf5512f273a0a0d6871b8e1de288f41646746a51a5"},
    {"gravidar_group_init",0x10ff73b,0x26,"dca043e4618cf83ee17c204d94dbe3fccc4dd0a8e2002d42dc122b6cd5c460a8"},
    {"find_order_execute",0xd87ad0,0x70,"5fc0b6a1bfb50182222ecd1133469332582f404ace3f96a5f4aa03262a0561f5"},
    {"script_exec_run",0xd5fbb0,0x73,"ba595478c53852158f7ca0d93fc6a24ab3a4e0aa7442dfa3d74510d137c490f1"},
    {"script_exec_range",0xd5fc30,0x6f,"a75340d2594bb8d07cf693976b4630a66483a9ab63db2dd1411089540470b450"},
    {"script_ai_event",0x10d4270,0x50,"5a5288bf8d4649e9cbff9fa15b725393604cf377952019b6ed49ef84eef731dd"},
    {"script_stack_event",0x10d4060,0x60,"2e0531ca319aa15c081dcb0bd855e6e83ff2b7aac607bed8d63b1059e3741ea7"},
    {"script_cue_check",0xf76110,0x30,"153b266840dfa400d36232054984f1b58010920efa75ebf326353226cff42ae7"},
    {"script_cue_event",0xf799c0,0x5,"529c42874c3721ae4eacc50351b52aa4394e77063b350c61def1e5eb1db3e805"},
    {"script_cue_wait_site",0xf76500,0x18,"217674bfb2496a9da8474ac3eb379fca9e527c0ca686179a06c571053074c94a"},
    {"script_cue_raise_site",0xf78d30,0x10,"5022fe7ccee724d6de65038962de3a86f555b0754596ec99fd4eab81839d4da8"},
    {"script_cue_timed_site",0xf79d10,0x10,"2d08723110d795d5e2a465bd828319b1716e7997eb1e5d0bf7a1e8e0a7ae5674"},
    {"script_cue_value",0xf5b3e8,0x28,"6930ebececcbeb5e80609e832f9f430d56b252f333617cd04c8572f1a2fdf9b0"},
    {"import_map_clear",0x11e0b30,0xb0,"74e893b9c8b3765d6ecae61a3466a8bf0f869b82854838f383cd998e97b4b3b4"},
    {"import_clear_load_a",0x1cdd737,0xc,"bd1343601d67f21d4934ae7c5b41476a601f76c77d7c7a795fd03368ef658581"},
    {"import_clear_load_b",0x1cddf54,0xc,"412caedcd74e04b7f0dda787501c8c96ad44fc50ce894522964c6ab4f15d4a01"},
    /* World hash: find, registry, bank selectors and the layouts it reads (Windows offsets in gate_world.h). */
    {"component_find",0xa68df0,0x93,"6177d6aa24635956c7ed938eb80e67b6175ce95a1fb87bc1d0ef261eb9ef1684"},
    {"component_find_use",0xa3c096,0x13,"30981a421f027e8f70cf2a3b40809c57115b513f5a4a12eac3a32049122e657a"},
    {"move_bank_swap",0x13e3560,0x7d,"a5cd9f3ad84083aca642f843e6b6e803f87565c84adbfec928fce568b4b1efa5"},
    {"nav_select",0x14498d0,0x552,"24bcf9859bf3e139fb8214c95d64b9d11fa99a6a846f405054877911a33f2dac"},
    {"nav_agent_construct",0x13d1dd0,0x109d,"e93ba23b94d19e6d754822a11afcab0df82f653c1f74ae73084484cadc19ae3d"},
    {"nav_agent_commands",0x14286a0,0x31a,"d88d9de43b1ead89cb51ec811d68539d1e91fcadec733628f3df5aab41c20df0"}, /* up to the exact-reciprocal window 0x14289ba */
    {"component_controller",0x13142a0,0x1cf,"2593d770c68f51f6377bc734e995a76e2b1c0fdcc21a73042b271303b0812d91"},
    {"component_hull",0x11b4230,0x87,"e750d3e995fc0bc47b8a2932e43e8209490df0bb88ca9511f6e2eedc29fcb7b9"},
    {"component_flags",0x18c73b0,0x96,"fdeabdccb6836ba76a71270b78b48391a137f936ce803a750a9297950f765bfa"},
    {"component_fields",0x14670f0,0x1617,"49716fb3a8a863cfe3525cae32e22f96f8cd30068e62d4933c282b1717a4f392"}, /* around the window 0x1468707 */
    {"component_fields_after",0x146870f,0x1180,"0a1c2a890be795f3b0dfe3697dca6be6fa0e8b4bc6e0a83e9a1a6cfa19edfa56"},
    {"shield_percent",0x17d7b70,0xb6,"d8e75cdbda4450259bb1b2503407f66ceba721584f6e9384a64b4ae3b2bb6f30"},
    {"shield_recharge",0x1293330,0x32e,"0e5e820415c66333da71b051d131239375d0cb5ccc86a201b6e099ef4bdd5e20"},
    {"shield_group_construct",0x17be2e0,0x28e,"57b744a6fe7b8eca588d4104746b38b5559c1a831d9cfd8a78f659b6c247685c"},
    {"dock_evaluate",0x1758130,0x1268,"da65aa570ad47377504f25a3762a3a4a414fe6ffdbae4d52cd863baef75640c5"},
    {"dock_state",0x176d330,0x3e4,"e6dedf6013e69fe35cda4e01d71619f31063787ca7bdab0a7277d331436adfc3"},
    {"undock_state",0x118d1c0,0x17b,"79f1a74439df157a685c6f3beba3c329ccc1fc561abe265822d15939f79c2cb9"},
    {"static_value",0xd58000,0x27,"a062371a0ca71707d40dcd41e6ecfe0a219ba9e10ff58bd67f2136a3d5d76b6c"},
    {"movement_value_set",0xe7ed30,0x29,"1e1b2abccd23b0c40a35722ede28ca40d004e9cd032156a21ad90043a108f8f2"},
    {"movement_value_ref",0xd57f00,0x5,"fe5d0803355430c1264111b6534266cc4ee3a28ac617487a8aa094b3f344c0e9"},
};
#define LX_SITE_DT "frame_tick_time_advance" /* hashed with the two dt qwords normalised to the original */
