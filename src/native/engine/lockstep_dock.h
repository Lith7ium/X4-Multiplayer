/* Read-only DockMovementController state. This controller replaces navigation
 * during docking: it has no navigation-agent buffers to read or initialize.
 * Its native transform evaluator consumes three PosRot values, initial linear
 * and angular velocity, start time, animation selector and two boolean options.
 * Undocking consumes two poses, start time, selector and one boolean option.
 * Preserve exact bits, excluding vector padding and process-local owners. */
#define DOCK_CONTROLLER_VTABLE 0x2c3fdc0u
#define UNDOCK_CONTROLLER_VTABLE 0x2c3e568u
static int dock_state_verified;
static int dock_state_pins(void) {
    if(!dock_state_verified) {
        if(!hash_ok(image+0xd6e810,3856,"8912c157e4d54e76b1a72b830f2c08bd43c7e155c32577c8e30bd9d122c1cf75") ||
           !hash_ok(image+0xd6e150,660,"e64ee51008d97447e33dc24ee6769f6e3a18af68d1f3c6ce597885f5ac54ee55") ||
           !hash_ok(image+0xd6fdf0,2275,"22d737103007e0e041ec43fd33ba5714a01b80aecc676e52778cc1cff2678cea")) return 0;
        dock_state_verified=1;
    }
    return 1;
}
static int dock_controller_state(unsigned char *controller,void *object,char out[512]) {
    void *vtable,*owner;uint32_t vectors[14][4],animation;uint64_t time_bits;double time;
    unsigned char flags[2]={0};
    if(!object || !read_local(controller,&vtable,8) || (vtable!=image+DOCK_CONTROLLER_VTABLE && vtable!=image+UNDOCK_CONTROLLER_VTABLE)) return 0;
    int undock=vtable==image+UNDOCK_CONTROLLER_VTABLE;
    if(!read_local(controller+0x70,&owner,8) || owner!=object ||
       !read_local(controller+0x20,vectors[0],64) ||
       !read_local(controller+0x80,vectors[4],undock?64:128) ||
       (!undock && !read_local(controller+0x100,vectors[12],32)) ||
       !read_local(controller+0x60,&time_bits,8) ||
       !read_local(controller+(undock?0xc0:0x120),&animation,4) ||
       !read_local(controller+(undock?0xc4:0x124),flags,undock?1:2) || flags[0]>1 || flags[1]>1) return 0;
    memcpy(&time,&time_bits,8);if(!isfinite(time)) return 0;
    unsigned count=undock?8:14;
    for(unsigned i=0;i<count;i++) for(unsigned j=0;j<3;j++) {
        float value;memcpy(&value,&vectors[i][j],4);if(!isfinite(value)) return 0;
    }
    int n=undock?snprintf(out,512,"UNDOCKCTL_1:%016llx:%08x:%02x:",(unsigned long long)time_bits,animation,flags[0]):
                 snprintf(out,512,"DOCKCTL_1:%016llx:%08x:%02x%02x:",(unsigned long long)time_bits,animation,flags[0],flags[1]);
    for(unsigned i=0;i<count;i++) for(unsigned j=0;j<3;j++) n+=snprintf(out+n,512-(size_t)n,"%08x",vectors[i][j]);
    return n==(undock?233:377);
}
