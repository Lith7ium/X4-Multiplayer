/* Owned synthetic addresses only. This executable never opens a game/process. */
#include "rotation_xml_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

#define BASE UINT64_C(0x10000)
#define MEM_SIZE 0x10000u
#define NODE (BASE + 0x100)
#define ROT (BASE + 0x400)
#define POS (BASE + 0x700)
#define HEAP_CHILD (BASE + 0x1000)
#define HEAP_ATTR (BASE + 0x2000)
#define TEXT (BASE + 0x4000)
#define BANK (BASE + 0x6000)
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static unsigned checks;
struct arena {
    unsigned char bytes[MEM_SIZE];
    unsigned char pages[MEM_SIZE / 256];
    unsigned reads, stop_at, revoke_at, original_calls, evaluate_calls;
    int stop, original_action, eval_result;
    uint64_t change_at;
    struct x4rx_guard guard;
};
static struct arena a;
static jmp_buf missing_return;
static void put32(uint64_t p, uint32_t x) {
    unsigned i; for (i=0;i<4;++i) a.bytes[(size_t)(p-BASE)+i]=(unsigned char)(x>>(8*i));
}
static void put64(uint64_t p, uint64_t x) {
    unsigned i; for (i=0;i<8;++i) a.bytes[(size_t)(p-BASE)+i]=(unsigned char)(x>>(8*i));
}
static int read_bytes(void *context, uint64_t p, void *out, size_t n) {
    struct arena *m=(struct arena *)context; size_t first,last,i;
    ++m->reads;
    if (m->stop_at && m->reads==m->stop_at) m->stop=1;
    if (m->revoke_at && m->reads==m->revoke_at) {
        m->pages[(ROT-BASE)/256]=0;
        m->guard.active=0; /* Model owner lifetime ending, not just a read fault. */
    }
    if (p<BASE || p-BASE>=MEM_SIZE || n>MEM_SIZE-(size_t)(p-BASE)) return 0;
    first=(size_t)(p-BASE)/256; last=((size_t)(p-BASE)+n-1)/256;
    for (i=first;i<=last;++i) if (!m->pages[i]) return 0;
    memcpy(out,m->bytes+(size_t)(p-BASE),n);
    return 1;
}
static int stopped(void *context) { return ((struct arena *)context)->stop; }
static struct x4rx_memory memory(void) {
    struct x4rx_memory m={&a,read_bytes,stopped}; return m;
}
static void node(uint64_t p, uint32_t tag, const struct x4rx_layout *l) {
    put32(p+12,tag); put64(p+l->child_capacity,4); put64(p+l->attr_capacity,4);
}
static void setup(const struct x4rx_layout *l, int heap) {
    unsigned i;
    memset(&a,0,sizeof(a)); memset(a.pages,1,sizeof(a.pages));
    a.guard.load_serial=9; a.guard.owner_thread=20; a.guard.current_thread=20;
    a.guard.module=30; a.guard.manager=40; a.guard.active=1;
    node(NODE,0x2dc,l); node(ROT,0x3e7,l); node(POS,0x340,l);
    put64(NODE+l->child_count,2);
    if (heap) { put64(NODE+l->child_capacity,8); put64(NODE+l->children,HEAP_CHILD); }
    put64((heap?HEAP_CHILD:NODE+l->children),POS);
    put64((heap?HEAP_CHILD:NODE+l->children)+8,ROT);
    if (heap) { put64(ROT+l->attr_capacity,8); put64(ROT+l->attributes,HEAP_ATTR); }
    for (i=0;i<X4RX_BANK_SIZE;++i) a.bytes[BANK-BASE+i]=(unsigned char)(i^0xa7);
}
static uint64_t attribute(const struct x4rx_layout *l, int heap, unsigned index,
                          uint32_t tag, const char *text, int long_storage) {
    uint64_t p=(heap?HEAP_ATTR:ROT+l->attributes)+(uint64_t)index*40;
    uint64_t chars=TEXT+(uint64_t)index*512; size_t n=strlen(text);
    put32(p,tag);
    if (l->platform==X4RX_WINDOWS) {
        put64(p+24,n); put64(p+32,long_storage?512:15);
        if (long_storage) put64(p+8,chars); else chars=p+8;
    } else {
        if (!long_storage) chars=p+24;
        put64(p+8,chars); put64(p+16,n);
        if (long_storage) put64(p+24,512);
    }
    memcpy(a.bytes+(size_t)(chars-BASE),text,n+1);
    put64(ROT+l->attr_count,index+1);
    return p;
}
static int capture(const struct x4rx_layout *l, unsigned source, unsigned duplicate,
                   struct x4rx_snapshot *s) {
    struct x4rx_memory m=memory();
    return x4rx_capture(&m,l,source==X4RX_OFFSET_NODE?NODE:ROT,source,duplicate,s);
}
static void bad_unchanged(const struct x4rx_layout *l, int expected) {
    struct x4rx_snapshot s,before; memset(&s,0x5b,sizeof(s)); before=s;
    CHECK(capture(l,X4RX_OFFSET_NODE,X4RX_DUPLICATES_REFUSE,&s)==expected);
    CHECK(!memcmp(&s,&before,sizeof(s)));
}
static int get_guard(void *context, struct x4rx_guard *g) {
    struct arena *m=(struct arena *)context; *g=m->guard; g->stopped=(unsigned)m->stop; return 1;
}
static int evaluate(void *context, const struct x4rx_input *input, uint32_t words[9]) {
    struct arena *m=(struct arena *)context; unsigned i; ++m->evaluate_calls;
    CHECK(input->orientation==1);
    for(i=0;i<9;++i) words[i]=UINT32_C(0x3f000000)+i;
    if(m->eval_result==2) words[3]=UINT32_C(0x7f800000);
    if(m->eval_result==3) m->stop=1;
    return m->eval_result==1?-1:0;
}
static void original(void *context) {
    struct arena *m=(struct arena *)context; ++m->original_calls;
    /* A native-like original writes position as well as the basis. The helper
       must preserve this result and only propose subsequent basis changes. */
    memset(m->bytes+(size_t)(BANK-BASE),0x6d,X4RX_BANK_SIZE);
    if(m->original_action==1) m->stop=1;
    if(m->original_action==2) ++m->guard.load_serial;
    if(m->original_action==3) m->pages[(ROT-BASE)/256]=0;
    if(m->original_action==4) m->bytes[(size_t)(m->change_at-BASE)]^=1;
    if(m->original_action==5) ++m->guard.manager;
    if(m->original_action==6) m->guard.active=0;
    if(m->original_action==7) m->guard.failed=1;
    if(m->original_action==8) ++m->guard.current_thread;
    if(m->original_action==9) ++m->guard.module;
    if(m->original_action==10) longjmp(missing_return,1);
}
static int plan(const struct x4rx_layout *l, struct x4rx_plan *p) {
    struct x4rx_memory m=memory();
    return x4rx_after_original_plan(&m,l,NODE,X4RX_OFFSET_NODE,BANK,get_guard,&a,
        evaluate,&a,original,&a,p);
}
static void basic_cases(const struct x4rx_layout *l,int heap) {
    struct x4rx_snapshot s; struct x4rx_memory m=memory(); uint64_t entry;
    setup(l,heap); attribute(l,heap,0,0x532,"90",0);
    CHECK(capture(l,X4RX_OFFSET_NODE,0,&s)==X4RX_OK);
    CHECK(s.input.orientation==1 && s.input.angles[1].present==1);
    CHECK(!strcmp(s.input.angles[1].text,"90") && s.input.angles[1].length==2);
    CHECK(!s.input.angles[0].present && !s.input.angles[2].present);
    CHECK(x4rx_unchanged(&m,&s)==X4RX_OK);
    CHECK(capture(l,X4RX_ROTATION_NODE,0,&s)==X4RX_OK);
    setup(l,heap); attribute(l,heap,0,0x3e2,"-0",0);
    attribute(l,heap,1,0x532,"",0); attribute(l,heap,2,0x32e,".125",0);
    CHECK(capture(l,1,0,&s)==X4RX_OK);
    CHECK(!strcmp(s.input.angles[0].text,".125"));
    CHECK(s.input.angles[1].present && !s.input.angles[1].length);
    CHECK(!strcmp(s.input.angles[2].text,"-0"));
    setup(l,heap); put64(NODE+l->child_count,1);
    CHECK(capture(l,1,0,&s)==X4RX_KEEP_NATIVE && !s.input.orientation);
    setup(l,heap); CHECK(capture(l,1,0,&s)==X4RX_OK && s.input.orientation);
    CHECK(!s.input.angles[0].present && !s.input.angles[1].present && !s.input.angles[2].present);
    setup(l,heap); entry=attribute(l,heap,0,0x532,"0.8875748217105865478515625",1);
    CHECK(capture(l,1,0,&s)==X4RX_OK && s.input.angles[1].length==strlen("0.8875748217105865478515625"));
    a.bytes[(size_t)(entry-BASE)]^=1; CHECK(x4rx_unchanged(&m,&s)==X4RX_CHANGED);
    setup(l,heap); attribute(l,heap,0,0x532,"90",0); attribute(l,heap,1,0x532,"-90",0);
    bad_unchanged(l,X4RX_DUPLICATE);
    CHECK(capture(l,1,X4RX_FIRST_ATTRIBUTE,&s)==X4RX_OK);
    CHECK(s.input.duplicate_attributes==1 && !strcmp(s.input.angles[1].text,"90"));
    setup(l,heap); attribute(l,heap,0,0x111,"0",0); bad_unchanged(l,X4RX_SHAPE);
    setup(l,heap); put32(ROT+12,0x36a); bad_unchanged(l,X4RX_SHAPE);
    setup(l,heap); put64(NODE+l->attr_count,1); bad_unchanged(l,X4RX_SHAPE);
    setup(l,heap); put64(ROT+l->child_count,1); bad_unchanged(l,X4RX_SHAPE);
    setup(l,heap); put64((heap?HEAP_CHILD:NODE+l->children),ROT); bad_unchanged(l,X4RX_DUPLICATE);
    setup(l,heap); put64((heap?HEAP_CHILD:NODE+l->children)+8,POS); bad_unchanged(l,X4RX_DUPLICATE);
}
static void bounds_cases(const struct x4rx_layout *l) {
    static const char *bad[]={"+90"," 90","90;","1e2","NaN","inf","--1","-",".","1.2.3"};
    unsigned i; uint64_t e; char text[258]; struct x4rx_snapshot s; struct x4rx_memory m=memory();
    for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        setup(l,0); attribute(l,0,0,0x532,bad[i],0); bad_unchanged(l,X4RX_TEXT);
    }
    setup(l,0); attribute(l,0,0,0x532,"90",0); a.stop=1; bad_unchanged(l,X4RX_STOP);
    setup(l,0); a.stop_at=3; bad_unchanged(l,X4RX_STOP);
    setup(l,0); a.pages[(ROT-BASE)/256]=0; bad_unchanged(l,X4RX_READ);
    setup(l,0); put64(NODE+l->child_count,5); bad_unchanged(l,X4RX_BOUNDS);
    setup(l,0); put64(NODE+l->child_capacity,3); bad_unchanged(l,X4RX_BOUNDS);
    setup(l,1); put64(NODE+l->child_count,33); put64(NODE+l->child_capacity,40); bad_unchanged(l,X4RX_BOUNDS);
    setup(l,1); put64(NODE+l->children,UINT64_MAX-5); bad_unchanged(l,X4RX_BOUNDS);
    setup(l,1); put64(NODE+l->children,0); bad_unchanged(l,X4RX_BOUNDS);
    setup(l,1); put64(ROT+l->attr_capacity,X4RX_CAPACITY_MAX+1); bad_unchanged(l,X4RX_BOUNDS);
    setup(l,0); memset(text,'1',256); text[256]=0;
    attribute(l,0,0,0x532,text,1); CHECK(capture(l,1,0,&s)==X4RX_OK && s.input.angles[1].length==256);
    setup(l,0); text[256]='1'; text[257]=0; attribute(l,0,0,0x532,text,1); bad_unchanged(l,X4RX_BOUNDS);
    setup(l,0); e=attribute(l,0,0,0x532,"90",0);
    put64(e+(l->platform==X4RX_WINDOWS?24:16),16); bad_unchanged(l,X4RX_BOUNDS);
    setup(l,0); e=attribute(l,0,0,0x532,"90",1); put64(e+8,UINT64_MAX-1); bad_unchanged(l,X4RX_BOUNDS);
    setup(l,0); e=attribute(l,0,0,0x532,"90",1);
    put64(e+(l->platform==X4RX_WINDOWS?32:24),1); bad_unchanged(l,X4RX_BOUNDS);
    setup(l,0); attribute(l,0,0,0x532,"90",1); a.bytes[TEXT-BASE+2]='x'; bad_unchanged(l,X4RX_TEXT);
    setup(l,0); attribute(l,0,0,0x532,"90",1); a.bytes[TEXT-BASE]=0; bad_unchanged(l,X4RX_TEXT);
    setup(l,0); attribute(l,0,0,0x532,"90",1);
    CHECK(capture(l,1,0,&s)==X4RX_OK); a.pages[(TEXT-BASE)>>8]=0;
    CHECK(x4rx_unchanged(&m,&s)==X4RX_READ);
    setup(l,0); CHECK(x4rx_capture(&m,l,UINT64_MAX-3,1,0,&s)==X4RX_BOUNDS);
    { struct x4rx_layout wrong=*l; wrong.attr_count++; CHECK(x4rx_capture(&m,&wrong,NODE,1,0,&s)==X4RX_ARGUMENT); }
    { uint64_t out; CHECK(x4rx_address(UINT64_MAX,1,1,&out)==X4RX_BOUNDS); }
    memset(&s,0,sizeof(s)); s.records=X4RX_RECORD_MAX+1; CHECK(x4rx_unchanged(&m,&s)==X4RX_ARGUMENT);
    setup(l,1); memset(text,'1',256); text[256]=0;
    put64(ROT+l->attr_capacity,32);
    for(i=0;i<32;++i) attribute(l,1,i,0x532,text,1);
    CHECK(capture(l,1,X4RX_FIRST_ATTRIBUTE,&s)==X4RX_BOUNDS); /* Bounded transcript budget. */
}
static void lifecycle_cases(const struct x4rx_layout *l) {
    struct x4rx_plan p,unchanged; unsigned i,j; int action; unsigned char after[X4RX_BANK_SIZE];
    setup(l,0); attribute(l,0,0,0x532,"90",0);
    CHECK(plan(l,&p)==X4RX_OK && a.original_calls==1 && a.evaluate_calls==1);
    memcpy(after,a.bytes+BANK-BASE,sizeof(after));
    for(i=0;i<9;++i) {
        CHECK(p.words[i].offset==0x10+(i/3)*16+(i%3)*4);
        CHECK(p.words[i].before==UINT32_C(0x6d6d6d6d));
        /* Apply ONLY to this test-owned copy, never via a native-memory writer. */
        for(j=0;j<4;++j) after[p.words[i].offset+j]=(unsigned char)(p.words[i].candidate>>(j*8));
    }
    for(i=0;i<sizeof(after);++i) {
        int selected=i>=0x10 && i<0x40 && i%16<12;
        if(!selected) CHECK(after[i]==0x6d);
        CHECK(a.bytes[BANK-BASE+i]==0x6d); /* Helper itself wrote nothing. */
    }
    for(action=1;action<=9;++action) {
        setup(l,0); attribute(l,0,0,0x532,"90",1); a.change_at=TEXT; a.original_action=action;
        memset(&p,0x5a,sizeof(p)); unchanged=p;
        CHECK(plan(l,&p)<0 && a.original_calls==1);
        CHECK(!memcmp(&p,&unchanged,sizeof(p)));
    }
    setup(l,0); a.guard.owner_thread=0; CHECK(plan(l,&p)==X4RX_GUARD && a.original_calls==1 && a.evaluate_calls==0);
    setup(l,0); a.stop=1; CHECK(plan(l,&p)==X4RX_GUARD && a.original_calls==1);
    setup(l,0); attribute(l,0,0,0x532,"90x",0); CHECK(plan(l,&p)==X4RX_TEXT && a.original_calls==1 && !a.evaluate_calls);
    setup(l,0); a.eval_result=1; CHECK(plan(l,&p)==X4RX_EVALUATOR && a.original_calls==1);
    setup(l,0); a.eval_result=2; CHECK(plan(l,&p)==X4RX_EVALUATOR && a.original_calls==1);
    setup(l,0); a.eval_result=3; CHECK(plan(l,&p)==X4RX_GUARD && a.original_calls==1);
    setup(l,0); put64(NODE+l->child_count,1);
    memset(&p,0x5a,sizeof(p)); unchanged=p;
    CHECK(plan(l,&p)==X4RX_KEEP_NATIVE && a.original_calls==1 && !a.evaluate_calls);
    CHECK(!memcmp(&p,&unchanged,sizeof(p)));
    /* Sweep STOP and page revocation over every successful reader boundary. */
    setup(l,0); attribute(l,0,0,0x532,"90",1); CHECK(plan(l,&p)==X4RX_OK); j=a.reads;
    for(i=1;i<=j;++i) {
        setup(l,0); attribute(l,0,0,0x532,"90",1); a.stop_at=i;
        CHECK(plan(l,&p)<0 && a.original_calls==1);
        setup(l,0); attribute(l,0,0,0x532,"90",1); a.revoke_at=i;
        CHECK(plan(l,&p)<0 && a.original_calls==1);
    }
    setup(l,0); attribute(l,0,0,0x532,"90",0); a.original_action=10;
    memset(&p,0x5a,sizeof(p)); unchanged=p;
    if(setjmp(missing_return)==0) { (void)plan(l,&p); CHECK(0); }
    CHECK(a.original_calls==1 && !memcmp(&p,&unchanged,sizeof(p)));
}

