/* Nongame test-process environment adapter only; never linked into an agent. */
#include "portable_rotation.h"
#include <fenv.h>
#include <xmmintrin.h>

int x4rot_test_offset_batch(unsigned environment,const unsigned *orientations,
                           const struct x4rot_attribute *attributes,size_t count,
                           int *statuses,uint32_t *outputs) {
    static const int modes[4]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    unsigned before=_mm_getcsr(),control; int rounding=fegetround(); size_t i;
    if (environment>=8u || rounding<0 || fesetround(modes[environment&3u])) return -1;
    control=_mm_getcsr(); control&=~((1u<<15)|(1u<<6));
    if (environment&4u) control|=(1u<<15)|(1u<<6);
    _mm_setcsr(control);
    if (fegetround()!=modes[environment&3u] || _mm_getcsr()!=control || ((control>>13)&3u)!=(environment&3u)) {
        (void)fesetround(rounding); _mm_setcsr(before); return -4;
    }
    for (i=0;i<count;i++) statuses[i]=x4rot_offset(orientations[i],attributes+3*i,outputs+9*i);
    if (fesetround(rounding)) { _mm_setcsr(before); return -2; }
    _mm_setcsr(before);
    return fegetround()==rounding && _mm_getcsr()==before?0:-3;
}
