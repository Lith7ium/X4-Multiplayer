/* native_gate_exact_reciprocal: the trampoline recipes executed on synthetic functions
 * (every register pairing incl. xmm8-15, D == S, adjacent rcp(rsqrt) one by one and as a
 * chain to sqrtss, a RIP-relative neighbour; exact quotient or square root, untouched
 * upper lanes and source); the original-byte view
 * and arm refusals on the synthetic image; with X4.exe: all window pins, trampolines,
 * install with frozen threads, the view for other modules' pins, check and restore. */
static unsigned test_xrcp_movups(unsigned char *p,int store,unsigned reg,unsigned base_rm,int disp8) {
    unsigned n=0;if(reg>=8) p[n++]=0x44;
    p[n++]=0x0f;p[n++]=store?0x11:0x10;
    if(disp8) {p[n++]=(unsigned char)(0x40|(reg&7)<<3|base_rm);p[n++]=(unsigned char)disp8;}
    else p[n++]=(unsigned char)((reg&7)<<3|base_rm);
    return n;
}
static unsigned test_xrcp_approx(unsigned char *p,unsigned kind,unsigned d,unsigned s) {
    unsigned n=0;p[n++]=0xf3;if(d>=8 || s>=8) p[n++]=(unsigned char)(0x40|(d>=8?4:0)|(s>=8?1:0));
    p[n++]=0x0f;p[n++]=kind==XRCP_RCPSS || kind==XRCP_CHAIN_END?0x53:0x52;p[n++]=(unsigned char)(0xc0|(d&7)<<3|(s&7));return n;
}
typedef void (*test_xrcp_fn)(const float *in,float *out);
/* sub rsp,0xa8 and movdqu [rsp+16*i],xmm(6+i) (or the reverse and add rsp,0xa8): xmm6-15 are
 * callee-saved in the MS x64 ABI and the synthetic functions use them. */
static unsigned test_xrcp_frame(unsigned char *p,int enter) {
    unsigned n=0;
    if(enter) {p[n++]=0x48;p[n++]=0x81;p[n++]=0xec;p[n++]=0xa8;p[n++]=0;p[n++]=0;p[n++]=0;}
    for(unsigned i=0;i<10;i++) {
        unsigned reg=6+i;p[n++]=0xf3;if(reg>=8) p[n++]=0x44;
        p[n++]=0x0f;p[n++]=enter?0x7f:0x6f;p[n++]=(unsigned char)(0x44|(reg&7)<<3);p[n++]=0x24;p[n++]=(unsigned char)(16*i);
    }
    if(!enter) {p[n++]=0x48;p[n++]=0x81;p[n++]=0xc4;p[n++]=0xa8;p[n++]=0;p[n++]=0;p[n++]=0;}
    return n;
}
/* frame; movups d,[rcx]; movups s,[rcx+16]; WINDOW{approx...; lea rax,[rip+0x1234]}; movups [rdx],d;
 * movups [rdx+16],s; mov [rdx+32],rax; frame; ret */
