/* White-box tests of the Linux gate core (gate_linux.c), no X4 and no network
 * beyond loopback: pins, refusals and stages on a fake image, the code patches
 * and their restoration, the thunk, the frame gate state machine against a
 * scripted relay thread, and the frame seed. With an X4 Linux executable path
 * the fake image is loaded from that file (read only), so the real pins, the
 * file pin and the patch bytes are exercised too.
 *   test_gate_linux [path/to/X4]
 * Built and run by tools/dev/check_lockstep_gate_linux.py. */
#define GATE_TLS_TEST
#include "gate_linux.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "test_lockstep_relay_host.h"
#include "test_gate_fetch.h"

static __thread unsigned char test_tls[0x2800] __attribute__((aligned(16)));
static unsigned char *gate_tls_test_base(void) {return test_tls+sizeof(test_tls);}

#define FAKE_END 0x7bb7000u /* end of the X4 data segment (.bss) */
static unsigned char *fake;
static unsigned char fake_table[0x138];
static uint64_t test_ticks,test_loading_ticks;
static void put16(unsigned char *p,uint16_t v) {memcpy(p,&v,2);}
static void put32(unsigned char *p,uint32_t v) {memcpy(p,&v,4);}
static void put64(unsigned char *p,uint64_t v) {memcpy(p,&v,8);}
static void putd(uintptr_t va,double v) {memcpy(LX(va),&v,8);}
static double getd(uintptr_t va) {double v;memcpy(&v,LX(va),8);return v;}
/* FrameTick stand-in: TimerBase advances by dt (1/hz under the fixed patch). */
static void test_frame_tick(void *frame,unsigned char loading) {
    assert(frame==(void*)0x1234);
    if(loading) {++test_loading_ticks;return;}
    double dt=dt_state()==DT_FIXED?gate_tick_dt():0.021;
    putd(LX_GAME_VA,getd(LX_GAME_VA)+dt*getd(LX_SPEED_VA));putd(LX_REAL_VA,getd(LX_REAL_VA)+dt);
    ++test_ticks;
}
static void fake_data(void) {
    void *table=fake_table;memcpy(LX(LX_VBI_TABLE_VA),&table,8);memset(fake_table,0,sizeof(fake_table));
    putd(LX_SPEED_VA,1.0);putd(LX_GAME_VA,20.0);putd(LX_REAL_VA,30.0);
    *LX(LX_IN_FLIGHT_VA)=1;*LX(LX_MOVIE_FLAG_VA)=0;
}
static unsigned char *fake_map(void) {
    void *p=mmap(NULL,FAKE_END-LX_IMAGE_BASE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    assert(p!=MAP_FAILED);return p;
}
/* Synthetic image: ELF header, the TLS and text segment rows, the FrameTick call,
 * the original dt qwords and 1/60; pins recomputed from these bytes. */
static lx_site synth_sites[sizeof(lx_sites)/sizeof(*lx_sites)];
static char synth_hashes[sizeof(lx_sites)/sizeof(*lx_sites)][65];
static void synth_image(void) {
    fake=fake_map();image=fake;
    memcpy(fake,"\177ELF\2\1\1",7);put16(fake+16,2);put16(fake+18,62);put64(fake+32,64);put16(fake+54,56);put16(fake+56,12);
    unsigned char *text=fake+64+56*3,*tls=fake+64+56*8;
    put32(text,1);put32(text+4,5);put64(text+16,LX_TEXT_VA);
    put32(tls,7);put32(tls+4,4);put64(tls+16,LX_TLS_VADDR);put64(tls+40,LX_TLS_MEMSZ);put64(tls+48,LX_TLS_ALIGN);
    *LX(LX_CALL_VA)=0xe8;put32(LX(LX_CALL_VA)+1,(uint32_t)(LX_TICK_VA-(LX_CALL_VA+5)));
    put64(LX(LX_DT_LOAD_VA),LX_DT_LOAD_ORIGINAL);put64(LX(LX_DT_MIN_VA),LX_DT_MIN_ORIGINAL);
    putd(LX_ONE_SIXTIETH_VA,1.0/60.0);
    for(unsigned i=0;i<gate_site_count;i++) {
        synth_sites[i]=lx_sites[i];
        assert(digest_hex(LX(lx_sites[i].va),lx_sites[i].length,synth_hashes[i]));
        synth_sites[i].sha256=synth_hashes[i];
    }
    gate_sites=synth_sites;gate_frame_tick=test_frame_tick;
    fake_data();
}
/* Image from the ELF file: every PT_LOAD's file bytes at its address. */
static void elf_image(const char *path) {
    fake=fake_map();image=fake;
    FILE *f=fopen(path,"rb");assert(f);
    unsigned char header[64+56*12];assert(fread(header,1,sizeof(header),f)==sizeof(header));
    for(unsigned i=0;i<12;i++) {
        const unsigned char *p=header+64+56*i;
        if(rd32(p)!=1) continue;
        uint64_t offset=rd64(p+8),va=rd64(p+16),size=rd64(p+32);
        assert(va>=LX_IMAGE_BASE && va+size<=FAKE_END);
        assert(!fseeko(f,(off_t)offset,SEEK_SET) && fread(LX(va),1,size,f)==size);
    }
    fclose(f);
    gate_sites=lx_sites;gate_frame_tick=test_frame_tick;
    fake_data();
}
static void reset_session(void) {
    used=0;installed=0;dt_installed=0;dt_restored=0;boot_mode=0;boot_phase=0;boot_adopted=0;
    main_thread=0;ui_thread=0;wire_version=2;wire_clients=2;wire_lookahead=0;seed_frames=0;rng_seeds=seed_skips=0;
    memset(&stats,0,sizeof(stats));relay_address=0;memcpy(proposal,"00000000",9);
    ui_tls=NULL;ui_tls_tick=UINT64_MAX;
}

static void test_sha256(void) {
    char out[65];
    assert(digest_hex("",0,out) && !strcmp(out,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    assert(digest_hex("abc",3,out) && !strcmp(out,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const char *two="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    assert(digest_hex(two,strlen(two),out) && !strcmp(out,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    static char million[1000000];memset(million,'a',sizeof(million));
    assert(digest_hex(million,sizeof(million),out) && !strcmp(out,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    assert(digest_hex(million,55,out) && !strcmp(out,"9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
    assert(digest_hex(million,56,out) && !strcmp(out,"b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"));
    assert(digest_hex(million,64,out) && !strcmp(out,"ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
    /* Streaming in odd pieces equals one call. */
    lockstep_sha256_ctx c;unsigned char raw[32];char hex[65];lockstep_sha256_init(&c);
    for(size_t at=0,step=1;at<sizeof(million);at+=step,step=step%97+1)
        lockstep_sha256_update(&c,million+at,at+step<=sizeof(million)?step:sizeof(million)-at);
    lockstep_sha256_final(&c,raw);hex32(raw,hex);assert(!strcmp(hex,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    LARGE_INTEGER a,b;QueryPerformanceCounter(&a);
    for(unsigned i=0;i<1000;i++) assert(digest_hex(million+i,16384,out));
    QueryPerformanceCounter(&b);
    printf("sha256 vectors PASS; 16 KB per digest %.1f us\n",(double)(b.QuadPart-a.QuadPart)/1000.0);
}
static void test_compat(void) {
    ULONGLONG t0=GetTickCount64();LARGE_INTEGER q0,q1,f;QueryPerformanceCounter(&q0);
    struct timespec pause={0,20000000};nanosleep(&pause,NULL);
    QueryPerformanceCounter(&q1);QueryPerformanceFrequency(&f);
    assert(GetTickCount64()-t0>=19 && f.QuadPart==1000000 && q1.QuadPart-q0.QuadPart>=19000);
    assert(GetCurrentThreadId() && GetCurrentThreadId()==(DWORD)((uintptr_t)pthread_self()>>12));
    volatile LONG64 n=5;assert(InterlockedIncrement64(&n)==6 && n==6);
    volatile LONG v=3;assert(InterlockedCompareExchange(&v,9,4)==3 && v==3);
    assert(InterlockedCompareExchange(&v,9,3)==3 && v==9);assert(InterlockedExchange(&v,1)==9 && v==1);
    static SRWLOCK lock=SRWLOCK_INIT;AcquireSRWLockExclusive(&lock);ReleaseSRWLockExclusive(&lock);
    AcquireSRWLockShared(&lock);AcquireSRWLockShared(&lock);ReleaseSRWLockShared(&lock);ReleaseSRWLockShared(&lock);
    errno=EAGAIN;assert(WSAGetLastError()==WSAEWOULDBLOCK);errno=EINTR;assert(WSAGetLastError()==WSAEWOULDBLOCK);
    errno=ECONNREFUSED;assert(WSAGetLastError()==ECONNREFUSED);
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_DGRAM,0,pair));u_long queued=99;
    assert(!ioctlsocket(pair[1],FIONREAD,&queued) && queued==0);
    assert(send(pair[0],"abcde",5,0)==5);assert(!ioctlsocket(pair[1],FIONREAD,&queued) && queued==5);
    closesocket(pair[0]);closesocket(pair[1]);
    printf("compat PASS\n");
}
static void perms_of(void *at,char out[5]) {assert(gate_maps_find((uintptr_t)at,out,NULL,0));}
static void test_code_swap(void) {
    unsigned char *page=mmap(NULL,GATE_PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(page!=MAP_FAILED);
    for(unsigned i=0;i<GATE_PAGE;i++) page[i]=(unsigned char)i;
    assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_EXEC));
    uint64_t q;memcpy(&q,page+8,8);char p[5];
    assert(!gate_code_swap64(page+8,q+1,0x1111));memcpy(&q,page+8,8);assert(q==0x0f0e0d0c0b0a0908ull); /* foreign value kept */
    assert(gate_code_swap64(page+8,q,0x1111));memcpy(&q,page+8,8);assert(q==0x1111);
    perms_of(page,p);assert(!strcmp(p,"r-xp"));
    assert(!gate_code_swap64(page+12,0,1)); /* misaligned */
    assert(!gate_code_swap128(page+8,0,1)); /* misaligned for 16 */
    unsigned __int128 w,expected;memcpy(&expected,page+16,16);
    assert(!gate_code_swap128(page+16,expected+1,0));memcpy(&w,page+16,16);assert(w==expected);
    assert(gate_code_swap128(page+16,expected,~expected));memcpy(&w,page+16,16);assert(w==~expected);
    perms_of(page+16,p);assert(!strcmp(p,"r-xp"));
    assert(!gate_code_swap64(page+GATE_PAGE-4,0,0)); /* never across a page */
    munmap(page,GATE_PAGE);
    printf("code swap PASS\n");
}
static void test_near_page(void) {
    static unsigned char anchor;
    unsigned char *p=gate_near_page(&anchor);assert(p && !((uintptr_t)p%GATE_PAGE));
    intptr_t d=(intptr_t)p-(intptr_t)&anchor;assert(d>INT32_MIN/2 && d<INT32_MAX/2);
    char perms[5];perms_of(p,perms);assert(!strcmp(perms,"rw-p"));
    unsigned char *q=gate_near_page(&anchor);assert(q && q!=p); /* never a mapped page again */
    munmap(p,GATE_PAGE);munmap(q,GATE_PAGE);
    printf("near page PASS\n");
}
/* Every pin, the header and the call refuse on one changed byte, naming the stage. */
static void test_verify_stages(void) {
    gate_env.file=0;gate_env.maps=0;gate_env.pin=0;
    assert(verify() && !strcmp(verify_stage,"ok"));
    for(unsigned i=0;i<gate_site_count;i++) {
        unsigned char *at=LX(gate_sites[i].va+gate_sites[i].length/2);
        unsigned char *page=(unsigned char*)((uintptr_t)at&~(uintptr_t)(GATE_PAGE-1));
        assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));
        *at^=0x40;assert(!verify() && !strcmp(verify_stage,gate_sites[i].name));*at^=0x40;
    }
    assert(verify());
    fake[18]^=1;assert(!verify() && !strcmp(verify_stage,"elf"));fake[18]^=1;
    unsigned char *tls=NULL;
    for(unsigned i=0;i<12;i++) if(rd32(fake+64+56*i)==7) tls=fake+64+56*i;
    assert(tls);tls[40]^=0x10;assert(!verify() && !strcmp(verify_stage,"segments"));tls[40]^=0x10;
    assert(verify());
    printf("verify stages PASS (%u sites)\n",gate_site_count);
}
static void test_dt(void) {
    assert(dt_state()==DT_ORIGINAL);
    /* The patched load reads the 1/60 constant: disp32 resolves to it. */
    uint64_t load=dt_fixed_load();int32_t disp;memcpy(&disp,(unsigned char*)&load+4,4);
    assert((uint64_t)((int64_t)LX_DT_LOAD_VA+8+disp)==LX_ONE_SIXTIETH_VA && getd(LX_ONE_SIXTIETH_VA)==1.0/60.0);
    assert((load&0xffffffffu)==(LX_DT_LOAD_ORIGINAL&0xffffffffu));
    *LX(LX_IN_FLIGHT_VA)=0;gate_park_ms=30;assert(!install_dt() && dt_state()==DT_ORIGINAL); /* main thread not parked */
    *LX(LX_IN_FLIGHT_VA)=1;*LX(LX_MOVIE_FLAG_VA)=1;assert(!install_dt());*LX(LX_MOVIE_FLAG_VA)=0;
    assert(install_dt() && dt_state()==DT_FIXED && dt_installed);
    assert(site_ok(site_named(LX_SITE_DT))); /* pinned in its original form */
    restore_dt();assert(dt_restored && !dt_installed && dt_state()==DT_ORIGINAL);
    /* Another rate: the load reads a read-only double 1/30 of this gate within rel32 reach. */
    assert(!gate_tick_configure(25,1,0) && !gate_tick_configure(30.5,1,0) && !gate_tick_configure(30,2,0) && gate_tick_hz==60);
    assert(gate_tick_configure(30,0,0) && gate_tick_hz==30 && gate_tick_dt()==1.0/30.0);
    assert(install_dt() && dt_state()==DT_FIXED && dt_own && *dt_own==1.0/30.0);
    load=dt_fixed_load();memcpy(&disp,(unsigned char*)&load+4,4);
    assert(LX(LX_DT_LOAD_VA)+8+disp==(unsigned char*)dt_own);
    assert(gate_tick_configure(30,0,1) && !gate_tick_configure(60,0,1)); /* after the start only the same values */
    restore_dt();assert(dt_restored && !dt_installed && dt_state()==DT_ORIGINAL);
    assert(gate_tick_configure(60,0,0) && gate_tick_hz==60 && !gate_tick_pace);
    /* The schedule: origin, n/hz, a late frame runs at once, too late re-bases. */
    gate_pace_state pace={0};
    assert(!gate_pace_deadline(&pace,1000,1000000,30) && pace.origin==1000);
    assert(gate_pace_deadline(&pace,2000,1000000,30)==1000+33333);
    assert(!gate_pace_deadline(&pace,70000,1000000,30) && pace.count==2); /* 3.3 ms late: no hold */
    assert(!gate_pace_deadline(&pace,1000000,1000000,30) && pace.rebases==1 && pace.origin==1000000);
    /* A foreign owner of the minsd qword is never overwritten. */
    assert(install_dt());uint64_t foreign=0x9090909090909090ull;
    assert(gate_code_swap64(LX(LX_DT_MIN_VA),LX_DT_MIN_PATCHED,foreign));
    restore_dt();assert(!dt_restored && dt_installed && dt_state()==DT_FOREIGN && rd64(LX(LX_DT_MIN_VA))==foreign);
    assert(gate_code_swap64(LX(LX_DT_MIN_VA),foreign,LX_DT_MIN_PATCHED));restore_dt();assert(dt_restored && dt_state()==DT_ORIGINAL);
    printf("dt patch PASS\n");
}

/* Scripted relay thread (protocol 2): permits echo this client's command and a
 * fixed peer command; optional stop, wrong echo or silence. */
typedef struct {
    int fd,port;unsigned slot,stop_at,mode;volatile int quit;uint64_t last;unsigned packets;
    char token[33],peer[9],last_digest[65],last_command[9];
} fake_relay;
enum {RELAY_PERMIT,RELAY_WRONG,RELAY_SILENT};
static void *relay_thread(void *arg) {
    fake_relay *r=arg;char buffer[2048];
    while(!r->quit) {
        struct sockaddr_in from;socklen_t size=sizeof(from);
        ssize_t n=recvfrom(r->fd,buffer,sizeof(buffer)-1,0,(struct sockaddr*)&from,&size);
        if(n<=0) continue;
        buffer[n]=0;char token[64],digest_text[80],command[16];unsigned slot;unsigned long long tick,game,real;
        if(sscanf(buffer,"NGSTATE|2|%32[0-9a-f]|%u|%llu|%16llx|%16llx|%64[0-9a-f]|%8[0-9a-f]",token,&slot,&tick,&game,&real,digest_text,command)!=7) continue;
        assert(!strcmp(token,r->token) && slot==r->slot);
        ++r->packets;r->last=tick;strcpy(r->last_digest,digest_text);strcpy(r->last_command,command);
        if(r->mode==RELAY_SILENT) continue;
        char reply[256];int m;
        if(r->stop_at && tick>=r->stop_at) m=snprintf(reply,sizeof(reply),"NGSTOP|2|%s|%u|%llu",r->token,slot,tick);
        else m=snprintf(reply,sizeof(reply),"NGGO|2|%s|%u|%llu|%s|%s",r->token,slot,tick+1,
            r->mode==RELAY_WRONG?"0badc0de":(slot==1?command:r->peer),slot==1?r->peer:command);
        sendto(r->fd,reply,(size_t)m,0,(struct sockaddr*)&from,size);
    }
    return NULL;
}
static pthread_t relay_start(fake_relay *r,unsigned mode,unsigned stop_at) {
    memset(r,0,sizeof(*r));r->mode=mode;r->stop_at=stop_at;r->slot=1;strcpy(r->token,"0123456789abcdef0123456789abcdef");strcpy(r->peer,"01070100");
    r->fd=socket(AF_INET,SOCK_DGRAM,0);assert(r->fd>=0);
    struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(!bind(r->fd,(struct sockaddr*)&a,sizeof(a)));socklen_t size=sizeof(a);assert(!getsockname(r->fd,(struct sockaddr*)&a,&size));
    r->port=ntohs(a.sin_port);struct timeval t={0,20000};setsockopt(r->fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));
    pthread_t thread;assert(!pthread_create(&thread,NULL,relay_thread,r));return thread;
}
static void relay_stop(fake_relay *r,pthread_t thread) {r->quit=1;pthread_join(thread,NULL);close(r->fd);}
/* What the main loop calls: the target of the call at LX_CALL_VA. */
static void call_frame(int loading) {
    unsigned char *target=call_target();
    if(target==LX(LX_TICK_VA)) test_frame_tick((void*)0x1234,(unsigned char)loading);
    else {assert(target==thunk);((tick_fn)(void*)thunk)((void*)0x1234,(unsigned char)loading);}
}
static const char *DIGEST0="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static int report(uint64_t tick,const char *command,const char *hash) {
    /* lua_report for protocol 2 */
    if(!gate_command_valid(command)||(stats.report_tick==tick&&strcmp(command,proposal))) return 0;
    int ok=x4gate_report(tick,hash);if(ok) memcpy(proposal,command,9);return ok;
}
static void boot_to_admission(fake_relay *r,double options) {
    unsigned char window[16];memcpy(window,LX(LX_CALL_WINDOW),16);
    assert(boot_gate(1,(unsigned)r->port,r->token,5000,DIGEST0,options)==0 && installed && dt_state()==DT_FIXED);
    assert(call_target()==thunk && thunk[0]==0xff && thunk[1]==0x25);
    char perms[5];perms_of(thunk,perms);assert(!strcmp(perms,"r-xp"));
    assert(!memcmp(window,LX(LX_CALL_WINDOW),7) && !memcmp(window+11,LX(LX_CALL_WINDOW)+11,5)); /* only the rel32 changed */
    assert(boot_gate(1,(unsigned)r->port,r->token,5000,DIGEST0,options)==0); /* the reloaded UI repeats it */
    assert(boot_gate(1,(unsigned)r->port,r->token,5000,DIGEST0,options+4)==LX_STOP_UNPORTED_OPTION);
    call_frame(0);assert(boot_phase==0 && r->packets==0); /* menu: passes through */
    assert(load_requested() && boot_phase==1);
    memset(fake_table+LX_VBI_LOADING,1,1);call_frame(1);call_frame(1);assert(boot_phase==2);
    memset(fake_table+LX_VBI_LOADING,0,1);
}
static void test_frame_gate(void) {
    fake_relay r;pthread_t thread=relay_start(&r,RELAY_PERMIT,40);
    reset_session();uint64_t before=test_ticks;
    unsigned char original[16];memcpy(original,LX(LX_CALL_WINDOW),16);
    /* Refusals leave the image untouched. */
    assert(boot_gate(1,(unsigned)r.port,"short",5000,DIGEST0,0)==10 && !used);
    assert(boot_gate(1,(unsigned)r.port,r.token,5000,DIGEST0,4)==LX_STOP_UNPORTED_OPTION && !used);
    *LX(LX_MOVIE_FLAG_VA)=1;assert(boot_gate(1,(unsigned)r.port,r.token,5000,DIGEST0,0)==27 && !used);*LX(LX_MOVIE_FLAG_VA)=0;
    assert(start_gate(1,(unsigned)r.port,r.token,5000,DIGEST0,0)==12 && !used); /* start needs an external dt owner */
    assert(!memcmp(original,LX(LX_CALL_WINDOW),16) && dt_state()==DT_ORIGINAL);
    boot_to_admission(&r,2);
    /* First live frame: admitted with the initial digest and command. */
    unsigned char *mine=gate_tls_test_base();int32_t worker=1;memcpy(mine+GATE_TLS_LINUX_VIEW,&worker,4);
    uint64_t *rng=gate_tls_rng(mine);*rng=0x1234;
    call_frame(0);assert(boot_phase==3 && stats.issued==1 && r.last==0 && !strcmp(r.last_digest,DIGEST0) && seed_skips==1 && *rng==0x1234);
    char hash[65];
    for(uint64_t tick=1;stats.active;tick++) {
        gate_stats s;assert(x4gate_poll(&s) && s.issued==tick);
        /* Permit of tick T carries the commands sealed with tick T-1. */
        char expect[9];snprintf(expect,sizeof(expect),"%08x",(unsigned)(tick-1)%5==0?0:(unsigned)(tick-1));
        assert(!strcmp(inputs[0],tick==1?"00000000":expect) && !strcmp(inputs[1],"01070100")); /* the scripted peer */
        char command[9];snprintf(command,sizeof(command),"%08x",(unsigned)tick%5==0?0:(unsigned)tick);
        snprintf(hash,sizeof(hash),"%064llx",(unsigned long long)tick);
        assert(!report(tick,"zzzzzzzz",hash) && report(tick,command,hash) && report(tick,command,hash) && !report(tick,"fffffff1",hash));
        call_frame(0);
        if(stats.active) assert(*rng==lockstep_seed(LX_SEED_DOMAIN,stats.issued,0,0,0,0));
    }
    assert(stats.reason==1 && stats.restored==1 && stats.completed==40 && r.last==40 && rng_seeds==39);
    assert(!memcmp(original,LX(LX_CALL_WINDOW),16) && dt_state()==DT_ORIGINAL && dt_restored && connection==INVALID_SOCKET);
    assert(fabs(getd(LX_GAME_VA)-(20.0+41.0/60.0+0.021))<1e-9 && stats.max_dt_error<1e-9); /* menu + 40 gated frames fixed, the stop frame at the clock dt */
    call_frame(0);assert(test_ticks>before); /* back to the original call */
    relay_stop(&r,thread);
    printf("frame gate protocol 2 PASS (40 ticks, NGSTOP, seed, restore)\n");
}
static void test_frame_gate_failures(void) {
    fake_relay r;pthread_t thread;
    /* A permit echoing another proposal stops with 56. */
    thread=relay_start(&r,RELAY_WRONG,0);reset_session();boot_to_admission(&r,0);
    call_frame(0);assert(!stats.active && stats.reason==56 && stats.restored==1 && dt_state()==DT_ORIGINAL);relay_stop(&r,thread);
    /* No report before the next frame: 4. */
    thread=relay_start(&r,RELAY_PERMIT,0);reset_session();boot_to_admission(&r,0);
    call_frame(0);assert(stats.active && stats.issued==1);call_frame(0);assert(!stats.active && stats.reason==4 && stats.restored==1);relay_stop(&r,thread);
    /* A foreign owner of the call window: 3, never overwritten. */
    thread=relay_start(&r,RELAY_PERMIT,0);reset_session();boot_to_admission(&r,0);
    call_frame(0);unsigned __int128 foreign=call_patched^((unsigned __int128)0xff<<120);
    assert(call_swap(call_patched,foreign));call_frame(0);
    assert(!stats.active && stats.reason==3 && stats.restored==0 && installed);
    unsigned __int128 now;memcpy(&now,LX(LX_CALL_WINDOW),16);assert(now==foreign);
    assert(call_swap(foreign,call_original));installed=0;restore_dt();relay_stop(&r,thread);
    /* A changed speed factor: 3. */
    thread=relay_start(&r,RELAY_PERMIT,0);reset_session();boot_to_admission(&r,0);
    call_frame(0);putd(LX_SPEED_VA,2.0);report(1,"00000000",DIGEST0);call_frame(0);
    assert(!stats.active && stats.reason==3 && stats.restored==1);putd(LX_SPEED_VA,1.0);relay_stop(&r,thread);
    /* A silent relay: 7 after the timeout. */
    thread=relay_start(&r,RELAY_SILENT,0);reset_session();
    assert(boot_gate(1,(unsigned)r.port,r.token,1000,DIGEST0,0)==0);
    call_frame(0);assert(load_requested());memset(fake_table+LX_VBI_LOADING,1,1);call_frame(1);memset(fake_table+LX_VBI_LOADING,0,1);
    ULONGLONG start=GetTickCount64();call_frame(0);
    printf("silent: active=%u reason=%u restored=%u waits=%llu phase=%d\n",stats.active,stats.reason,stats.restored,(unsigned long long)stats.waits,boot_phase);
    assert(!stats.active && stats.reason==7 && stats.restored==1 && GetTickCount64()-start>=1000 && stats.waits>=5); /* resent about every 100 ms */relay_stop(&r,thread);
    /* Loading after admission: 2. */
    thread=relay_start(&r,RELAY_PERMIT,0);reset_session();boot_to_admission(&r,0);
    call_frame(0);report(1,"00000000",DIGEST0);memset(fake_table+LX_VBI_LOADING,1,1);call_frame(0);
    assert(!stats.active && stats.reason==2 && stats.restored==1);memset(fake_table+LX_VBI_LOADING,0,1);relay_stop(&r,thread);
    /* UI stop restores at once; the gate is single use per process. */
    thread=relay_start(&r,RELAY_PERMIT,0);reset_session();boot_to_admission(&r,0);
    assert(x4gate_stop()==1 && stats.reason==1 && dt_state()==DT_ORIGINAL);
    /* Single use: the same configuration repeats (reloaded UI) without re-arming; any other is refused. */
    assert(boot_gate(1,(unsigned)r.port,r.token,5000,DIGEST0,0)==0 && !stats.active && !installed && dt_state()==DT_ORIGINAL);
    assert(boot_gate(2,(unsigned)r.port,r.token,5000,DIGEST0,0)==10 && boot_gate(1,(unsigned)r.port,r.token,5000,DIGEST0,2)==10);
    relay_stop(&r,thread);
    printf("frame gate failures PASS (56, 4, 3 foreign, 3 speed, 7, 2, stop)\n");
}
#include "test_gate_world.h"
#include "test_gate_path.h"
#include "test_gate_load.h"
#include "test_gate_loadout.h"
#include "test_gate_attention.h"
#include "test_gate_step_trace.h"
#include "test_gate_fp_redirect.h"
#include "test_gate_coverage.h"
/* The real entry bytes: the hook patch and its exact restoration, without running them. */
static void test_world_install_elf(void) {
    reset_session();wt_reset_identities();
    unsigned char *entry=LX(LX_IMPORT_INSERT_VA),*clear=LX(LX_IMPORT_CLEAR_VA),original[16],original_clear[16];
    memcpy(original,entry,16);memcpy(original_clear,clear,16);
    assert(rd64(entry)==LX_IMPORT_ENTRY_ORIGINAL && rd64(clear)==LX_IMPORT_CLEAR_ORIGINAL);
    thunk=make_thunk();assert(thunk && world_identity_install());
    assert(entry[0]==0xe9 && !memcmp(entry+5,original+5,11) && world_identity_owned());
    assert(clear[0]==0xe9 && !memcmp(clear+5,original_clear+5,11));
    world_identity_restore();
    assert(world_identity_restored && !memcmp(entry,original,16) && !memcmp(clear,original_clear,16));
    printf("world identity patch on the executable image PASS\n");
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    test_fetch();test_fetch_region_cost();
    test_sha256();test_relay_host();test_compat();test_code_swap();test_near_page();
    synth_image();test_verify_stages();test_dt();test_frame_gate();test_frame_gate_failures();
    test_world_golden(getenv("GATE_WORLD_GOLDEN"));
    test_path_determinism();
    test_path_install();
    test_path_budget();
    test_load_patches();test_boot_hold_decisions();test_cue_identity();test_event_order_linux();test_gravidar_linux();
    test_loadout_order_linux();
    test_attention_linux();
    test_step_trace_linux();
    test_step_chain_linux();
    test_fp_redirect_linux();
    test_coverage_linux();
    test_world_hook(); /* last on the synthetic image: it replaces the insert's bytes */
    if(argc>1) {
        /* The real executable's bytes: file pin, every site, the call, the patches. */
        reset_session();elf_image(argv[1]);gate_env.exe=argv[1];gate_env.file=1;
        assert(verify() && !strcmp(verify_stage,"ok"));
        test_dt();
        unsigned char window[16],dt[16];memcpy(window,LX(LX_CALL_WINDOW),16);memcpy(dt,LX(LX_DT_LOAD_VA),16);
        test_frame_gate();test_frame_gate_failures();
        assert(!memcmp(window,LX(LX_CALL_WINDOW),16) && !memcmp(dt,LX(LX_DT_LOAD_VA),16));
        test_world_install_elf();test_path_install();test_path_budget();test_load_patches();test_world_golden(getenv("GATE_WORLD_GOLDEN"));
        test_loadout_order_elf();
        test_fp_redirect_elf();
        test_step_chain_elf();
        printf("ELF image PASS (%s)\n",argv[1]);
    } else printf("ELF image SKIPPED (no executable path)\n");
    printf("ALL PASS\n");
    return 0;
}
