/* The probe samplers' primitives: hex_words is byte-identical to %08x per word, sample_read
 * copies like read_local and fails on unmapped memory instead of faulting. */
static void test_sample_primitives(void) {
    uint32_t words[80];char fast[1024],slow[1024];uint64_t x=0x9e3779b97f4a7c15ULL;
    for(unsigned round=0;round<200;round++) {
        unsigned count=1+round%80;
        for(unsigned i=0;i<count;i++) {x^=x<<13;x^=x>>7;x^=x<<17;words[i]=(uint32_t)(round%7?x:(round%2?0:0xffffffffu));}
        int n=0;for(unsigned i=0;i<count;i++) n+=snprintf(slow+n,sizeof(slow)-(size_t)n,"%08x",words[i]);
        assert(hex_words(fast,sizeof(fast),words,count)==n && !strcmp(fast,slow));
    }
    char tiny[16];assert(hex_words(tiny,sizeof(tiny),words,2)==0 && !tiny[0]);
    assert(hex_words(tiny,17,words,2)==16 && strlen(tiny)==16);
    unsigned char source[64],copy[64],other[64];for(unsigned i=0;i<64;i++) source[i]=(unsigned char)(i*7+1);
    assert(sample_read(source,copy,64) && !memcmp(source,copy,64));
    assert(read_local(source,other,64) && !memcmp(other,copy,64));
    assert(!sample_read(NULL,copy,8) && !read_local(NULL,copy,8));
    unsigned char *page=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);assert(page);
    assert(!sample_read(page,copy,8) && !read_local(page,copy,8));
    assert(!sample_read((void*)16,copy,8) && !read_local((void*)16,copy,8));
    VirtualFree(page,0,MEM_RELEASE);
    uint64_t before=read_local_calls;assert(read_local(source,copy,8) && read_local_calls==before+1);
    char timing[512];
    assert(frame_timing_status(timing,sizeof(timing))>0 && strstr(timing," pre_us=") && strstr(timing," created_us=") &&
           strstr(timing," join_us=") && strstr(timing," reads="));
    LARGE_INTEGER f,a,b,c;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);
    for(unsigned i=0;i<100000;i++) assert(read_local(source+(i&31),copy,8));
    QueryPerformanceCounter(&b);
    for(unsigned i=0;i<100000;i++) assert(sample_read(source+(i&31),copy,8));
    QueryPerformanceCounter(&c);
    printf("probe sampler primitives PASS: hex_words = %%08x; 8-byte read %.3f us (ReadProcessMemory) vs %.3f us (guarded copy)\n",
           (double)(b.QuadPart-a.QuadPart)*1e6/(double)f.QuadPart/1e5,(double)(c.QuadPart-b.QuadPart)*1e6/(double)f.QuadPart/1e5);
}
