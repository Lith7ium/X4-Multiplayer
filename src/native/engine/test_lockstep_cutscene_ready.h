/* Cutscene ready on the game clock (lockstep_cutscene_ready.h) on a fake region: the native post's site,
 * the manager's active list, first sight, the post 2.0 s later with X4's event fields, once; a cutscene
 * stopped before its deadline; the site checked every pass; restore. The image test checks the pins. */
static double test_cr_time;
static unsigned test_cr_posts;
static unsigned char *test_cr_last_event,*test_cr_last_source;
static double test_cr_last_time;
static int test_cr_now(double *now) {*now=test_cr_time;return 1;}
static void *test_cr_alloc(size_t size,uintptr_t a,uintptr_t b,uintptr_t c,unsigned align) {
    (void)a;(void)b;(void)c;assert(size==0x48 && align==0x10);return _aligned_malloc(size,16);
}
static void test_cr_copy(void *dst,const void *src) {memcpy(dst,src,0x20);}
static void test_cr_post(void *source,void *event,double time) {
    ++test_cr_posts;test_cr_last_source=source;test_cr_last_event=event;test_cr_last_time=time;
}
static void test_cutscene_ready(void) {
    unsigned char *region=VirtualAlloc(NULL,CR_MANAGER+0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(region);
    static __attribute__((aligned(16))) unsigned char manager[0x100],sentinel[0x20],node1[0x20],node2[0x20],cs1[0x500],cs2[0x500];
    unsigned char *saved_base=cr_base;cr_fns saved_fns=cr_fn;uint64_t saved_issued=stats.issued;
    cr_base=region;cr_fn=(cr_fns){test_cr_alloc,test_cr_copy,test_cr_post,test_cr_now};
    /* The native post's site as X4 has it; the patch is nop + jmp with the same rel32. */
    region[CR_SITE]=0x0f;region[CR_SITE+1]=0x85;
    assert(cr_site()==CR_ORIGINAL && cr_swap(CR_ORIGINAL,CR_PATCHED) && region[CR_SITE]==0x90 && region[CR_SITE+1]==0xe9);
    cr_patched=1;cr_armed=1;cr_failures=0;cr_posted=cr_seen=cr_dropped=cr_frames=0;memset(cr_table,0,sizeof(cr_table));cr_row_head=cr_row_tail=0;
    /* No manager (menus): nothing to do. */
    memset(region+CR_MANAGER,0,8);assert(cr_before_release() && !test_cr_posts);
    /* The manager with one active cutscene (key "npc_comm", id 7). */
    void *p=manager;memcpy(region+CR_MANAGER,&p,8);
    p=sentinel;memcpy(manager+CR_ACTIVE,&p,8);
    memset(cs1,0,sizeof(cs1));*(int*)(cs1+CR_ID)=7;*(uint64_t*)(cs1+CR_EXTRA)=0x6868;memcpy(cs1+CR_KEY,"npc_comm",8);
    {uint64_t size=8,capacity=15;memcpy(cs1+CR_KEY+0x10,&size,8);memcpy(cs1+CR_KEY+0x18,&capacity,8);}
    p=node1;memcpy(sentinel,&p,8);p=sentinel;memcpy(node1,&p,8);p=cs1;memcpy(node1+0x10,&p,8);
    test_cr_time=100.0;stats.issued=50;test_cr_posts=0;
    assert(cr_before_release() && !test_cr_posts && cr_seen==1 && !cs1[CR_FLAG]);
    test_cr_time=101.9;stats.issued=107;assert(cr_before_release() && !test_cr_posts);
    /* 2.0 s after first sight: X4's event, posted once through X4's own deferred post; the flag set as X4 sets it. */
    test_cr_time=102.0;stats.issued=110;assert(cr_before_release() && test_cr_posts==1 && cs1[CR_FLAG]==1 && cr_posted==1);
    assert(test_cr_last_source==manager && test_cr_last_time==102.0);
    {   void *vt=NULL;memcpy(&vt,test_cr_last_event,8);
        assert(vt==(void*)(region+CR_EVENT_VTABLE) && !*(uint32_t*)(test_cr_last_event+8) && !*(uint64_t*)(test_cr_last_event+0x10) &&
               !memcmp(test_cr_last_event+0x18,cs1+CR_KEY,0x20) && *(int*)(test_cr_last_event+0x38)==7 && *(uint64_t*)(test_cr_last_event+0x40)==0x6868);
        _aligned_free(test_cr_last_event);}
    {const char *row=cr_take_row();assert(row && !strcmp(row,"tick=110 id=7 key=npc_comm") && !cr_take_row());}
    test_cr_time=103.0;assert(cr_before_release() && test_cr_posts==1);
    /* A second cutscene stopped before its deadline: dropped, never posted; the first leaves the list too. */
    memset(cs2,0,sizeof(cs2));*(int*)(cs2+CR_ID)=8;
    p=node2;memcpy(sentinel,&p,8);p=sentinel;memcpy(node2,&p,8);p=cs2;memcpy(node2+0x10,&p,8);
    test_cr_time=104.0;assert(cr_before_release() && cr_seen==2);
    p=sentinel;memcpy(sentinel,&p,8);
    test_cr_time=107.0;assert(cr_before_release() && test_cr_posts==1 && cr_dropped==1 && !cr_table[0].cutscene && !cr_table[1].cutscene);
    {char row[256];assert(cr_status(row,sizeof(row)) && strstr(row,"armed=1 site=e990 seen=2 posted=1 dropped=1 "));}
    /* The site changed by someone else: stop (178) on the next pass. */
    region[CR_SITE]=0x0f;assert(!cr_before_release() && !strcmp(cr_stage,"site_changed") && cr_failures==1);
    region[CR_SITE]=0x90;cr_failures=0;
    /* Restore: X4's bytes back. */
    cr_restore();assert(!cr_armed && cr_restored && region[CR_SITE]==0x0f && region[CR_SITE+1]==0x85);
    cr_base=saved_base;cr_fn=saved_fns;stats.issued=saved_issued;cr_restored=0;cr_stage="off";
    VirtualFree(region,0,MEM_RELEASE);
}