static void test_xrcp_build(unsigned char *code,const unsigned *kinds,unsigned count,unsigned d,unsigned s,
                            xrcp_window *w,unsigned *window_at) {
    unsigned k=0;memset(w,0,sizeof(*w));
    k+=test_xrcp_frame(code+k,1);
    k+=test_xrcp_movups(code+k,0,d,1,0);if(s!=d) k+=test_xrcp_movups(code+k,0,s,1,16);
    *window_at=k;
    for(unsigned i=0;i<count;i++) {
        unsigned n=test_xrcp_approx(code+k,kinds[i],d,i?d:s);
        w->insn[w->count++]=(xrcp_insn){(unsigned char)(k-*window_at),(unsigned char)n,(unsigned char)kinds[i],0};k+=n;
    }
    static const unsigned char lea[7]={0x48,0x8d,0x05,0x34,0x12,0,0};
    memcpy(code+k,lea,7);w->insn[w->count++]=(xrcp_insn){(unsigned char)(k-*window_at),7,XRCP_COPY,3};k+=7;
    w->length=(unsigned char)(k-*window_at);w->rva=*window_at;
    k+=test_xrcp_movups(code+k,1,d,2,0);if(s!=d) k+=test_xrcp_movups(code+k,1,s,2,16);
    code[k++]=0x48;code[k++]=0x89;code[k++]=0x42;code[k++]=0x20; /* mov [rdx+32], rax */
    k+=test_xrcp_frame(code+k,0);
    code[k++]=0xc3;
}
static float test_xrcp_exact(unsigned kind,float x) {
    volatile float v=x;float r;
    if(kind==XRCP_RCPSS) {volatile float one=1.0f;r=one/v;}
    else if(kind==XRCP_SQRT) r=sqrtf(v);
    else {volatile float t=sqrtf(v),one=1.0f;r=one/t;}
    return r;
}
static void test_xrcp_recipes(void) {
    unsigned char *mem=VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(mem);
    unsigned char *code=mem,*tramp=mem+0x4000,*constants=mem+0x8000;memcpy(constants,xrcp_constants,sizeof(xrcp_constants));
    static const unsigned pairs[][2]={{3,3},{3,5},{9,9},{9,2},{2,12},{14,14},{0,15},{8,1}};
    static const float inputs[]={2.0f,0.0f,-0.0f,1e-40f,-4.0f,INFINITY,3.0e38f,0.7f,1e-20f,-1e30f,25.0f};
    static const unsigned shapes[][2]={{XRCP_RCPSS,0},{XRCP_RSQRTSS,0},{XRCP_RSQRTSS,XRCP_RCPSS},{XRCP_SQRT,XRCP_CHAIN_END}};
    unsigned cases=0;
    for(unsigned sh=0;sh<4;sh++) for(unsigned pr=0;pr<sizeof(pairs)/sizeof(*pairs);pr++) {
        unsigned kinds[2]={shapes[sh][0],shapes[sh][1]},count=shapes[sh][1]?2:1,d=pairs[pr][0],s=pairs[pr][1],at;
        xrcp_window w;memset(code,0xcc,0x200);test_xrcp_build(code,kinds,count,d,s,&w,&at);
        unsigned char original[XRCP_MAX_WINDOW];memcpy(original,code+at,w.length);
        unsigned size;assert(xrcp_emit_window(tramp,&w,original,code+at,constants,XRCP_DROP,&size) && size<=XRCP_SLOT);
        for(unsigned i=0;i<sizeof(inputs)/sizeof(*inputs);i++) {
            float in[8]={inputs[i],11.0f,-12.5f,13.25f,inputs[i],21.0f,22.0f,23.0f},native[12],exact[12];
            if(s!=d) {in[0]=-7.0f;}
            memset(native,0,sizeof(native));memset(exact,0,sizeof(exact));
            memcpy(code+at,original,w.length);FlushInstructionCache(GetCurrentProcess(),code,0x200);
            ((test_xrcp_fn)(void*)code)(in,native);
            int32_t rel=(int32_t)(tramp-(code+at+5));code[at]=0xe9;memcpy(code+at+1,&rel,4);memset(code+at+5,0xcc,w.length-5);
            FlushInstructionCache(GetCurrentProcess(),code,0x200);
            ((test_xrcp_fn)(void*)code)(in,exact);
            float x=s!=d?in[4]:in[0],want=test_xrcp_exact(kinds[0],x);
            if(count==2 && kinds[1]==XRCP_RCPSS) want=test_xrcp_exact(XRCP_RCPSS,want); /* a chain end adds nothing */
            assert(!memcmp(&exact[0],&want,4) || (want!=want && exact[0]!=exact[0]));
            assert(!memcmp(&exact[1],&in[1],12) && !memcmp(&native[1],&in[1],12)); /* lanes 1-3 of d */
            if(s!=d) assert(!memcmp(&exact[4],&in[4],16) && !memcmp(&native[4],&in[4],16)); /* s untouched */
            uint64_t rax_native,rax_exact;memcpy(&rax_native,&native[8],8);memcpy(&rax_exact,&exact[8],8);
            assert(rax_native==rax_exact && rax_exact==(uint64_t)(uintptr_t)(code+at+w.insn[count].offset+7+0x1234));
            ++cases;
        }
    }
    /* The recipe for rsqrtss xmm9, xmm9: lea, movss, sqrtss, movss, andps, orps, divss, lea. */
    xrcp_window w;unsigned at;const unsigned kinds[1]={XRCP_RSQRTSS};test_xrcp_build(code,kinds,1,9,9,&w,&at);
    unsigned char original[XRCP_MAX_WINDOW];memcpy(original,code+at,w.length);unsigned size;
    assert(xrcp_emit_window(tramp,&w,original,code+at,constants,XRCP_DROP,&size) && size==5+6+6+6+8+8+6+5+7+5);
    static const unsigned char head[]={0x48,0x8d,0x64,0x24,0xf0,0xf3,0x44,0x0f,0x11,0x0c,0x24,0xf3,0x44,0x0f,0x51,0x0c,0x24,
                                       0xf3,0x44,0x0f,0x11,0x0c,0x24,0x44,0x0f,0x54,0x0d};
    assert(!memcmp(tramp,head,sizeof(head)) && !memcmp(tramp+5+6+6+6+8,"\x44\x0f\x56\x0d",4));
    assert(!memcmp(tramp+5+6+6+6+8+8,"\xf3\x44\x0f\x5e\x0c\x24\x48\x8d\x64\x24\x10",11));
    /* The chain rsqrtss xmm9, xmm2; rcpss xmm9, xmm9: one sqrtss xmm9, xmm2 (f3 44 0f 51 ca), then the copy. */
    const unsigned chain[2]={XRCP_SQRT,XRCP_CHAIN_END};test_xrcp_build(code,chain,2,9,2,&w,&at);
    memcpy(original,code+at,w.length);
    assert(xrcp_emit_window(tramp,&w,original,code+at,constants,XRCP_DROP,&size) && size==5+7+5);
    assert(!memcmp(tramp,"\xf3\x44\x0f\x51\xca\x48\x8d\x05",8));
    /* a chain end must be rcpss d,d */
    w.insn[1].kind=XRCP_CHAIN_END;original[w.insn[1].offset+w.insn[1].length-1]^=0x01;
    assert(!xrcp_emit_window(tramp,&w,original,code+at,constants,XRCP_DROP,&size));
    /* Decoder refusals: wrong opcode for the kind, memory form, REX.W. */
    unsigned dd,ss;unsigned char bad[5]={0xf3,0x0f,0x53,0xc1,0};
    assert(xrcp_decode(bad,4,XRCP_RCPSS,&dd,&ss) && dd==0 && ss==1 && !xrcp_decode(bad,4,XRCP_RSQRTSS,&dd,&ss));
    assert(xrcp_decode(bad,4,XRCP_CHAIN_END,&dd,&ss) && !xrcp_decode(bad,4,XRCP_SQRT,&dd,&ss));
    bad[3]=0x01;assert(!xrcp_decode(bad,4,XRCP_RCPSS,&dd,&ss));
    unsigned char wide[5]={0xf3,0x48,0x0f,0x53,0xc1};assert(!xrcp_decode(wide,5,XRCP_RCPSS,&dd,&ss));
    VirtualFree(mem,0,MEM_RELEASE);
    printf("exact reciprocal recipes: %u executed cases (rcpss, rsqrtss, rcp(rsqrt) one by one and as sqrtss; xmm0-15, d==s and d!=s) PASS\n",cases);
}
static void test_exact_reciprocal(void) {
    test_xrcp_recipes();
    uint32_t saved_active=stats.active;uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;int saved_boot=boot_mode,saved_phase=boot_phase;
    /* The original-byte view: patched windows read as their originals, nothing else changes. */
    const xrcp_window *w=&xrcp_windows[5];unsigned char saved[XRCP_MAX_WINDOW],bytes[XRCP_MAX_WINDOW+2];memcpy(saved,xrcp_original[5],sizeof(saved));
    memcpy(bytes,image+w->rva-1,w->length+2);
    for(unsigned k=0;k<w->length;k++) {xrcp_original[5][k]=(unsigned char)(0x10+k);image[w->rva+k]=0xcc;}
    image[w->rva-1]=0x77;image[w->rva+w->length]=0x88;
    unsigned char copy[64];InterlockedExchange(&xrcp_any,1);
    assert(read_local(image+w->rva-1,copy,w->length+2) && copy[0]==0x77 && copy[1]==0xcc && copy[w->length+1]==0x88); /* not patched */
    xrcp_patched[5]=1;
    assert(read_local(image+w->rva-1,copy,w->length+2) && copy[0]==0x77 && copy[w->length+1]==0x88);
    for(unsigned k=0;k<w->length;k++) assert(copy[1+k]==0x10+k);
    assert(read_local(image+w->rva+2,copy,3) && copy[0]==0x12 && copy[2]==0x14);
    assert(sample_read(image+w->rva,copy,1) && copy[0]==0x10);
    /* hash_ok hashes the view: the digest of the original bytes passes over the patched ones. */
    unsigned char want[64];memcpy(want,image+w->rva-1,w->length+2);for(unsigned k=0;k<w->length;k++) want[1+k]=(unsigned char)(0x10+k);
    char hex[65];assert(digest_hex(want,w->length+2,hex) && hash_ok(image+w->rva-1,w->length+2,hex));
    xrcp_patched[5]=0;assert(!hash_ok(image+w->rva-1,w->length+2,hex));
    InterlockedExchange(&xrcp_any,0);memcpy(image+w->rva-1,bytes,w->length+2);memcpy(xrcp_original[5],saved,sizeof(saved));
    /* Arm refusals: state, then the pins of the synthetic image. */
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();
    assert(!xrcp_arm() && !strcmp(xrcp_stage,"pins") && !xrcp_installed);
    stats.issued=1;assert(!xrcp_arm() && !strcmp(xrcp_stage,"state"));stats.issued=0;
    boot_phase=2;assert(!xrcp_arm());boot_phase=0;ui_thread=GetCurrentThreadId()+1;assert(!xrcp_arm());
    char out[320];assert(xrcp_status(out,sizeof(out))>0 && strstr(out,"installed=0 windows=819 rewritten=1262 "));
    xrcp_stage="idle";
    stats.active=saved_active;stats.issued=saved_issued;ui_thread=saved_ui;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("exact reciprocal original-byte view/hash_ok/arm refusals PASS\n");
}
static void test_exact_reciprocal_native_image(const char *path) {
    unsigned char *prior=image;image=test_pmath_map(path);
    assert(xrcp_pins());
    /* Every replaced instruction decodes as its kind with register operands. */
    unsigned rewritten=0,copies=0,rip=0;
    for(unsigned i=0;i<XRCP_WINDOWS;i++) for(unsigned k=0;k<xrcp_windows[i].count;k++) {
        const xrcp_insn *x=&xrcp_windows[i].insn[k];unsigned d,s;
        if(x->kind) {assert(xrcp_decode(xrcp_original[i]+x->offset,x->length,x->kind,&d,&s));++rewritten;}
        else {++copies;rip+=x->rip!=0;}
    }
    assert(rewritten==XRCP_REWRITTEN);
    /* A gate pin over a window: its SHA-256 of the original bytes, before and while installed. */
    const xrcp_window *w=&xrcp_windows[XRCP_WINDOWS/2];
    unsigned char *range=image+w->rva-64;char before[65];assert(digest_hex(range,256,before) && hash_ok(range,256,before));
    assert(xrcp_build());
    for(unsigned i=0;i<XRCP_WINDOWS;i++) assert(xrcp_state(i)==0);
    /* tools/dev/check_exact_reciprocal.py decodes every trampoline from this export. */
    const char *export=getenv("X4LAN_XRCP_EXPORT");
    if(export) {
        FILE *f=fopen(export,"wb");assert(f);uint64_t bases[2]={(uint64_t)(uintptr_t)image,(uint64_t)(uintptr_t)xrcp_region};
        uint32_t head[2]={XRCP_WINDOWS,(uint32_t)xrcp_region_size};
        assert(fwrite("XRCP1",1,5,f)==5 && fwrite(bases,8,2,f)==2 && fwrite(head,4,2,f)==2);
        for(unsigned i=0;i<XRCP_WINDOWS;i++) {
            uint32_t row[3]={xrcp_windows[i].rva,(uint32_t)(XRCP_CONSTANTS+i*XRCP_SLOT),xrcp_sizes[i]};assert(fwrite(row,4,3,f)==3);
        }
        assert(fwrite(xrcp_region,1,xrcp_region_size,f)==xrcp_region_size && !fclose(f));
    }
    assert(xrcp_switch(1)==1);xrcp_installed=1;xrcp_failures=0;
    for(unsigned i=0;i<XRCP_WINDOWS;i++) assert(xrcp_state(i)==1 && xrcp_patched[i]);
    unsigned char raw[XRCP_MAX_WINDOW];SIZE_T got=0;
    assert(xrcp_owned() && xrcp_check() && ReadProcessMemory(GetCurrentProcess(),image+w->rva,raw,w->length,&got) && raw[0]==0xe9);
    char during[65];assert(digest_hex(range,256,during) && strcmp(before,during) && hash_ok(range,256,before));
    unsigned char viewed[256];assert(read_local(range,viewed,256) && digest_hex(viewed,256,during) && !strcmp(before,during));
    xrcp_restore();assert(!xrcp_installed && xrcp_restored && !InterlockedCompareExchange(&xrcp_any,0,0));
    for(unsigned i=0;i<XRCP_WINDOWS;i++) assert(xrcp_state(i)==0 && !xrcp_patched[i]);
    assert(xrcp_pins() && hash_ok(range,256,before));
    VirtualFree(xrcp_region,0,MEM_RELEASE);xrcp_region=NULL;xrcp_restored=0;xrcp_stage="idle";xrcp_attempts=xrcp_busy=0;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    printf("exact reciprocal: %u windows pinned, %u instructions decoded, %u copied (%u RIP-relative), installed, viewed and restored\n",
           (unsigned)XRCP_WINDOWS,rewritten,copies,rip);
}
