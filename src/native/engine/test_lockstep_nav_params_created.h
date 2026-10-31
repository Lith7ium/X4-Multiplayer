/* native_gate_nav_params_init re-bases loading-time deadlines only: ships created
 * in an admitted frame (created tick >= 1) keep their native scheduling (big-BS6),
 * ships created while loading (tick 0) and saved ships are re-based. */
static void test_nav_params_created(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    assert(NAV_CREATED_TICK_SHIFT==CREATED_TICK_SHIFT && NAV_CREATED_TICK_LIMIT==CREATED_TICK_LIMIT);
    const uint64_t spawned=CREATED_BIT|(15ULL<<CREATED_TICK_SHIFT)|0x1234,loaded=CREATED_BIT|0x5678;
    assert(nav_created_tick(spawned)==15 && !nav_created_tick(loaded) && nav_created_tick(CREATED_BIT|(CREATED_TICK_LIMIT<<CREATED_TICK_SHIFT))==CREATED_TICK_LIMIT);
    test_pc_begin();
    assert(world_identity_record(0x8100,0xae79c));
    test_pc_create(0x8101,spawned);test_pc_create(0x8102,loaded);world_portable_created=1;
    static unsigned char ship[0x300];unsigned char *s=ship;
    memset(ship,0,sizeof(ship));memset(test_nc_controller,0,sizeof(test_nc_controller));memcpy(test_nc_controller+0x28,&s,8);
    memset(nav_params_table,0,sizeof(nav_params_table));nav_params_count=0;nav_params_armed=1;
    nav_params_pins=nav_params_inits=nav_params_unmapped=nav_params_created=nav_params_first=nav_params_frame_pins=nav_params_frame_inits=0;
    nav_params_tick=UINT64_MAX;stats.issued=16;ui_thread=GetCurrentThreadId();stats.active=1;
    double v=9.0,due;
    /* Spawned at tick 15: neither reset while on nor pinned while off; not recorded. */
    *(uint64_t*)(ship+8)=0x8101;test_nc_controller[0xd8]=1;memcpy(test_nc_controller+0xd0,&v,8);
    nav_params_apply(test_nc_controller,7.0);memcpy(&due,test_nc_controller+0xd0,8);
    assert(due==9.0 && nav_params_created==1 && !nav_params_inits && !nav_params_count);
    test_nc_controller[0xd8]=0;nav_params_apply(test_nc_controller,7.0);memcpy(&due,test_nc_controller+0xd0,8);
    assert(due==9.0 && nav_params_created==2 && !nav_params_pins);
    /* The same through the cadence bracket. */
    test_nc_controller[0xd8]=1;nav_cadence_active=1;nav_cadence_sample c;nav_cadence_enter(test_nc_controller,7.0,&c);
    memcpy(&due,test_nc_controller+0xd0,8);assert(due==9.0 && nav_params_created==3 && c.valid);
    /* Created while loading (tick 0): re-based at its first call. */
    *(uint64_t*)(ship+8)=0x8102;nav_params_apply(test_nc_controller,7.0);memcpy(&due,test_nc_controller+0xd0,8);
    assert(due==7.0-1.0/60.0 && nav_params_inits==1 && nav_params_count==1 && nav_params_created==3);
    /* Saved: re-based as before. */
    *(uint64_t*)(ship+8)=0x8100;memcpy(test_nc_controller+0xd0,&v,8);nav_params_apply(test_nc_controller,7.0);
    memcpy(&due,test_nc_controller+0xd0,8);assert(due==7.0-1.0/60.0 && nav_params_inits==2 && nav_params_count==2);
    /* Without portable created keys the spawned ship is unmapped: left alone while on. */
    world_portable_created=0;*(uint64_t*)(ship+8)=0x8101;memcpy(test_nc_controller+0xd0,&v,8);
    nav_params_apply(test_nc_controller,7.0);memcpy(&due,test_nc_controller+0xd0,8);assert(due==9.0 && nav_params_unmapped==1);
    char status[1536];int n=nav_cadence_status(status,sizeof(status));
    assert(n>0 && strstr(status," params_inits=2 params_ids=2 params_unmapped=1 params_first=16 params_created=3"));
    nav_cadence_active=0;nav_params_armed=0;memset(nav_params_table,0,sizeof(nav_params_table));nav_params_count=0;
    nav_params_pins=nav_params_inits=nav_params_unmapped=nav_params_created=nav_params_first=nav_params_frame_pins=nav_params_frame_inits=0;
    nav_params_tick=UINT64_MAX;memset(test_nc_controller,0,sizeof(test_nc_controller));
    memset(nav_cadence_frame,0,sizeof(nav_cadence_frame));memset(nav_cadence_total,0,sizeof(nav_cadence_total));nav_cadence_tick=UINT64_MAX;
    test_pc_end();
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;
    printf("nav params init leaves admitted-frame creations alone, re-bases load-created and saved ships: tests PASS\n");
}
