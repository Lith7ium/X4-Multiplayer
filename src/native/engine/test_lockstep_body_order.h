/* native_gate_body_order: two synthetic clients queue the same objects in
 * different pointer orders; 'sort' runs every drop and creation after the
 * pass in one portable order (saved, created, this frame's MoveWorker
 * creations, others) with the references held and released as the set does,
 * 'observe' keeps the native calls; rows, refusals, call-site swaps and the
 * pinned build. Owned memory only; no X4 code runs. */
typedef struct {__attribute__((aligned(16))) unsigned char data[0x80];const char *name;} test_bo_object;
static char test_bo_log[512];
static unsigned test_bo_destroyed;
static void test_bo_append(const char *op,void *object) {
    size_t n=strlen(test_bo_log);snprintf(test_bo_log+n,sizeof(test_bo_log)-n,"%s%s:%s",n?",":"",op,((test_bo_object*)object)->name);
}
static void test_bo_drop(void *object) {test_bo_append("drop",object);}
static void test_bo_create(void *object) {test_bo_append("create",object);}
static void test_bo_destroy(void *base,unsigned flags) {assert(flags==3);(void)base;++test_bo_destroyed;}
static void (*const test_bo_vtable[1])(void *,unsigned)={test_bo_destroy};
static void test_bo_object_init(test_bo_object *o,const char *name,uint64_t runtime,LONG references) {
    memset(o->data,0,sizeof(o->data));o->name=name;memcpy(o->data+8,&runtime,8);
    const void *vtable=test_bo_vtable;memcpy(o->data+0x38,&vtable,8);
    memcpy(o->data+0x40,&references,4);LONG state=2;memcpy(o->data+0x44,&state,4);
}
/* One pass of a client: the walk in its native (pointer) order, the set's clear, the flush. */
typedef struct {test_bo_object *object;int drop,create;} test_bo_queued;
static void test_bo_pass(const test_bo_queued *queued,unsigned count) {
    test_bo_log[0]=0;border_count=0;border_in_pass=1;
    for(unsigned i=0;i<count;i++) {
        if(queued[i].drop) border_note(test_bo_drop,queued[i].object->data,0);
        if(queued[i].create) border_note(test_bo_create,queued[i].object->data,1);
    }
    border_in_pass=0;
    for(unsigned i=0;i<count;i++) border_release(queued[i].object->data); /* the set drops its reference */
    border_flush_with(test_bo_drop,test_bo_create);
}
static void test_body_order(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_ids=world_identity_installed;
    test_pc_begin();assert(created_allocate(0));world_portable_created=1;
    stats.active=1;boot_phase=3;ui_thread=GetCurrentThreadId();stats.issued=15;
    border_ring=VirtualAlloc(NULL,sizeof(*border_ring)*BORDER_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(border_ring);
    InitializeCriticalSection((CRITICAL_SECTION *)(image+BORDER_LOCK));
    assert(world_identity_record(0x100,0x30) && world_identity_record(0x101,0x10));
    test_pc_create(0x200,TEST_PC_CREATED_A);test_pc_create(0x201,TEST_PC_CREATED_B);
    static test_bo_object s1,s2,c1,c2,f1,f2,u;
    const char *expected="drop:s2,create:s2,create:s1,create:c2,create:c1,create:f2,create:f1,drop:u";
    /* Client A: this frame's MoveWorker created f2 before f1 (other notes between). */
    created_pending_count=4;
    created_pending[0]=(created_note_t){0x999,CREATED_MAIN,0};created_pending[1]=(created_note_t){0x301,CREATED_MOVEWORKER,0};
    created_pending[2]=(created_note_t){0x555,CREATED_MOVEWORKER,0};created_pending[3]=(created_note_t){0x300,CREATED_MOVEWORKER,0};
    test_bo_object_init(&s1,"s1",0x100,2);test_bo_object_init(&s2,"s2",0x101,1);test_bo_object_init(&c1,"c1",0x200,1);test_bo_object_init(&c2,"c2",0x201,1);
    test_bo_object_init(&f1,"f1",0x300,1);test_bo_object_init(&f2,"f2",0x301,1);test_bo_object_init(&u,"u",0x400,1);
    border_mode=BORDER_SORT;border_installed=1;border_failures=0;border_rows=0;border_moved=border_entries=border_unordered=border_reenqueued=0;
    const test_bo_queued a[7]={{&u,1,0},{&f1,0,1},{&c1,0,1},{&s1,0,1},{&f2,0,1},{&c2,0,1},{&s2,1,1}};
    test_bo_destroyed=0;test_bo_pass(a,7);
    assert(!strcmp(test_bo_log,expected) && !border_failures && border_count==7 && border_entries==7 && border_unordered==1);
    assert(test_bo_destroyed==6 && *(LONG*)(s1.data+0x40)==1 && *(LONG*)(s1.data+0x44)==2 && *(LONG*)(u.data+0x44)==3);
    /* Client B: other addresses, the reverse pointer order, other notes around f2 and f1. */
    created_pending_count=5;
    created_pending[0]=(created_note_t){0x301,CREATED_MOVEWORKER,0};created_pending[1]=(created_note_t){0x777,CREATED_MOVEWORKER,0};
    created_pending[2]=(created_note_t){0x300,CREATED_OTHER,0};created_pending[3]=(created_note_t){0x888,CREATED_MAIN,0};
    created_pending[4]=(created_note_t){0x300,CREATED_MOVEWORKER,0};
    static test_bo_object s1b,s2b,c1b,c2b,f1b,f2b,ub;
    test_bo_object_init(&s1b,"s1",0x100,2);test_bo_object_init(&s2b,"s2",0x101,1);test_bo_object_init(&c1b,"c1",0x200,1);test_bo_object_init(&c2b,"c2",0x201,1);
    test_bo_object_init(&f1b,"f1",0x300,1);test_bo_object_init(&f2b,"f2",0x301,1);test_bo_object_init(&ub,"u",0x400,1);
    const test_bo_queued b[7]={{&s2b,1,1},{&c2b,0,1},{&f2b,0,1},{&s1b,0,1},{&c1b,0,1},{&f1b,0,1},{&ub,1,0}};
    test_bo_pass(b,7);assert(!strcmp(test_bo_log,expected));
    /* Rows: positions, portable places, kinds; frame-created keys once assigned. */
    char text[256];assert(border_rows==14 && !border_row_text(7,0,text,sizeof(text)) && border_row_text(7,1,text,sizeof(text)));
    assert(strstr(text," mode=sort count=7 pos=0 order=0 kind=saved klass=0 key=0000000000000010 drop=1 create=1"));
    stats.issued=16;test_pc_create(0x301,TEST_PC_CREATED_A^0x40);
    assert(border_row_text(9,0,text,sizeof(text)) && strstr(text," pos=2 order=4 kind=frame klass=1 key="));
    assert(border_row_text(13,0,text,sizeof(text)) && strstr(text," pos=6 order=6 kind=unordered klass=2 key=0000000000000000 drop=1 create=0"));
    /* A call outside the pass or on another thread runs at once; nothing is recorded. */
    test_bo_log[0]=0;border_count=0;border_note(test_bo_create,c1.data,1);assert(!strcmp(test_bo_log,"create:c1") && !border_count);
    border_in_pass=1;ui_thread=GetCurrentThreadId()+1;border_note(test_bo_drop,c1.data,0);ui_thread=GetCurrentThreadId();border_in_pass=0;
    assert(!strcmp(test_bo_log,"create:c1,drop:c1") && !border_count);
    /* Observe: native calls in pointer order, the same rows, no references taken. */
    border_mode=BORDER_OBSERVE;test_bo_log[0]=0;border_count=0;border_in_pass=1;LONG before=*(LONG*)(s1.data+0x40);
    border_note(test_bo_create,c1.data,1);border_note(test_bo_create,s1.data,1);border_in_pass=0;
    border_flush_with(test_bo_drop,test_bo_create);
    assert(!strcmp(test_bo_log,"create:c1,create:s1") && *(LONG*)(s1.data+0x40)==before && border_list[0].order==1 && border_list[1].order==0);
    assert(border_row_text(15,1,text,sizeof(text)) && strstr(text," mode=observe count=2 pos=1 order=0 kind=saved "));
    /* A queue left after the flush is counted; a full record list fails closed and runs natively. */
    border_mode=BORDER_SORT;*(uint64_t*)(image+BORDER_SET_COUNT)=1;test_bo_object_init(&s1,"s1",0x100,2);
    const test_bo_queued one[1]={{&s1,0,1}};test_bo_pass(one,1);assert(border_reenqueued==1);*(uint64_t*)(image+BORDER_SET_COUNT)=0;
    test_bo_log[0]=0;border_count=BORDER_MAX;border_list[BORDER_MAX-1].object=NULL;border_in_pass=1;
    border_note(test_bo_create,c2.data,1);border_in_pass=0;
    assert(!strcmp(test_bo_log,"create:c2") && border_overflow==1 && border_failures==1 && !border_check() && !strcmp(border_stage,"full"));
    border_failures=0;border_overflow=0;border_count=0;
    char status[512];assert(border_status(status,sizeof(status)) && strstr(status,"mode=sort installed=1 ") && strstr(status," reenqueued=1 "));
    border_installed=0;
    /* Arming refusals on the zero image. */
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;world_identity_installed=1;
    created_armed=0;assert(!border_arm(BORDER_SORT) && !strcmp(border_stage,"needs"));created_armed=1;
    assert(!border_arm(BORDER_SORT) && !strcmp(border_stage,"site") && !border_installed);assert(!border_arm(BORDER_OFF));
    stats.issued=1;assert(!border_arm(BORDER_SORT));stats.issued=0;boot_phase=2;assert(!border_arm(BORDER_SORT));boot_phase=0;
    /* Call sites: swap, ownership, restore (the plain image's original calls). */
    for(unsigned i=0;i<BORDER_CALLS;i++) {
        unsigned char call[5]={0xe8};int32_t rel=(int32_t)(border_calls[i].target-(border_calls[i].rva+5));memcpy(call+1,&rel,4);
        memcpy(image+border_calls[i].rva,call,5);
    }
    if(!border_stubs) border_stubs=border_alloc_near(BORDER_STUB_SIZE);
    assert(border_stubs && border_write_stubs() && border_stubs_owned());
    for(unsigned i=0;i<BORDER_CALLS;i++) {
        assert(border_call_state(i)==0 && border_cas(i,border_rel_to(i,image+border_calls[i].target),border_rel_to(i,border_stub(i))));
        border_done|=1u<<i;assert(border_call_state(i)==1);
    }
    border_installed=1;assert(border_owned());
    image[border_calls[1].rva+2]^=1;assert(!border_owned());image[border_calls[1].rva+2]^=1;assert(border_owned());
    border_restore();assert(!border_installed && border_restored && !border_done);
    for(unsigned i=0;i<BORDER_CALLS;i++) {assert(border_call_state(i)==0);memset(image+border_calls[i].rva,0,5);}
    border_restored=0;border_stage="idle";border_mode=BORDER_OFF;
    DeleteCriticalSection((CRITICAL_SECTION *)(image+BORDER_LOCK));memset(image+BORDER_LOCK,0,sizeof(CRITICAL_SECTION));
    VirtualFree(border_ring,0,MEM_RELEASE);border_ring=NULL;border_rows=0;border_entries=border_moved=border_unordered=border_reenqueued=0;
    test_pc_end();world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("body order sort/observe/rows/references/swap tests PASS\n");
}
static void test_body_order_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    assert(border_pins_ok());
    for(unsigned i=0;i<BORDER_CALLS;i++) assert(border_call_state(i)==0);
    /* The set is keyed by the raw pointer: insert compares it with node+0x20. */
    assert(!memcmp(image+0x86a3c0,"\x48\x3b\x43\x20",4));
    /* The pass walks the set from its first node [0x2f5dff0] under TryEnterCriticalSection(0x3949b98). */
    assert(!memcmp(image+0x83ece4,"\x48\x8b\x1d",3) && 0x83eceb+*(int32_t*)(image+0x83ece7)==0x2f5dff0);
    assert(!memcmp(image+0x83ec33,"\x48\x8d\x0d",3) && 0x83ec3a+*(int32_t*)(image+0x83ec36)==BORDER_LOCK);
    assert(!memcmp(image+0x83ec3a,"\xff\x15",2) && 0x83ec40+*(int32_t*)(image+0x83ec3c)==0x1bc14c8);
    /* The enqueue's reference (state 1 -> 2, count +1) and the set's release (count -1, state 2 -> 3, slot 0 of +0x38). */
    assert(!memcmp(image+0x51c7f2,"\xbf\x02\x00\x00\x00\xb8\x01\x00\x00\x00\xf0\x0f\xb1\x7b\x44\xb8\x01\x00\x00\x00\xf0\x0f\xc1\x43\x40",25));
    assert(!memcmp(image+0x83ee22,"\x48\x83\xc1\x38\xb8\xff\xff\xff\xff\xf0\x0f\xc1\x41\x08",14));
    image[0x86a3c3]^=1;assert(!border_pins_ok());image[0x86a3c3]^=1;
    image[border_calls[2].rva+1]^=1;assert(border_call_state(2)<0 && !border_pins_ok());image[border_calls[2].rva+1]^=1;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("body order native pins, pointer-keyed set, pass lock and references verified");
}
