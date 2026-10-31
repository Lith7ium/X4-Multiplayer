/* gate.sha256 (digest_hex): FIPS 180-2 vectors, block edges and the probe's lowercase hex form. */
static void test_digest(void) {
    char out[65];
    assert(digest_hex("",0,out) && !strcmp(out,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    assert(digest_hex("abc",3,out) && !strcmp(out,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const char *two="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    assert(digest_hex(two,strlen(two),out) && !strcmp(out,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    static char million[1000000];memset(million,'a',sizeof(million));
    assert(digest_hex(million,sizeof(million),out) && !strcmp(out,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    /* 55/56/64 bytes: the padding edges (one or two final blocks). */
    assert(digest_hex(million,55,out) && !strcmp(out,"9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
    assert(digest_hex(million,56,out) && !strcmp(out,"b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"));
    assert(digest_hex(million,64,out) && !strcmp(out,"ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
    for(unsigned i=0;i<64;i++) assert((out[i]>='0' && out[i]<='9') || (out[i]>='a' && out[i]<='f'));
    assert(out[64]==0 && digest_alg);
    assert(!digest_hex(million,(size_t)0x100000000ull,out));  /* beyond one BCryptHashData call: refused, not truncated */
    LARGE_INTEGER f,a,b;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);
    for(unsigned i=0;i<1000;i++) assert(digest_hex(million+i,16384,out));
    QueryPerformanceCounter(&b);
    printf("gate.sha256 vectors PASS; 16 KB (a play config tick's declared state) %.1f us per digest\n",
           (double)(b.QuadPart-a.QuadPart)*1e6/(double)f.QuadPart/1000.0);
}
