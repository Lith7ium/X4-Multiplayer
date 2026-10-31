/* Owned-memory regression of the shared class/path scope on both XML layouts. */
#define main input_regressions
#include "test_rotation_xml_input.c"
#undef main
#include "rotation_xml_scope.h"
#define MOVE (BASE+0x8000)
#define POSITION (BASE+0x8300)
#define READ (BASE+0x8600)
#define WRITE (BASE+0x8900)
#define SIBLING (BASE+0x8c00)
#define CLASS_TEXT (BASE+0x9000)
static void children(uint64_t p,const struct x4rx_layout *l,uint64_t x,uint64_t y) {
    put64(p+l->child_count,y?2:1);put64(p+l->children,x);
    if(y)put64(p+l->children+8,y);
}
static void scope_setup(const struct x4rx_layout *l,unsigned cls,unsigned role,int heap) {
    setup(l,heap);attribute(l,heap,0,0x532,"90",0);
    node(MOVE,0x2b2,l);node(POSITION,0x340,l);node(READ,0x382,l);node(WRITE,0x52f,l);
    node(SIBLING,0x36a,l); /* unrelated sibling may contain unsupported orientation */
    children(MOVE,l,POSITION,SIBLING);children(POSITION,l,READ,WRITE);
    children(role?WRITE:READ,l,NODE,SIBLING);
    uint64_t entry=MOVE+l->attributes,chars=CLASS_TEXT;const char *text=cls?"player":"navigation";
    put32(entry,0xbb);put64(MOVE+l->attr_count,1);
    if(l->platform==X4RX_WINDOWS) {
        put64(entry+24,strlen(text));put64(entry+32,heap?32:15);
        if(heap)put64(entry+8,chars);else chars=entry+8;
    }else {
        if(!heap)chars=entry+24;
        put64(entry+8,chars);put64(entry+16,strlen(text));if(heap)put64(entry+24,32);
    }
    memcpy(a.bytes+chars-BASE,text,strlen(text)+1);
}
static int scoped(const struct x4rx_layout *l,unsigned role,unsigned source,struct x4rs_snapshot *s) {
    struct x4rx_memory m=memory();return x4rs_capture(&m,l,MOVE,role,source==X4RX_ROTATION_NODE?ROT:NODE,source,s);
}
static void scope_cases(const struct x4rx_layout *l,unsigned cls,unsigned role,int heap) {
    struct x4rs_snapshot s,prior;struct x4rx_memory m=memory();unsigned source=l->platform==X4RX_WINDOWS?2:1;
    scope_setup(l,cls,role,heap);CHECK(scoped(l,role,source,&s)==0);
    CHECK(s.class_id==cls && s.role==role && s.movement==MOVE && s.offset==NODE && s.rotation==ROT);
    CHECK(!strcmp(s.xml.input.angles[1].text,"90"));CHECK(x4rs_unchanged(&m,&s)==0);
    /* Every captured structural/value byte is replayed, not just the angles. */
    for(unsigned i=0;i<s.xml.records;i++)for(unsigned j=0;j<s.xml.record[i].length;j++) {
        size_t at=(size_t)(s.xml.record[i].address-BASE)+j;
        a.bytes[at]^=1;CHECK(x4rs_unchanged(&m,&s)==X4RX_CHANGED);a.bytes[at]^=1;
    }
    for(unsigned mutant=0;mutant<15;mutant++) {
        scope_setup(l,cls,role,heap);memset(&s,0x54,sizeof s);prior=s;
        switch(mutant) {
        case 0:put32(MOVE+12,0x340);break;
        case 1:put32(MOVE+l->attributes,0x111);break;
        case 2:put32(POSITION+12,0x111);break;
        case 3:put32((role?WRITE:READ)+12,0x111);break;
        case 4:put32(NODE+12,0x111);break;
        case 5:put64(MOVE+l->children+8,POSITION);break;
        case 6:put64(POSITION+l->children+(role?0:8),role?WRITE:READ);break;
        case 7:put64((role?WRITE:READ)+l->children+8,NODE);break;
        case 8:put64((heap?HEAP_CHILD:NODE+l->children)+8,POS);break;
        case 9:put32(ROT+12,0x36a);break;
        case 10:put64(MOVE+l->child_count,33);break;
        case 11:put64(MOVE+l->child_capacity,UINT64_MAX);break;
        case 12:a.stop=1;break;
        case 13:a.pages[(POSITION-BASE)/256]=0;break;
        case 14:{uint64_t p=MOVE+l->attributes+(l->platform==X4RX_WINDOWS?24:16);put64(p,11);break;}
        }
        CHECK(scoped(l,role,source,&s)<0);CHECK(!memcmp(&s,&prior,sizeof s));
    }
    scope_setup(l,cls,role,heap);CHECK(x4rs_capture(&m,l,MOVE,role,POS,source,&s)==X4RX_SHAPE);
    CHECK(x4rs_capture(&m,l,UINT64_MAX-8,role,ROT,source,&s)==X4RX_BOUNDS);
    CHECK(x4rs_capture(&m,l,MOVE,2,ROT,source,&s)==X4RX_ARGUMENT);
    scope_setup(l,cls,role,heap);put64(NODE+l->child_count,1);
    CHECK(scoped(l,role,X4RX_OFFSET_NODE,&s)==X4RX_KEEP_NATIVE && !s.xml.input.orientation);
    CHECK(scoped(l,role,X4RX_ROTATION_NODE,&s)==X4RX_SHAPE);
    scope_setup(l,cls,role,heap);CHECK(scoped(l,role,source,&s)==0);unsigned reads=a.reads;
    for(unsigned i=1;i<=reads;i++) {
        scope_setup(l,cls,role,heap);a.stop_at=i;CHECK(scoped(l,role,source,&s)==X4RX_STOP);
    }
}
int main(void) {
    CHECK(sizeof(struct x4rs_snapshot)+128<16384);
    input_regressions();
    for(unsigned platform=0;platform<2;platform++)for(unsigned cls=0;cls<2;cls++)
      for(unsigned role=0;role<2;role++)for(int heap=0;heap<2;heap++)
        scope_cases(platform?&x4rx_linux:&x4rx_windows,cls,role,heap);
    printf("rotation scoped XML: %u checks passed\n",checks);return 0;
}
