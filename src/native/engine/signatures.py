"""Build-pinned X4 signatures; hashes only, never executable bytes."""

EXECUTABLE = {
    'size': 55412768,
    'sha256': '19750a6563889a970f434b5566eb396c6b2dc29ff814bd3e336f838176ad6891',
    'timestamp': 0x6A1EADED,
    'image_size': 0x6FA9000,
}

COMMON_SITES = {
    'move_worker_run': (0xEF4400, 25, '4449b89ae08c1ebc42ad4bb0ad61acd1c63d68de52c6ae63c74e639390f85e91'),
    'vbi_dispatch': (0xEF4681, 64, '161234434ac6f861075f55a3b7b5e4643a45c7b72045b225e4c6a3cdbc78cd6d'),
    'vbi_register': (0x14CA580, 42, '896bc70922e9954e08c838c449f51dbcdc428842a4c25dd6a80bdea2c4cc6a26'),
}

SIMTICK_SITES = {
    'game_client_move': (0x122B360, 24, '8e123f79687ad689c48d2eaf879253c42d1359f129ddebd2c2d51ea6658a46d0'),
    'pathing_wait_swap': (0xEAC170, 24, '718b690e9f3d2779d2f91c812253fb26bfa1f4ce4cd73accae7229675e261f12'),
    'ui_controller_move': (0xB3BAD0, 28, '893307c89a256e16553b164d9d16ec4f0c3187060ebd86513b8f55694f1378e4'),
}

FIXEDTICK_SITES = {
    'main_loop_frame_call': (0x12AE9C7, 24, '119ee2003ab898668e3e485723f196ff83e43d99b3e8f26f1394c59b24541d26'),
    'frame_tick_prologue': (0xFA2D70, 32, '238662a6fc129b269aec1dbf9fff25fe1aa80138c8fc3be6f996dd65921f30e7'),
    'frame_tick_dt': (0xFA32D2, 58, 'a5bc59769a92153bec1c80192aa0d71459768e85442c9c5b8d2c72536701bfd7'),
    # XPhys::Scene::Step: Jolt PhysicsSystem::Update(dt, *(*0x6cf1510) collision steps, ...).
    'physics_collision_steps': (0xBA8CE, 39, 'c667e74f191023745d6e737e530ba1753a9dcc02c8b5dc9e70247d8298f3ce8e'),
}

# Component-walk profiling (overhaul): primary-vtable Move slot 674 per class.
# (function RVA, vtable entry RVA, SHA-256 of the function's first 32 bytes). The entry is
# relocated, so it is compared at runtime against image base + function RVA.
WALK_SITES = {
    'galaxy': (0x748120, 0x2B17348, '7c0981e988f37f237e70006b1453ce0e444fad8aba53e984c09ecba96e188a86'),
    'cluster': (0x3B8280, 0x2AA39A8, '51cd721d73d5812394c24f2feaf557276a8cedc867559ea49c7ff15026869e91'),
    'sector': (0x784230, 0x2B1FF58, '25b24d9be58c6e66ccb588207d0e3b13a8135d4a1d2eb100fd567c5e4cd24858'),
    'zone': (0x834950, 0x2B67590, '32b67bb8c930181f5eec4953b151d556af8d792652e0c50a967d4690dd75c9bf'),
    'ship': (0x7CC810, 0x2B85FA8, '809f44f2b8b4712e0ca1a54404595f609dd2c134847e4c884363061a7277649c'),
    'station': (0x41A430, 0x2B779E8, 'cdf874aac2d43fa934141f7bf472c51a9ac539a6f97b4f8bb57bebcb5c47e986'),
}
# Further vtables whose Move entry holds the same function as a primary class above
# (class, vtable entry RVA). S/M/L/XL ships each have their own vtable; the base 'ship'
# vtable alone is never reached by the walk.
WALK_EXTRA_ENTRIES = [
    ('ship', 0x2B813C8),   # ship_s
    ('ship', 0x2B83950),   # ship_m
    ('ship', 0x2B9D508),   # ship_l
    ('ship', 0x2B9AF98),   # ship_xl
]
WALK_CODE_SITES = {f'walk_{name}_move': (fn, 32, sha) for name, (fn, _entry, sha) in WALK_SITES.items()}

