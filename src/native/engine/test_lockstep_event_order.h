/* Synthetic due-event batches: two address layouts of the same sources must be
 * delivered in one order. Also runs, local identities, cleared slots, nested
 * passes, lookup failure and call-site ownership. No native code is executed. */
#define TEST_EO_SLOTS 64u
#define TEST_EO_SOURCES 5u
static unsigned char test_eo_batch[TEST_EO_SLOTS*EVENT_ORDER_STRIDE];
static unsigned char test_eo_sources[2][TEST_EO_SOURCES][0x40];
static unsigned char test_eo_events[2][TEST_EO_SLOTS][16];
static volatile uint64_t test_eo_count;
static unsigned test_eo_layout,test_eo_seen[TEST_EO_SLOTS],test_eo_seen_count;
static int test_eo_clear_after=-1,test_eo_clear_label=-1,test_eo_nest_after=-1;
static void test_eo_put(uint64_t slot,unsigned source,unsigned label,double time) {
    unsigned char *s=test_eo_batch+slot*EVENT_ORDER_STRIDE,*src=source<TEST_EO_SOURCES?test_eo_sources[test_eo_layout][source]:NULL;
    unsigned char *event=test_eo_events[test_eo_layout][label];
    memset(s,0x5a,EVENT_ORDER_STRIDE); /* Fields other than source/event/time must stay put. */
    memcpy(s+0x18,&src,8);memcpy(s+0x28,&event,8);memcpy(s+0x30,&time,8);
}
static unsigned test_eo_label(const void *event) {
    uintptr_t base=(uintptr_t)test_eo_events[test_eo_layout],at=(uintptr_t)event;
    assert(at>=base && at<base+sizeof(test_eo_events[0]) && (at-base)%16==0);
    return (unsigned)((at-base)/16);
}
static void test_eo_loop(uint64_t begin);
static void test_eo_native(void *source,void *event,double time) {
    unsigned label=test_eo_label(event);(void)time;
    assert(source && test_eo_seen_count<TEST_EO_SLOTS);test_eo_seen[test_eo_seen_count++]=label;
    if((int)label==test_eo_clear_after) {
        /* EventSource destructor: clears only the dying source's slot field. */
        for(uint64_t i=0;i<test_eo_count;i++) {
            unsigned char *s=test_eo_batch+i*EVENT_ORDER_STRIDE;void *e;memcpy(&e,s+0x28,8);
            if(e==test_eo_events[test_eo_layout][test_eo_clear_label]) memset(s+0x18,0,8);
        }
    }
    if((int)label==test_eo_nest_after) {
        /* Nested ProcessDueEvents: appends after the live pass and restores the count. */
        uint64_t start=test_eo_count;
        test_eo_put(start,4,40,9.0);test_eo_put(start+1,0,41,8.0);test_eo_count=start+2;
        test_eo_nest_after=-1;test_eo_loop(start);assert(test_eo_count==start);
    }
}
/* Mirrors the native slot loop at 0x99c3c0: count read once, empty source
 * skipped, every slot released after the call. */
