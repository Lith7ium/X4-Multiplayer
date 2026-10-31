/* Count X4 generator steps between two TLS RNG words (Random::U64State step).
 * stdin: "from to limit" (hex, hex, decimal) per line; stdout: steps or -1.
 * Build: zig cc -O2 -target x86_64-windows-gnu rng_steps.c -o rng_steps.exe */
#include <stdio.h>
#include <stdint.h>
static uint64_t step(uint64_t v){v=v*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (v>>30)|(v<<34);}
int main(void){unsigned long long a,b,lim;while(scanf("%llx %llx %llu",&a,&b,&lim)==3){long long r=-1;uint64_t x=a;
for(unsigned long long i=0;i<=lim;i++){if(x==b){r=(long long)i;break;}x=step(x);}printf("%lld\n",r);fflush(stdout);}return 0;}
