/* Shared v2 saved-bank selection. Passive bounded copies only; callers still
 * own native Load exclusion, original invocation and output publication. */
#ifndef X4MP_ROTATION_XML_SCOPE_H
#define X4MP_ROTATION_XML_SCOPE_H
#include "rotation_xml_input.h"

enum x4rs_class { X4RS_NAVIGATION = 0, X4RS_PLAYER = 1 };
struct x4rs_snapshot {
    struct x4rx_snapshot xml;
    uint64_t movement, offset, rotation;
    unsigned role, class_id;
};

/* Read a unique selected child, retaining the vector and every scanned tag.
 * Unrelated siblings are deliberately permitted, including targetpoint data. */
static inline int x4rs_child(const struct x4rx_memory *m, struct x4rx_snapshot *s,
        const struct x4rx_layout *l, uint64_t node, uint32_t wanted, uint64_t *out) {
    uint64_t children, found = 0; unsigned n, i; unsigned char b[8]; int r;
    r = x4rx_vector(m,s,node,l->children,l->child_capacity,l->child_count,8,&children,&n);
    if (r) return r;
    for (i=0;i<n;i++) {
        uint64_t child;
        r=x4rx_record_read(m,s,children,(uint64_t)i*8,b,8); if (r) return r;
        child=x4rx_u64(b);
        r=x4rx_record_read(m,s,child,12,b,4); if (r) return r;
        if (x4rx_u32(b)==wanted) {
            if (found) return X4RX_DUPLICATE;
            found=child;
        }
    }
    if (!found) return X4RX_SHAPE;
    *out=found; return X4RX_OK;
}
static inline int x4rs_class_read(const struct x4rx_memory *m, struct x4rx_snapshot *s,
        const struct x4rx_layout *l, uint64_t movement, unsigned *class_id) {
    uint64_t attrs; unsigned n,i,found=0,selected=0; unsigned char raw[40]; int r;
    r=x4rx_vector(m,s,movement,l->attributes,l->attr_capacity,l->attr_count,40,&attrs,&n);
    if (r) return r;
    for (i=0;i<n;i++) {
        uint64_t entry,p,length,capacity,inline_at; char text[11];
        r=x4rx_address(attrs,(uint64_t)i*40,40,&entry); if (r) return r;
        r=x4rx_record_read(m,s,entry,0,raw,40); if (r) return r;
        if (x4rx_u32(raw)!=0xbb) continue;
        if (found++) return X4RX_DUPLICATE;
        if (l->platform==X4RX_WINDOWS) {
            length=x4rx_u64(raw+24); capacity=x4rx_u64(raw+32);
            if (capacity<16) {r=x4rx_address(entry,8,1,&p);if(r)return r;}
            else p=x4rx_u64(raw+8);
        } else {
            p=x4rx_u64(raw+8);length=x4rx_u64(raw+16);
            r=x4rx_address(entry,24,1,&inline_at);if(r)return r;
            capacity=p==inline_at?15:x4rx_u64(raw+24);
        }
        if (!p || (length!=6 && length!=10) || length>capacity || capacity>X4RX_BYTES_MAX)
            return X4RX_BOUNDS;
        r=x4rx_record_read(m,s,p,0,text,(size_t)length+1);if(r)return r;
        if (length==10 && !memcmp(text,"navigation",11)) selected=X4RS_NAVIGATION;
        else if (length==6 && !memcmp(text,"player",7)) selected=X4RS_PLAYER;
        else return X4RX_SHAPE;
    }
    if (!found) return X4RX_SHAPE;
    *class_id=selected;return X4RX_OK;
}

/* All selection and numeric input reads share one bounded replay log. On any
 * refusal the output remains untouched. Source is the platform's proven hook
 * visibility: Windows rotation child, Linux offset reader (including absence). */
static inline int x4rs_capture(const struct x4rx_memory *m,const struct x4rx_layout *l,
        uint64_t movement,unsigned role,uint64_t expected_node,unsigned source,
        struct x4rs_snapshot *out) {
    struct x4rs_snapshot s;uint64_t position,state,attrs,children;
    unsigned n,attr_n,i,positions=0;unsigned char b[8];int r;
    if (!out || !x4rx_layout_valid(l) || role>1 ||
        (source!=X4RX_OFFSET_NODE && source!=X4RX_ROTATION_NODE)) return X4RX_ARGUMENT;
    memset(&s,0,sizeof s);s.movement=movement;s.role=role;
    r=x4rx_record_read(m,&s.xml,movement,12,b,4);if(r)return r;
    if(x4rx_u32(b)!=0x2b2) return X4RX_SHAPE;
    r=x4rs_class_read(m,&s.xml,l,movement,&s.class_id);if(r)return r;
    r=x4rs_child(m,&s.xml,l,movement,0x340,&position);if(r)return r;
    r=x4rs_child(m,&s.xml,l,position,role?0x52f:0x382,&state);if(r)return r;
    r=x4rs_child(m,&s.xml,l,state,0x2dc,&s.offset);if(r)return r;
    if(source==X4RX_OFFSET_NODE && expected_node!=s.offset) return X4RX_SHAPE;
    r=x4rx_vector(m,&s.xml,s.offset,l->attributes,l->attr_capacity,l->attr_count,40,&attrs,&attr_n);
    if(r)return r;
    if(attr_n)return X4RX_SHAPE;
    r=x4rx_vector(m,&s.xml,s.offset,l->children,l->child_capacity,l->child_count,8,&children,&n);
    if(r)return r;
    for(i=0;i<n;i++) {
        uint64_t child;uint32_t tag;
        r=x4rx_record_read(m,&s.xml,children,(uint64_t)i*8,b,8);if(r)return r;
        child=x4rx_u64(b);
        r=x4rx_record_read(m,&s.xml,child,12,b,4);if(r)return r;tag=x4rx_u32(b);
        if(tag==0x340) {if(++positions>1)return X4RX_DUPLICATE;}
        else if(tag==0x3e7) {if(s.rotation)return X4RX_DUPLICATE;s.rotation=child;}
        else return X4RX_SHAPE;
    }
    if(source==X4RX_ROTATION_NODE && (!s.rotation || expected_node!=s.rotation))return X4RX_SHAPE;
    if(s.rotation) {r=x4rx_rotation(m,&s.xml,l,s.rotation,X4RX_DUPLICATES_REFUSE);if(r)return r;}
    *out=s;return s.rotation?X4RX_OK:X4RX_KEEP_NATIVE;
}
static inline int x4rs_unchanged(const struct x4rx_memory *m,const struct x4rs_snapshot *s) {
    if(!s || !s->movement || !s->offset || s->role>1 || s->class_id>X4RS_PLAYER ||
       s->xml.input.orientation!=(unsigned)(s->rotation!=0)) return X4RX_ARGUMENT;
    return x4rx_unchanged(m,&s->xml);
}
#endif