static void test_eo_loop(uint64_t begin) {
    uint64_t end=test_eo_count;
    for(uint64_t i=begin;i<end;i++) {
        unsigned char *s=test_eo_batch+i*EVENT_ORDER_STRIDE;void *source,*event;double time,none=-1.0;
        memcpy(&source,s+0x18,8);
        if(source) {memcpy(&time,s+0x30,8);memcpy(&event,s+0x28,8);
            event_order_deliver_with(test_eo_native,test_eo_batch,&test_eo_count,source,event,time);}
        memset(s+0x18,0,8);memset(s+0x28,0,8);memcpy(s+0x30,&none,8);
    }
    test_eo_count=begin;
}
static void test_eo_identities(unsigned layout) {
    /* Saved IDs are shared; runtime IDs differ per layout, including the
     * unsaved source 4 whose order is therefore only locally decided. */
    const uint64_t saved[TEST_EO_SOURCES]={0x50,0x40,0x30,0x20,0};
    for(unsigned k=0;k<TEST_EO_SOURCES;k++) {
        uint64_t runtime=layout?0xe0e0000900ULL-k:0xe0e0000100ULL+k;
        memcpy(test_eo_sources[layout][k]+8,&runtime,8);
        if(saved[k]) assert(world_identity_record(runtime,saved[k]));
    }
}
static void test_eo_fill(unsigned layout) {
    /* The same multiset of runs in two address orders; source 0 has a run of
     * three (two equal-time events keep their FIFO order), source 2 is earliest. */
    static const unsigned order[2][TEST_EO_SOURCES]={{0,1,2,3,4},{3,2,4,1,0}};
    test_eo_layout=layout;uint64_t slot=0;
    for(unsigned k=0;k<TEST_EO_SOURCES;k++) switch(order[layout][k]) {
    case 0:test_eo_put(slot++,0,0,1.0);test_eo_put(slot++,0,1,1.0);test_eo_put(slot++,0,2,2.0);break;
    case 1:test_eo_put(slot++,1,3,1.0);break;
    case 2:test_eo_put(slot++,2,4,0.5);break;
    case 3:test_eo_put(slot++,3,5,1.0);break;
    default:test_eo_put(slot++,4,6,1.0);break;
    }
    test_eo_count=slot;test_eo_seen_count=0;
}
static void test_eo_reset_counters(void) {
    event_order_calls=event_order_passes=event_order_boot_passes=event_order_total=event_order_run_total=0;
    event_order_local=event_order_ties=event_order_local_ties=event_order_collisions=event_order_moved=event_order_saturated=0;
    event_order_hash=0xcbf29ce484222325ULL;event_order_failures=0;event_order_next=0;event_order_stage="idle";
    event_order_view_runs=event_order_view_entries=0;
}
static void test_event_order(void) {
    if(!world_identities) {
        world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        assert(world_identities);
    }
    test_eo_identities(0);test_eo_identities(1);
    /* Time key: monotone across signs and zero, infinities at the ends. */
    const double times[]={-INFINITY,-2.0,-1.0,-0.0,0.0,1e-300,1.0,2.0,INFINITY};
    for(unsigned i=1;i<sizeof(times)/sizeof(*times);i++) {
        uint64_t a,b;memcpy(&a,&times[i-1],8);memcpy(&b,&times[i],8);
        assert(event_order_time_key(a)<event_order_time_key(b));
    }
    int saved_active=stats.active,saved_boot=boot_mode;stats.active=1;boot_mode=0;
    /* Disarmed: native slot order, untouched batch. */
    test_eo_reset_counters();event_order_armed=0;test_eo_fill(0);test_eo_loop(0);
    const unsigned native_a[]={0,1,2,3,4,5,6};
    assert(test_eo_seen_count==7 && !memcmp(test_eo_seen,native_a,sizeof(native_a)) && !event_order_calls);
    /* Armed: both layouts deliver (time, saved ID, then local runtime ID). */
    const unsigned expected[]={4,5,3,0,1,2,6};uint64_t hashes[2];
    event_order_armed=1;
    for(unsigned layout=0;layout<2;layout++) {
        test_eo_reset_counters();test_eo_fill(layout);test_eo_loop(0);
        assert(test_eo_seen_count==7 && !memcmp(test_eo_seen,expected,sizeof(expected)));
        assert(event_order_calls==7 && event_order_passes==1 && event_order_total==7 && event_order_run_total==5 &&
               event_order_local==1 && event_order_ties==3 && event_order_local_ties==1 && !event_order_collisions &&
               !event_order_failures && event_order_last_count==7 && event_order_last_runs==5 && !strcmp(event_order_stage,"done"));
        hashes[layout]=event_order_hash;
        for(unsigned i=0;i<7;i++) { /* Released, and the 0x5a fill outside the moved fields survived. */
            const unsigned char *s=test_eo_batch+i*EVENT_ORDER_STRIDE;
            assert(s[0]==0x5a && s[0x17]==0x5a && s[0x20]==0x5a && s[0x27]==0x5a);
        }
    }
    assert(hashes[0]==hashes[1] && event_order_moved);
    /* View-local sources (U::UIElement): delivered in place, left out of the hash and the local and tie
     * counts; a client with one more of their events (here: none at all) keeps the same hash. */
    {
        void *uielement=image+EVENT_ORDER_VIEW_LOCAL;uint64_t view_hashes[3];
        for(unsigned layout=0;layout<2;layout++) memcpy(test_eo_sources[layout][4],&uielement,8);
        for(unsigned layout=0;layout<2;layout++) {
            test_eo_reset_counters();test_eo_fill(layout);test_eo_loop(0);
            assert(test_eo_seen_count==7 && !memcmp(test_eo_seen,expected,sizeof(expected)));
            assert(event_order_run_total==5 && !event_order_local && event_order_ties==2 && !event_order_local_ties &&
                   event_order_view_runs==1 && event_order_view_entries==1 && !event_order_failures);
            view_hashes[layout]=event_order_hash;
        }
        test_eo_reset_counters();test_eo_fill(0);test_eo_count=6; /* layout 0 puts source 4 last: drop its event */
        test_eo_loop(0);assert(test_eo_seen_count==6 && !event_order_view_runs && !event_order_local);
        view_hashes[2]=event_order_hash;
        assert(view_hashes[0]==view_hashes[1] && view_hashes[1]==view_hashes[2] && view_hashes[0]!=hashes[0]);
        char text[640];assert(event_order_status(text,sizeof(text))>0 && strstr(text," view_runs=0 view_entries=0"));
        for(unsigned layout=0;layout<2;layout++) memset(test_eo_sources[layout][4],0,8);
    }
    /* A source destroyed mid-pass: its later slot is cleared and skipped; no resort. */
    test_eo_reset_counters();test_eo_fill(1);test_eo_clear_after=5;test_eo_clear_label=1;test_eo_loop(0);
    const unsigned cleared[]={4,5,3,0,2,6};
    assert(test_eo_seen_count==6 && !memcmp(test_eo_seen,cleared,sizeof(cleared)) && event_order_passes==1 && !event_order_failures);
    test_eo_clear_after=test_eo_clear_label=-1;
    /* A nested pass is sorted on its own; the enclosing pass is not resorted. */
    test_eo_reset_counters();test_eo_fill(0);test_eo_nest_after=3;test_eo_loop(0);
    const unsigned nested[]={4,5,3,41,40,0,1,2,6};
    assert(test_eo_seen_count==9 && !memcmp(test_eo_seen,nested,sizeof(nested)) && event_order_passes==2 && !event_order_failures);
    /* Unknown call arguments: native delivery of exactly those, failure, then
     * native passthrough until the gate stops. */
    test_eo_reset_counters();test_eo_fill(0);test_eo_seen_count=0;
    event_order_deliver_with(test_eo_native,test_eo_batch,&test_eo_count,test_eo_sources[0][1],test_eo_events[0][9],1.0);
    assert(test_eo_seen_count==1 && test_eo_seen[0]==9 && event_order_failures==1 && !strcmp(event_order_stage,"find"));
    test_eo_seen_count=0;test_eo_loop(0);
    assert(test_eo_seen_count==7 && !memcmp(test_eo_seen,native_a,sizeof(native_a)) && !event_order_passes);
    /* Census rows: one per collected entry in sorted slot order, recorded in boot
     * passes and gated ticks up to the limit; vtables are read as RVAs only. */
    int saved_phase=boot_phase;uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;
    test_eo_reset_counters();event_order_row_cursor=0;event_order_row_faults=0;
    event_order_rows_armed=1;event_order_rows_ticks=1;boot_mode=1;boot_phase=2;stats.issued=0;
    unsigned char *source_vtable=image+0x2a91750,*event_vtable=image+0x2b00010;
    memcpy(test_eo_sources[0][2],&source_vtable,8);memcpy(test_eo_events[0][4],&event_vtable,8);
    test_eo_fill(0);test_eo_loop(0);
    char row[384];assert(event_order_row_cursor==7 && !event_order_row(7,row,sizeof(row)));
    assert(event_order_row(0,row,sizeof(row)) && strstr(row,"index=0 pass=1 phase=2 tick=0 slot=0 source=saved:0000000000000030 "
        "stype=2a91750 etype=2b00010 due=3fe0000000000000 now=")==row);
    assert(event_order_row(1,row,sizeof(row)) && strstr(row," slot=1 source=saved:0000000000000020 stype=ffffffff etype=ffffffff due=3ff0000000000000 "));
    assert(event_order_row(6,row,sizeof(row)) && strstr(row," slot=6 source=runtime:000000e0e0000104 "));
    boot_phase=3;stats.issued=1;test_eo_fill(1);test_eo_loop(0);
    assert(event_order_row_cursor==14 && event_order_row(13,row,sizeof(row)) && strstr(row,"pass=2 phase=3 tick=1 slot=6 source=runtime:000000e0e00008fc "));
    stats.issued=2;test_eo_fill(0);test_eo_loop(0);assert(event_order_row_cursor==14 && event_order_passes==3);
    /* Bounded ring: rows overwritten before draining report evicted=1. */
    event_order_row_cursor=EVENT_ORDER_ROWS+10;
    assert(event_order_row(9,row,sizeof(row)) && !strcmp(row,"index=9 evicted=1") && event_order_row(10,row,sizeof(row)) &&
           strstr(row,"index=10 pass=") && !event_order_row(EVENT_ORDER_ROWS+10,row,sizeof(row)));
    char rows_status[768];assert(event_order_status(rows_status,sizeof(rows_status)) && strstr(rows_status," rows_armed=1 row_ticks=1 rows=65546 row_overwritten=10 "));
    event_order_row_cursor=0;event_order_rows_armed=0;memset(test_eo_sources[0][2],0,8);memset(test_eo_events[0][4],0,8);
    /* Arming: only after event order, before loading, on the UI thread; a reload
     * must repeat the same limit. */
    boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();assert(!event_order_rows_arm(3));
    event_order_installed=1;assert(!event_order_rows_arm(601) && event_order_rows_arm(3) && event_order_rows_arm(3) && !event_order_rows_arm(4));
    event_order_installed=0;event_order_rows_armed=0;event_order_rows_ticks=0;
    boot_phase=saved_phase;stats.issued=saved_issued;ui_thread=saved_ui;
    test_eo_reset_counters();event_order_armed=0;stats.active=saved_active;boot_mode=saved_boot;
    /* Call-site word: owner-checked swap and restore, never over a foreign word. */
    uint64_t native_word=EVENT_ORDER_NATIVE_WORD,patched=native_word^0x0000ffffffff0000ULL;
    memcpy(image+EVENT_ORDER_SITE,&native_word,8);
    assert(event_order_swap(native_word,patched) && !event_order_swap(native_word,0) && *(uint64_t*)(image+EVENT_ORDER_SITE)==patched);
    event_order_original_word=native_word;event_order_patched_word=patched;event_order_installed=1;
    *(uint64_t*)(image+EVENT_ORDER_SITE)=patched^1;event_order_restore();
    assert(event_order_installed && !event_order_restored && *(uint64_t*)(image+EVENT_ORDER_SITE)==(patched^1));
    *(uint64_t*)(image+EVENT_ORDER_SITE)=patched;event_order_restore();
    assert(!event_order_installed && event_order_restored && *(uint64_t*)(image+EVENT_ORDER_SITE)==native_word);
    memset(image+EVENT_ORDER_SITE,0,8);event_order_restored=0;event_order_original_word=event_order_patched_word=0;
    /* Arming needs an active boot gate before loading with world identities. */
    assert(!event_order_arm() && !event_order_armed && !event_order_installed);
    char status[512];assert(event_order_status(status,sizeof(status)) && strstr(status,"armed=0 installed=0 "));
    puts("event order portable tie/run/clear/nested/failure/census row/call-site owner tests PASS");
}
/* Pins and the call-site word against an independent read-only image copy. */
static void test_event_order_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    assert(event_order_pins() && *(uint64_t*)(image+EVENT_ORDER_SITE)==EVENT_ORDER_NATIVE_WORD);
    /* The call targets DeliverEvent from the pinned return address. */
    int32_t rel;memcpy(&rel,image+EVENT_ORDER_SITE+2,4);assert(EVENT_ORDER_RETURN+rel==EVENT_ORDER_DELIVER);
    const unsigned tails[]={0x99bb50,0x99c3d1,EVENT_ORDER_RETURN,0x99c46a,EVENT_ORDER_DELIVER,0x99c642,0x99ba70,0x99bb1a,0x998f83,0x998fb9};
    for(unsigned i=0;i<sizeof(tails)/sizeof(*tails);i++) {image[tails[i]]^=1;assert(!event_order_pins());image[tails[i]]^=1;}
    assert(event_order_pins());
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("event order native pins, DeliverEvent call-site word and tamper refusals verified");
}
