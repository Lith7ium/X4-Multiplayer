/* native_gate_command_order: the pathing pass's appended event commands, read
 * per source; counts, hashes, rows, the payload exchange of 'sort' and its
 * refusals, against a std::list laid out like g_EventCommandQueue. */
static unsigned char *test_co_sentinel;
static unsigned char test_co_events[16][0x50]; /* the trace scans qwords +0x8..+0x40 */
static void test_co_list_reset(void) {
    unsigned char *node=*(unsigned char**)test_co_sentinel;
    while(node && node!=test_co_sentinel) {unsigned char *next=*(unsigned char**)node;free(node);node=next;}
    memcpy(test_co_sentinel,&test_co_sentinel,8);memcpy(test_co_sentinel+8,&test_co_sentinel,8);
    *(unsigned char**)(image+CO_QUEUE)=test_co_sentinel;*(uint64_t*)(image+CO_QUEUE+8)=0;
}
/* Appends one command at the tail, as 0x998430 and the batch splice do. */
static unsigned char *test_co_append(uint32_t kind,uint64_t source,unsigned event,double time) {
    unsigned char *node=calloc(1,0x40),*tail=*(unsigned char**)(test_co_sentinel+8),*e=test_co_events[event];assert(node);
    memcpy(node,&test_co_sentinel,8);memcpy(node+8,&tail,8);memcpy(tail,&node,8);memcpy(test_co_sentinel+8,&node,8);
    memcpy(node+CO_PAYLOAD,&kind,4);memcpy(node+CO_PAYLOAD+8,&source,8);memcpy(node+CO_PAYLOAD+0x10,&e,8);memcpy(node+CO_PAYLOAD+0x18,&time,8);
    ++*(uint64_t*)(image+CO_QUEUE+8);return node;
}
static unsigned test_co_event_at(unsigned char *node) {
    unsigned char *e=NULL;memcpy(&e,node+CO_PAYLOAD+0x10,8);return (unsigned)((e-&test_co_events[0][0])/0x50);
}
/* One pass: two commands before the release, then batches in the given order. */
static void test_co_pass(const unsigned *order,unsigned batches,int joined) {
    test_co_list_reset();
    test_co_append(3,0x7001,0,5.0);test_co_append(3,0x7002,1,5.0);
    co_before_release();
    for(unsigned b=0;b<batches;b++) switch(order[b]) {
        case 0: test_co_append(3,0x7001,3,6.0);test_co_append(3,0x7003,2,6.0);break;   /* A: event type 3, B */
        case 1: test_co_append(3,0x7001,2,6.0);test_co_append(3,0x9999,4,6.5);break;   /* A: type 2, local C */
        case 2: test_co_append(3,0x9999,4,6.5);test_co_append(5,0x7004,5,7.0);test_co_append(3,0x7004,6,6.0);break; /* local C tie, D mixed */
    }
    co_after_join(joined);
}
static void test_command_order(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x7001,0x92d05) && world_identity_record(0x7002,0x9404e) && world_identity_record(0x7003,0x6dc62) &&
           world_identity_record(0x7004,0xac854));
    for(unsigned i=0;i<16;i++) {void *vt=image+0x2a00000+i*8;memcpy(test_co_events[i],&vt,8);}
    InitializeCriticalSection((CRITICAL_SECTION*)(image+CO_LOCK));
    test_co_sentinel=calloc(1,0x40);assert(test_co_sentinel);
    memcpy(test_co_sentinel,&test_co_sentinel,8);memcpy(test_co_sentinel+8,&test_co_sentinel,8);
    stats.active=1;boot_mode=1;boot_phase=3;ui_thread=GetCurrentThreadId();
    /* Arming: serial pathing, before the first admitted frame, on the MoveWorker; pins. */
    co_armed=0;co_mode=CO_OFF;stats.issued=0;boot_phase=0;
    assert(!co_arm(CO_OBSERVE,0) && !co_arm(CO_OFF,1) && !co_arm(3,1));
    stats.issued=1;assert(!co_arm(CO_OBSERVE,1));stats.issued=0;ui_thread=GetCurrentThreadId()+1;assert(!co_arm(CO_OBSERVE,1));
    ui_thread=GetCurrentThreadId();assert(!co_arm(CO_OBSERVE,1) && !strcmp(co_stage,"pins") && !co_armed); /* zero image: unpinned */
    boot_phase=3;stats.issued=10;
    /* Not armed: nothing is read. */
    test_co_pass((const unsigned[]){0,1,2},3,1);assert(!co_frames && co_tick==UINT64_MAX);
    /* Observe: only the appended segment, grouped per source. */
    co_armed=1;co_mode=CO_OBSERVE;co_rows=0;
    test_co_pass((const unsigned[]){0,1,2},3,1);
    assert(co_frames==1 && co_frame.pre==2 && co_frame.appended==7 && co_frame.multi==3 && co_frame.unsorted==2 && co_frame.ties==1);
    assert(co_frame.local==2 && co_frame.other==1 && !co_frame.sorted && !co_frame.mixed && co_rows==3 && !co_failures);
    /* Queue order is unchanged in observe: A's events 3 then 2. */
    unsigned char *node=*(unsigned char**)test_co_sentinel;node=*(unsigned char**)*(unsigned char**)node; /* skip the two older commands */
    assert(test_co_event_at(node)==3);
    char row[512];assert(co_row_text(0,row,sizeof(row))>0);
    assert(!strcmp(row,"index=0 tick=10 source=saved:0000000000092d05 n=2 reordered=0 seq=02a00018@4018000000000000,02a00010@4018000000000000"));
    assert(co_row_text(1,row,sizeof(row))>0 && strstr(row,"source=saved:00000000000ac854 n=2"));
    assert(co_row_text(2,row,sizeof(row))>0 && strstr(row,"source=local:0000000000000000 n=2 reordered=0 seq=02a00020@401a000000000000,02a00020@401a000000000000"));
    uint64_t set_a=co_frame.set,seq_a=co_frame.seq,canon_a=co_frame.canon;
    /* Another completion order: same set and canonical hash, other sequence hash. */
    stats.issued=11;test_co_pass((const unsigned[]){1,0,2},3,1);
    assert(co_frame.set==set_a && co_frame.canon==canon_a && co_frame.seq!=seq_a && co_frame.unsorted==1 && co_frame.ties==1);
    /* Sort: the out-of-order source gets canonical order by payload exchange,
     * the tie and the mixed source keep theirs; both completion orders then agree. */
    co_mode=CO_SORT;stats.issued=12;test_co_pass((const unsigned[]){0,1,2},3,1);
    assert(co_frame.sorted==1 && co_frame.mixed==1 && co_frame.unsorted==2 && !strcmp(co_stage,"frame"));
    node=*(unsigned char**)*(unsigned char**)*(unsigned char**)test_co_sentinel;
    assert(test_co_event_at(node)==2); /* A's type-2 event now first, in the node of the type-3 one */
    uint64_t source;memcpy(&source,node+CO_PAYLOAD+8,8);assert(source==0x7001);
    assert(co_row_text(co_rows-3,row,sizeof(row))>0 && strstr(row," reordered=1 seq=02a00018@"));
    /* A mixed source out of order is left alone. */
    stats.issued=13;test_co_list_reset();co_before_release();
    test_co_append(3,0x7004,6,6.0);test_co_append(5,0x7004,5,5.0);test_co_append(3,0x7004,7,5.5);co_after_join(1);
    node=*(unsigned char**)test_co_sentinel;assert(co_frame.mixed==1 && !co_frame.sorted && test_co_event_at(node)==6);
    /* Sort without the proven join: observed only. */
    stats.issued=14;test_co_pass((const unsigned[]){0,1,2},3,0);
    assert(!co_frame.sorted && co_unjoined==1 && !strcmp(co_stage,"unjoined"));
    node=*(unsigned char**)*(unsigned char**)*(unsigned char**)test_co_sentinel;assert(test_co_event_at(node)==3);
    /* A swapped queue (flushed in between) is read whole; a size mismatch fails closed. */
    stats.issued=15;test_co_list_reset();co_before_release();
    unsigned char *other=calloc(1,0x40);assert(other);memcpy(other,&other,8);memcpy(other+8,&other,8);
    *(unsigned char**)(image+CO_QUEUE)=other;*(uint64_t*)(image+CO_QUEUE+8)=0;co_after_join(1);
    assert(co_swapped==1 && !co_frame.appended && !co_frame.pre);free(other);
    *(unsigned char**)(image+CO_QUEUE)=test_co_sentinel;
    test_co_list_reset();co_before_release();test_co_append(3,0x7001,2,6.0);--*(uint64_t*)(image+CO_QUEUE+8);co_after_join(1);
    assert(co_failures==1 && !strcmp(co_stage,"size") && !co_check());co_failures=0;
    /* No note before the join (the release did not run): nothing read. */
    uint64_t frames=co_frames;co_after_join(1);assert(co_frames==frames);
    /* Trace rows: every source, with kind, queue position and the portable keys
     * among the event's qwords +0x8..+0x40 (the source itself excluded). */
    co_mode=CO_OBSERVE;stats.issued=0;assert(co_trace_arm() && co_trace);stats.issued=16;
    uint64_t self=0x7001,target=0x7003,stranger=0x5555;
    memcpy(test_co_events[3]+8,&self,8);memcpy(test_co_events[2]+8,&stranger,8);memcpy(test_co_events[2]+16,&target,8);
    test_co_list_reset();co_before_release();test_co_append(3,0x7001,3,6.0);test_co_append(3,0x7002,2,6.0);co_after_join(1);
    assert(co_frame.rows==2 && !co_frame.multi);
    assert(co_row_text(co_rows-2,row,sizeof(row))>0 &&
           !strcmp(strstr(row,"tick=16"),"tick=16 source=saved:0000000000092d05 n=1 reordered=0 seq=02a00018@4018000000000000 detail=k3#0:0000000000000000/0000000000000000"));
    assert(co_row_text(co_rows-1,row,sizeof(row))>0 && strstr(row,"source=saved:000000000009404e n=1") &&
           strstr(row," detail=k3#1:000000000006dc62/0000000000000000"));
    memset(test_co_events[3]+8,0,8);memset(test_co_events[2]+8,0,16);
    /* The widest traced row fits the Lua row buffer. */
    co_row_t wide;memset(&wide,0xff,sizeof(wide));wide.klass=0;wide.n=CO_ROW_TYPES;wide.traced=1;co_ring[co_rows%CO_ROW_RING]=wide;
    LONG64 at=co_rows++;char big[1024];int w=co_row_text((uint64_t)at,big,sizeof(big));
    assert(w>0 && w+strlen("[X4LAN] NATIVE_GATE COMMAND_ORDER_ROW \n")<1024);
    co_trace=0;co_mode=CO_SORT;
    /* Status, also at the widest values. */
    char status[1024];int n=co_status(status,sizeof(status));
    assert(n>0 && strstr(status,"mode=sort armed=1 frame=16 ") && strstr(status," swapped=1 unjoined=1 "));
    memset(&co_frame,0xff,sizeof(co_frame));co_tick=stats.issued=UINT64_MAX-1;co_rows=co_failures=INT64_MAX;
    co_frames=co_total_multi=co_total_unsorted=co_total_ties=co_total_sorted=co_total_mixed=co_swapped=co_unjoined=co_overflow=UINT64_MAX;
    co_stage="block_changed";n=co_status(status,sizeof(status));
    assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE COMMAND_ORDER tick=18446744073709551615 \n")<1024);
    co_rows=CO_ROW_RING+3;assert(co_row_text(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    test_co_list_reset();free(test_co_sentinel);test_co_sentinel=NULL;
    *(unsigned char**)(image+CO_QUEUE)=NULL;*(uint64_t*)(image+CO_QUEUE+8)=0;
    DeleteCriticalSection((CRITICAL_SECTION*)(image+CO_LOCK));memset(image+CO_LOCK,0,sizeof(CRITICAL_SECTION));
    co_armed=0;co_trace=0;co_mode=CO_OFF;co_rows=co_failures=0;co_frames=co_total_multi=co_total_unsorted=co_total_ties=co_total_sorted=co_total_mixed=0;
    co_swapped=co_unjoined=co_overflow=0;co_tick=UINT64_MAX;memset(&co_frame,0,sizeof(co_frame));co_stage="idle";co_noted=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("command order segment/grouping/hashes/rows/sort/mixed/unjoined/swapped/size/status tests PASS\n");
}
/* With the X4 9.00 executable: the pinned post, append and splice code and the
 * command layout they write, tamper refusals and arming on the copy. */
static void test_command_order_native_image(const char *path) {
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
    assert(co_pins());
    int32_t rel;
    /* Post: lock, dirty flag, append on the queue, then {3, source, event, time}. */
    memcpy(&rel,image+0x99a7ad,4);assert(0x99a7b1+rel==CO_LOCK);
    memcpy(&rel,image+0x99a7cd,4);assert(0x99a7d1+rel==CO_QUEUE);
    memcpy(&rel,image+0x99a7d2,4);assert(image[0x99a7d1]==0xe8 && 0x99a7d6+rel==CO_APPEND);
    assert(!memcmp(image+0x99a7d6,"\xc7\x00\x03\x00\x00\x00\x48\x89\x58\x08\x48\x89\x78\x10\xf2\x0f\x11\x70\x18",19));
    /* Append: a 0x40-byte node, payload zeroed from +0x10, size +8, linked before the sentinel. */
    assert(!memcmp(image+0x998466,"\xb9\x40\x00\x00\x00",5) && !memcmp(image+0x998478,"\x48\x83\xc0\x10",4));
    assert(!memcmp(image+0x99848a,"\x48\xff\x43\x08\x48\x8b\x4f\x08\x48\x89\x3a\x48\x89\x4a\x08\x48\x89\x57\x08\x48\x89\x11",22));
    /* Splice: same lock, the queue and its size. */
    memcpy(&rel,image+CO_SPLICE+3,4);assert(CO_SPLICE+7+rel==CO_LOCK);
    memcpy(&rel,image+0xeaebd6,4);assert(0xeaebda+rel==CO_QUEUE);
    memcpy(&rel,image+0xeaebef,4);assert(0xeaebf3+rel==CO_QUEUE+8);
    const unsigned tampers[]={CO_POST,CO_POST+0xec,0x99a7d8,CO_APPEND,CO_APPEND+0x7a,0x99848d,CO_SPLICE,CO_SPLICE+0x8b,0xeaec28};
    for(unsigned i=0;i<sizeof(tampers)/sizeof(*tampers);i++) {image[tampers[i]]^=1;assert(!co_pins());image[tampers[i]]^=1;}
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    co_armed=0;co_mode=CO_OFF;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(!co_arm(CO_SORT,0) && co_arm(CO_SORT,1) && co_armed && co_mode==CO_SORT && co_arm(CO_SORT,1) && !co_arm(CO_OBSERVE,1));
    assert(co_check());image[CO_SPLICE+20]^=1;assert(!co_check() && !strcmp(co_stage,"block_changed"));image[CO_SPLICE+20]^=1;
    co_armed=0;co_mode=CO_OFF;co_failures=0;co_stage="idle";
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("command order native post/append/splice pins, command layout, tamper refusals and arming verified");
}
