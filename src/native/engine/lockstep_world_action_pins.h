/* Full contiguous Windows roots; no interpreted action payload reads. */
typedef struct {unsigned rva,size;const char *sha;} world_action_pin;
static const world_action_pin world_action_pin_list[]={
 {0xbe2320,226,"a1633b66afd1a2d3b9eebf5dedb92fb020a5e91f9b772921531f9184ad0f80ff"},
 {0xbe0ba0,280,"9d94310ae24fd08d72280562248f875c499ede1630badd8987e399ba237170d3"},
 {0xb77830,12,"51e18b47d962724247496de5ddf731b2e290ce09e94e11e5246a16f45743075c"},
 {0xc5af30,3800,"bd1c8fdb1ffe02ff249d76133c40126624022735326a736a08124e3ec14ea88a"},
 {0xc72380,3552,"e5a9077b31bf996cbea45c65aa59f755062fb4062ea7deb301f4dfae0cbf1419"},
 {0xc6f7b0,1181,"a0fec7baeee30e27c4cd03b5eff1a8524190868e881bd2264707d7b136bfa782"},
 {0xcc6710,1403,"0863f3019f3027b33c2cac7c05d10a8708bd0db6e04ce1e24cb4fbf3bc327391"},
 {0x10631b0,472,"b5468455eb7534da2330706d124b3e70c1aedf137869208687e6686adfb2917d"},
 {0x1066ba0,613,"ab00b7598d2b6cce00d9d0135844f47bd2c9a94d9a9fcc1cf9508a994a8d9434"},
 {0x1063750,3437,"0d1c301108dac632a0163a03f309310e188c2df418a4222694fcec3ca646c056"},
 {0x106d5b1,40,"98cec6c522690356d88f9c9b8be8ef9e0cd59054dc9c2ea9db26b123bcd5b154"}
};
static int world_action_dispatch_owned(void) {
    uint64_t entry;unsigned __int128 dispatch;
    return npc_exec_installed&&nested_action_installed&&
        read_local(image+NPC_EXEC_ENTRY,&entry,8)&&entry==npc_exec_patched_word&&
        read_local(image+NESTED_ACTION_WORD,&dispatch,16)&&dispatch==nested_action_patched;
}
static int world_action_pins(void) {
    if(!world_action_dispatch_owned()||!world_queue_phase_pins()) return 0;
    unsigned char original[303];
    if(!read_local(image+NPC_EXEC_ENTRY,original,sizeof(original))) return 0;
    memcpy(original,&npc_exec_original_word,8);
    memcpy(original+NESTED_ACTION_WORD-NPC_EXEC_ENTRY,&nested_action_original,16);
    if(!hash_ok(original,sizeof(original),"7509510d2582f326fb87c58a23b715d1f7499577bf681c67377347314e3aa759")) return 0;
    for(unsigned i=0;i<sizeof(world_action_pin_list)/sizeof(world_action_pin_list[0]);i++) {
        const world_action_pin *p=&world_action_pin_list[i];if(!hash_ok(image+p->rva,p->size,p->sha)) return 0;
    }
    const unsigned tables[]={0x2c0cc88,0x2c0cdd0,0x2c0fb30},targets[]={0xbe2320,0xbe0ba0,0xb77830};
    for(unsigned i=0;i<3;i++) {uintptr_t target;if(!read_local(image+tables[i]+0x18,&target,8)||target!=(uintptr_t)(image+targets[i])) return 0;}
    return 1;
}
