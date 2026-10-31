/* Read-only frame-end diagnostics. Raw motion-property words are not a portable
 * digest: only fields whose meaning has been established may be interpreted. */
static int body_state(unsigned char *object,unsigned which,char out[1024]) {
    unsigned char *record,*xbody,*scene,*system,*iface,*manager,*bodies,*body,*motion,*holder;
    void *owner,*userdata;uint32_t id,actual_id;uint64_t count,due=0;uint32_t pose[8],mp[32],pre[8],delta[8],meta[8],flags;
    if(which>1 || !sample_read(object+0x260+which*8,&record,8)) return 0;
    if(!record) {strcpy(out,"none");return 1;}
    if(!sample_read(record,&owner,8) || owner!=object ||
       !sample_read(record+0x10,&xbody,8) || !sample_read(xbody+8,&id,4) || id==0xffffffffu ||
       !sample_read(record+0x18,&scene,8) || !sample_read(scene+0x20,&system,8) ||
       !sample_read(system+0x1b8,&iface,8) || !sample_read(iface+8,&manager,8) ||
       !sample_read(manager,&count,8) || (id&0x7fffffu)>=count || count>0x800000u ||
       !sample_read(manager+0x10,&bodies,8) || !sample_read(bodies+(id&0x7fffffu)*8,&body,8) || !body || ((uintptr_t)body&1) ||
       !sample_read(body+0x70,&actual_id,4) || actual_id!=id || !sample_read(body+0x50,&userdata,8) || userdata!=record ||
       !sample_read(body+0x48,&motion,8) || !motion || !sample_read(body,pose,sizeof(pose)) ||
       !sample_read(motion,mp,sizeof(mp)) || !sample_read(body+0x60,meta,sizeof(meta)) || !sample_read(record+0x40,pre,sizeof(pre)) || !sample_read(record+0x60,delta,sizeof(delta)) ||
       !sample_read(record+0x38,&flags,4) || !sample_read(record+0x20,&holder,8) || (holder && !sample_read(holder+0x28,&due,8))) return 0;
    int n=snprintf(out,1024,"flags=%08x shape_due=%016llx body_id=%08x pose=",flags,(unsigned long long)due,id);
    n+=hex_words(out+n,1024-(size_t)n,pose,8);
    n+=snprintf(out+n,1024-(size_t)n," motion=");
    n+=hex_words(out+n,1024-(size_t)n,mp,32);
    n+=snprintf(out+n,1024-(size_t)n," pre=");
    n+=hex_words(out+n,1024-(size_t)n,pre,8);
    n+=snprintf(out+n,1024-(size_t)n," delta=");
    n+=hex_words(out+n,1024-(size_t)n,delta,8);
    meta[4]=0; /* BodyID is process-local; identity was checked above. */
    n+=snprintf(out+n,1024-(size_t)n," meta=");
    n+=hex_words(out+n,1024-(size_t)n,meta,8);
    return 1;
}
