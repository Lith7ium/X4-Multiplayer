/* Standalone sanitizer/fixture replay. Input is generated private test evidence,
 * never a game executable or process. Every expected output bit is compared. */
#include "portable_rotation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int read_word(FILE *file,uint32_t *out) {
    unsigned char bytes[4];
    if (fread(bytes,1,4,file)!=4) return 0;
    *out=(uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
    return 1;
}
static void unchanged(const uint32_t out[9]) { unsigned i; for (i=0;i<9;i++) assert(out[i]==0xdeadbeefu); }
static void basics(void) {
    static const char midpoint[]="0.8875748217105865478515625";
    uint32_t out[9],value=0xdeadbeefu,angles[3]={0x3f490fdbu,0x3f490fdbu,0x7f7fffffu};
    char huge[256]; struct x4rot_attribute attrs[3]={{NULL,0,0},{NULL,0,1},{"bad",3,1}}; unsigned i;
    for (i=0;i<9;i++) out[i]=0xdeadbeefu;
    assert(x4rot_offset(X4ROT_ABSENT,attrs,out)==X4ROT_KEEP_NATIVE); unchanged(out);
    assert(x4rot_offset(X4ROT_EULER,attrs,out)==X4ROT_UNSUPPORTED); unchanged(out);
    assert(x4rot_decimal(midpoint,sizeof midpoint-1u,&value)==X4ROT_OK && value==0x3f63381au);
    value=0xdeadbeefu; memset(huge,'9',sizeof huge);
    assert(x4rot_decimal(huge,sizeof huge,&value)==X4ROT_OVERFLOW && value==0xdeadbeefu);
    assert(x4rot_decimal(NULL,0,&value)==X4ROT_UNSUPPORTED && value==0xdeadbeefu);
    assert(x4rot_sincos(0x7f800000u,out)==X4ROT_UNSUPPORTED); unchanged(out);
    assert(x4rot_basis(angles,out)==X4ROT_OK && out[0]==0x3eaf39e9u);
}
int main(int argc,char **argv) {
    static const unsigned char magic[8]={'X','4','R','O','T','1',0,0};
    FILE *file; unsigned char actual_magic[8]; uint32_t count,index; unsigned converted=0,kept=0;
    basics();
    if (argc!=2) { fprintf(stderr,"one private bounded fixture path required\n"); return 2; }
    file=fopen(argv[1],"rb"); assert(file);
    assert(fread(actual_magic,1,8,file)==8 && !memcmp(actual_magic,magic,8));
    assert(read_word(file,&count) && count<=25000u);
    for (index=0;index<count;index++) {
        struct x4rot_attribute attrs[3]; char text[3][257]; uint32_t orientation,status,out[9],expected[9]; unsigned i; int result;
        memset(text,'x',sizeof text); assert(read_word(file,&orientation));
        for (i=0;i<3;i++) {
            uint32_t present,length;
            assert(read_word(file,&present) && present<=1u && read_word(file,&length) && length<=256u);
            assert(fread(text[i],1,length,file)==length);
            attrs[i].present=present; attrs[i].length=length; attrs[i].text=present?text[i]:NULL;
        }
        assert(read_word(file,&status));
        for (i=0;i<9;i++) { assert(read_word(file,&expected[i])); out[i]=0xdeadbeefu; }
        result=x4rot_offset(orientation,attrs,out);
        assert(result==(int)status && !memcmp(out,expected,sizeof out));
        if (result==X4ROT_OK) ++converted; else { assert(result==X4ROT_KEEP_NATIVE); ++kept; }
    }
    assert(fgetc(file)==EOF && !ferror(file)); assert(fclose(file)==0);
    printf("PASS standalone exact fixture records=%u converted=%u kept=%u\n",count,converted,kept);
    return 0;
}
