/* native_gate_build_rows: the three rows, the list read as native built it, a failed read loses a row, site CAS/restore, the pinned build. */
static uint64_t test_br_portable(uint64_t runtime) {return runtime==0x777?0:runtime+0x1000;}
/* The natives the entries forward to: every argument where it was, the result handed back. */
static uint64_t test_br_args[14];static unsigned test_br_called;
static uintptr_t test_br_find_native(void *a,void *b,void *c,void *d) {
    test_br_args[0]=(uint64_t)(uintptr_t)a;test_br_args[1]=(uint64_t)(uintptr_t)b;test_br_args[2]=(uint64_t)(uintptr_t)c;test_br_args[3]=(uint64_t)(uintptr_t)d;
    ++test_br_called;return 0x1234567890abcdefULL;
}
static void *test_br_add_native(void *a,void *b,void *c,void *d,uint64_t e,uint64_t f,uint64_t g,uint64_t h,uint64_t i,uint64_t j,uint64_t k,uint64_t l,uint64_t m,uint64_t n) {
    uint64_t all[14]={(uint64_t)(uintptr_t)a,(uint64_t)(uintptr_t)b,(uint64_t)(uintptr_t)c,(uint64_t)(uintptr_t)d,e,f,g,h,i,j,k,l,m,n};
    memcpy(test_br_args,all,sizeof(all));++test_br_called;
    *(uint64_t*)((unsigned char*)a+8)=0x77;*(uint64_t*)((unsigned char*)a+0x20)=0x10; /* the constructor fills the task */
    return a;
}
static unsigned char test_br_start_native(void *a,uint64_t b,unsigned char c) {
    test_br_args[0]=(uint64_t)(uintptr_t)a;test_br_args[1]=b;test_br_args[2]=c;++test_br_called;return (unsigned char)(c^1);
}
static void test_br_jump(unsigned char *at,void *to) {at[0]=0x48;at[1]=0xb8;memcpy(at+2,&to,8);at[10]=0xff;at[11]=0xe0;}
static void test_build_rows_forwarding(void) {
    unsigned char *prior=image;image=VirtualAlloc(NULL,0x4000000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(image);
    test_br_jump(image+br_sites[BR_SITE_FIND].target,(void*)test_br_find_native);
    test_br_jump(image+br_sites[BR_SITE_ADD].target,(void*)test_br_add_native);
    test_br_jump(image+br_sites[BR_SITE_START].target,(void*)test_br_start_native);
    assert(FlushInstructionCache(GetCurrentProcess(),image,0x4000000));
    uintptr_t none[3]={0,0,0};char out[2048];test_br_called=0;LONG64 rows=br_rows;
    /* The find: tail call, four arguments, its result. */
    assert(br_find_entry((void*)0x11,none,(void*)0x33,(void*)0x44)==0x1234567890abcdefULL && test_br_called==1);
    assert(test_br_args[0]==0x11 && test_br_args[1]==(uint64_t)(uintptr_t)none && test_br_args[2]==0x33 && test_br_args[3]==0x44);
    assert(br_rows==rows+1 && br_row((uint64_t)rows,out,sizeof(out))>0 && strstr(out,"kind=find") && strstr(out," n=0 "));
    /* The add: fourteen slots in order, the task back, and the row reads what the constructor left in it. */
    static unsigned char task[0x40];rows=br_rows;
    assert(br_add_entry(task,(void*)2,(void*)3,(void*)4,5,6,7,8,9,10,11,12,13,14)==task && test_br_called==2);
    assert(test_br_args[0]==(uint64_t)(uintptr_t)task);
    for(unsigned i=1;i<14;i++) assert(test_br_args[i]==i+1);
    assert(br_row((uint64_t)rows,out,sizeof(out))>0 && strstr(out,"kind=add task=77 ship=1010 "));
    /* The start: three arguments, its result decides and is recorded. */
    static unsigned char processor[0x20];*(uint64_t*)(processor+8)=0x20;rows=br_rows;
    assert(br_start_entry(processor,0x77,0)==1 && test_br_called==3 && test_br_args[1]==0x77 && !test_br_args[2]);
    assert(br_row((uint64_t)rows,out,sizeof(out))>0 && strstr(out,"kind=start task=77 processor=1020 ok=1"));
    assert(br_start_entry(processor,0x78,1)==0 && br_row((uint64_t)rows+1,out,sizeof(out))>0 && strstr(out,"task=78 processor=1020 ok=0"));
    /* Not watching: still forwarded, nothing recorded. */
    stats.active=0;rows=br_rows;assert(br_start_entry(processor,0x79,0)==1 && test_br_called==5 && br_rows==rows);stats.active=1;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
}
static void test_build_rows(void) {
    uint64_t saved_issued=stats.issued;uint32_t saved_active=stats.active;int saved_phase=boot_phase;
    br_row_t *saved_ring=br_ring;br_ring=VirtualAlloc(NULL,sizeof(br_row_t)*BR_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(br_ring);
    br_rows=0;memset(br_counts,0,sizeof(br_counts));br_unreadable=0;br_portable=test_br_portable;
    stats.active=1;boot_phase=3;stats.issued=16127;
    /* A list of three, one without a portable ID: read in native order. */
    static unsigned char ship_a[0x20],ship_b[0x20],ship_c[0x20];
    *(uint64_t*)(ship_a+8)=0x10;*(uint64_t*)(ship_b+8)=0x777;*(uint64_t*)(ship_c+8)=0x30;
    uintptr_t list[3]={(uintptr_t)ship_a,(uintptr_t)ship_b,(uintptr_t)ship_c};
    uintptr_t vector[3]={(uintptr_t)list,(uintptr_t)(list+3),(uintptr_t)(list+3)};
    br_note_find(vector);
    char out[2048];assert(br_row(0,out,sizeof(out))>0);
    assert(strstr(out,"index=0 tick=16127 kind=find scope=- n=3 "));
    assert(strstr(out,"ids=1010,4000000000000777,1030"));
    /* The same set in another order: the set hash holds, the order hash moves. That is the point of having both. */
    char first[2048];strcpy(first,out);
    uintptr_t swapped[3]={(uintptr_t)ship_c,(uintptr_t)ship_b,(uintptr_t)ship_a};
    uintptr_t vector2[3]={(uintptr_t)swapped,(uintptr_t)(swapped+3),(uintptr_t)(swapped+3)};
    br_note_find(vector2);assert(br_row(1,out,sizeof(out))>0);
    const char *s1=strstr(first,"set="),*s2=strstr(out,"set=");assert(s1 && s2 && !memcmp(s1,s2,20));
    const char *o1=strstr(first,"order="),*o2=strstr(out,"order=");assert(o1 && o2 && memcmp(o1,o2,22));
    /* An empty list is still a row. */
    uintptr_t empty[3]={0,0,0};br_note_find(empty);assert(br_row(2,out,sizeof(out))>0 && strstr(out," n=0 ") && strstr(out,"ids=-"));
    /* The task: its ID and what it builds. */
    static unsigned char task[0x40];*(uint64_t*)(task+8)=0x55;*(uint64_t*)(task+0x20)=0x10;
    br_note_add(task);assert(br_row(3,out,sizeof(out))>0 && strstr(out,"kind=add task=55 ship=1010 "));
    /* The start, either way. */
    static unsigned char processor[0x20];*(uint64_t*)(processor+8)=0x20;
    br_note_start(processor,0x55,1);assert(br_row(4,out,sizeof(out))>0 && strstr(out,"kind=start task=55 processor=1020 ok=1"));
    br_note_start(processor,0x56,0);assert(br_row(5,out,sizeof(out))>0 && strstr(out,"kind=start task=56 processor=1020 ok=0"));
    assert(br_counts[BR_KIND_FIND]==3 && br_counts[BR_KIND_ADD]==1 && br_counts[BR_KIND_START]==2);
    /* A failed read loses a row, nothing else. */
    uintptr_t bad[3]={(uintptr_t)list,(uintptr_t)list+1,(uintptr_t)list+1};LONG64 rows=br_rows;
    br_note_find(bad);br_note_find(NULL);br_note_add(NULL);
    assert(br_rows==rows && br_unreadable==3);
    /* A list entry that cannot be read keeps the row, with its slot empty. */
    uintptr_t holed[2]={(uintptr_t)ship_a,0};uintptr_t vector3[3]={(uintptr_t)holed,(uintptr_t)(holed+2),(uintptr_t)(holed+2)};
    br_note_find(vector3);assert(br_rows==rows+1 && br_unreadable==4);
    assert(br_row((uint64_t)rows,out,sizeof(out))>0 && strstr(out,"ids=1010,0"));
    /* Outside an admitted frame nothing is recorded. */
    stats.active=0;rows=br_rows;br_note_find(vector);br_note_add(task);br_note_start(processor,1,1);assert(br_rows==rows);stats.active=1;
    assert(!br_row(BR_RING*2,out,sizeof(out)));
    /* Past the ring: an old row says it is gone. */
    br_rows=BR_RING+10;assert(br_row(0,out,sizeof(out))>0 && strstr(out,"evicted=1"));
    br_rows=0;
    test_build_rows_forwarding();br_rows=0;
    /* Refused before it is armed on the zero image; a boot phase refuses too. */
    br_installed=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;{DWORD ui=ui_thread;ui_thread=GetCurrentThreadId();
    assert(!br_arm() && !strcmp(br_stage,"pins"));boot_phase=1;assert(!br_arm());boot_phase=0;ui_thread=ui;}
    /* Site CAS, ownership, restore. */
    unsigned char *saved_page=br_page;br_page=br_alloc_page();assert(br_page);DWORD old,ignored;
    for(unsigned i=0;i<BR_SITES;i++) {image[br_sites[i].site]=0xe8;int32_t d=br_rel(i,image+br_sites[i].target);memcpy(image+br_sites[i].site+1,&d,4);assert(br_state(i)==0);}
    assert(VirtualProtect(br_page,0x1000,PAGE_READWRITE,&old));for(unsigned i=0;i<BR_SITES;i++) br_write_stub(br_page+BR_STUB*i,i);assert(VirtualProtect(br_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    for(unsigned i=0;i<BR_SITES;i++) {assert(br_cas(i,br_rel(i,image+br_sites[i].target),br_rel(i,br_page+BR_STUB*i)) && br_state(i)==1);br_done|=1u<<i;}
    br_installed=1;assert(br_owned());
    image[br_sites[1].site+2]^=1;assert(br_state(1)<0 && !br_owned());image[br_sites[1].site+2]^=1;assert(br_owned());
    br_restore();assert(!br_installed && br_restored && !br_done);
    for(unsigned i=0;i<BR_SITES;i++) {assert(br_state(i)==0);memset(image+br_sites[i].site,0,5);}
    br_restored=0;VirtualFree(br_page,0,MEM_RELEASE);br_page=saved_page;
    assert(br_status(out,sizeof(out))>0 && strstr(out," find=") && strstr(out," owned=1 "));
    br_portable=NULL;br_stage="idle";memset(br_counts,0,sizeof(br_counts));br_unreadable=0;
    VirtualFree(br_ring,0,MEM_RELEASE);br_ring=saved_ring;
    stats.issued=saved_issued;stats.active=saved_active;boot_phase=saved_phase;
    printf("build rows find/add/start/lost reads/site tests PASS\n");
}
static void test_build_rows_native_image(const char *path) {
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
    unsigned char *saved_page=br_page;br_page=NULL;
    assert(br_pins());
    /* The find: the list is its second argument, a local vector (0xbbf001: lea rdx,[rsp+0x40]). */
    assert(!memcmp(image+0xbbf001,"\x48\x8d\x54\x24\x40",5));
    /* The add: the constructor's result is the task, and nothing but rax is read after it (0xb94815: mov rdi,rax). */
    assert(!memcmp(image+0xb94815,"\x48\x8b\xf8",3));
    /* The start: its result decides, processor in rcx, the task's ID in rdx (0xb9b46a: mov rdx,[r15+8]; mov rcx,r14). */
    assert(!memcmp(image+0xb9b46a,"\x49\x8b\x57\x08\x49\x8b\xce",7) && !memcmp(image+0xb9b476,"\x84\xc0",2));
    /* GetBuildTasks reads the fields the rows name: the ID at +8 and the built object at +0x20 of a task. */
    assert(!memcmp(image+0x1ca09a,"\x48\x8b\x46\x08",4) && !memcmp(image+0x1ca0b4,"\x48\x8b\x46\x20",4));
    image[br_sites[0].site-3]^=1;assert(!br_pins());image[br_sites[0].site-3]^=1;assert(br_pins());
    /* Armed for real on this copy: every site routed, the pins hold with the calls patched, restored at stop. */
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    br_row_t *saved_ring=br_ring;
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();br_ring=NULL;br_done=0;br_installed=0;
    assert(br_arm()==1 && br_installed && !strcmp(br_stage,"armed"));
    for(unsigned i=0;i<BR_SITES;i++) assert(br_state(i)==1 && image[br_sites[i].site]==0xe8);
    assert(br_pins() && br_owned() && br_arm()==1);
    br_restore();assert(!br_installed && br_restored && !br_done);
    for(unsigned i=0;i<BR_SITES;i++) assert(br_state(i)==0);
    assert(br_pins());
    br_restored=0;br_stage="idle";br_rows=0;
    assert(VirtualFree(br_page,0,MEM_RELEASE) && VirtualFree(br_ring,0,MEM_RELEASE));
    br_ring=saved_ring;br_page=saved_page;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("build rows sites, arguments and task fields verified");
}