# Server frame diet (frame-diet agent, contrib/ferrite-rw-frame-diet; reports/REWRITE-FRAME-DIET.md).
# Verified before any vtable entry, VBI slot 2 or the render flag bit is touched.
FRAMEDIET_SITES = {
    'fd_scene_manager_update': (0xAF8E50, 32, '283e2a9a4513d6ac90bb6e6524b0ba6694c3581852d401fe3c78c18610e4b8dc'),
    'fd_scene_layer_walk': (0xAF8760, 32, '779f1fe33a1e6eab6fac308382dd828d0848637b998bb887c649358c717fbee9'),
    # AnarkScene slot 5: clears the per-frame lists, then tail-calls slot 12 (jmp [rax+0x60]).
    'fd_scene_update': (0x13AA510, 166, '493453e6210de75923ae9be01aeed195c9bd02e91858142745c91a58f7c5db39'),
    'fd_ui_render': (0xB3BBA0, 32, '6846988fdb1605f03ad1d619957a9f09af88ddf9207dc3518ae3e95ad7679f00'),
    # GameClient render bank: camera loop over Universe+0x2c0 calling slot 3 (call [rax+0x18]).
    'fd_game_client_render': (0x122BFC0, 174, 'faf61b940146abf385a9fb155bf180348fcfc7b7246cd52284fa227bfa2629b2'),
    'fd_camera_render': (0x983160, 32, '67aa70787d093d7f2511df0076d4e86a07c3adb0a846cfeb50568d75f504b55d'),
    # RenderPreviousFrame: test qword [RenderFlags], 0x8000000 (disable_rendering).
    'fd_render_disable_test': (0xFA007A, 11, 'a5034f95f69d074f1c7b4d16a02b57290024868f08657472044e3c16d300ecb0'),
    # ProcessGraphicsStateChanges: staged RenderFlags (0x6adfa38) copied over the live value.
    'fd_render_flags_stage': (0xFA3811, 14, '4ddbb572933826cd11b52b3b2de1108a8791a05f2cbbd6b49ea4cc99959db0ae'),
    # Audit probes: AutoIDMap insert counter (+0x38), registry bucket layout, deferred event list size,
    # thread RNG step (TLS+0x318).
    'fd_id_insert_count': (0xCC64D, 4, '7e104436070ef74ec041571d6ced9696c075c6783c63a65a92125a49fc548d74'),
    'fd_id_map_find': (0xCED70, 64, '99aa931616db6c9f92574b2408563aa3b9f09a452bc5456ce0ec25fa8a9bb9cf'),
    'fd_event_list_push': (0x998430, 112, '39da624a9f2bca0707dfbc5475e3f31b9eb4128a899171b51a165d4b306d29d3'),
    'fd_random_float_state': (0x14DDC90, 24, '99b587b2e3a3e175da228cdcadc902b715ec9fb81f048af19e0d4d7c10afb519'),
    # ui_elements: 0x138f2c0 prologue, the element update call (lea rcx,[rdi+0x298] .. E8 rel32 at 0x138f477)
    # and the element update itself.
    'fd_presentation_update': (0x138F2C0, 32, '15a932ce611ce2558ab3a3542ad052a0a1e75f2000cdfd134fca95fe0719844a'),
    'fd_element_update_call': (0x138F455, 39, '343e65ce1b29576faeaf537b44007640b990e598d048ba818992c02ef5964338'),
    'fd_element_update': (0x1399C40, 32, 'f1dab7026e2d9766968166e135e63c22eed0c04179c72b04c4a692f7a2fa9ce7'),
    # GameRender::Move (VBI slot 10 handler): render banks, then the 0x6adfc30 release tail.
    'fd_game_render_move': (0x121C1F0, 106, '5d20ad384ae9ce74bb15625b923fd33638ad20d3f9e553c5d9cb7d264b20ed6f'),
}
# Wrapped virtuals (name, vtable entry RVA, function RVA), in framediet.c VS_* order. The entries
# hold relocated pointers; they are compared at runtime against image base + function RVA.
FRAMEDIET_VTABLE_ENTRIES = [
    ('scene_manager_update', 0x2BC0DD8, 0xAF8E50),  # UI::XAnark::AnarkSceneManager slot 3
    ('scene_layer_walk', 0x2BC0E70, 0xAF8760),      # UI::XAnark::AnarkScene slot 12
    ('ui_render', 0x2BC07E0, 0xB3BBA0),             # UI::CUIController XVbiHandler slot 2 (Render)
    ('client_render', 0x2C8ABE0, 0x122BFC0),        # X4::GameClient XVbiHandler slot 2 (render bank)
    ('camera_render', 0x2BAE1B8, 0x983160),         # U::Camera slot 3 (Render)
    ('game_render', 0x2C8AC10, 0x121C1F0),          # X4::GameRender XVbiHandler slot 1 (VBI slot 10 Move)
]

SITES = {**COMMON_SITES, **SIMTICK_SITES, **FIXEDTICK_SITES, **WALK_CODE_SITES, **FRAMEDIET_SITES}


def c_header() -> str:
    lines = [
        '#pragma once',
        f'#define X4_FILE_SHA256 "{EXECUTABLE["sha256"]}"',
        f'#define X4_FILE_SIZE {EXECUTABLE["size"]}',
        f'#define X4_PE_TIMESTAMP 0x{EXECUTABLE["timestamp"]:08x}u',
        f'#define X4_IMAGE_SIZE 0x{EXECUTABLE["image_size"]:x}u',
        'typedef struct { size_t rva; ULONG length; const char *sha; } x4_signature_check;',
    ]
    for name, sites in [('common', COMMON_SITES), ('simtick', SIMTICK_SITES), ('fixedtick', FIXEDTICK_SITES),
                        ('framediet', FRAMEDIET_SITES)]:
        lines.append(f'static const x4_signature_check x4_{name}_checks[] = {{')
        for rva, length, sha in sites.values():
            lines.append(f'    {{0x{rva:x}, {length}, "{sha}"}},')
        lines.append('};')
    lines.append('typedef struct { const char *name; size_t fn; size_t entry; ULONG length; const char *sha; } x4_walk_site;')
    lines.append('static const x4_walk_site x4_walk_sites[] = {')
    for name, (fn, entry, sha) in WALK_SITES.items():
        lines.append(f'    {{"{name}", 0x{fn:x}, 0x{entry:x}, 32, "{sha}"}},')
    lines.append('};')
    classes = list(WALK_SITES)
    lines.append('typedef struct { int cls; size_t entry; } x4_walk_extra;')
    lines.append('static const x4_walk_extra x4_walk_extras[] = {')
    for name, entry in WALK_EXTRA_ENTRIES:
        lines.append(f'    {{{classes.index(name)}, 0x{entry:x}}},')
    lines.append('};')
    lines.append('typedef struct { const char *name; size_t entry; size_t fn; } x4_framediet_vtable;')
    lines.append('static const x4_framediet_vtable x4_framediet_vtables[] = {')
    for name, entry, fn in FRAMEDIET_VTABLE_ENTRIES:
        lines.append(f'    {{"{name}", 0x{entry:x}, 0x{fn:x}}},')
    lines.append('};')
    return '\n'.join(lines) + '\n'