/* Actual compiler ABI calls, only to these owned functions and arrays. These
   do not validate a future thunk's relocation/unwind/patch implementation. */
#if defined(__x86_64__) || defined(_M_X64)
struct typed_call { unsigned char *bank; const unsigned char *xml; unsigned mode; };
static unsigned typed_calls;
static const unsigned char *typed_expected_xml;
static void __attribute__((ms_abi,noinline)) owned_windows(unsigned char *basis, const unsigned char *xml) {
    CHECK(xml==typed_expected_xml); CHECK(basis==a.bytes+BANK-BASE+0x10); ++typed_calls;
    basis[0]=0x8b;
}
static void __attribute__((sysv_abi,noinline)) owned_linux(unsigned char *bank, const unsigned char *xml) {
    CHECK(xml==typed_expected_xml); CHECK(bank==a.bytes+BANK-BASE); ++typed_calls;
    bank[0]=0x9c;
}
static void typed_original(void *context) {
    struct typed_call *c=(struct typed_call *)context;
    if(c->mode==X4RX_WINDOWS) owned_windows(c->bank+0x10,c->xml);
    else owned_linux(c->bank,c->xml);
}
static void typed_cases(const struct x4rx_layout *l) {
    struct x4rx_plan p; struct x4rx_memory m=memory(); struct typed_call c;
    setup(l,0); attribute(l,0,0,0x532,"90",0);
    c.bank=a.bytes+BANK-BASE; c.xml=a.bytes+(l->platform==X4RX_WINDOWS?ROT:NODE)-BASE; c.mode=l->platform;
    typed_expected_xml=c.xml; typed_calls=0;
    CHECK(x4rx_after_original_plan(&m,l,l->platform==X4RX_WINDOWS?ROT:NODE,
        l->platform==X4RX_WINDOWS?X4RX_ROTATION_NODE:X4RX_OFFSET_NODE,BANK,
        get_guard,&a,evaluate,&a,typed_original,&c,&p)==X4RX_OK);
    CHECK(typed_calls==1);
    CHECK(c.bank[l->platform==X4RX_WINDOWS?0x10:0]==(l->platform==X4RX_WINDOWS?0x8b:0x9c));
}
#endif
int main(void) {
    const struct x4rx_layout *layouts[]={&x4rx_windows,&x4rx_linux}; unsigned i;
    for(i=0;i<2;++i) { basic_cases(layouts[i],0); basic_cases(layouts[i],1); bounds_cases(layouts[i]); lifecycle_cases(layouts[i]); }
#if defined(__x86_64__) || defined(_M_X64)
    for(i=0;i<2;++i) typed_cases(layouts[i]);
#endif
    printf("PASS rotation XML/candidate helpers: %u checks, synthetic owned memory only\n",checks);
    return 0;
}
