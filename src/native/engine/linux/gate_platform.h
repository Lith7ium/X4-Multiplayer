/* Linux platform layer of the lockstep gate (gate_linux.c).
 *
 * Part 1 gives the shared gate headers (lockstep_wire.h, lockstep_lookahead.h,
 * lockstep_gate_*.h) the few Win32 names they use, so those headers compile
 * unchanged on both platforms: integer types, GetTickCount64, the performance
 * counter, thread ids, Interlocked*, SRW locks and the WinSock spellings of BSD
 * sockets. Only what the shared code needs; semantics as on Windows.
 *
 * Part 2 holds the Linux engine primitives: /proc/self/maps queries, the file
 * pin of /proc/self/exe, compare-exchange patches of code (mprotect around an
 * 8- or 16-byte lock cmpxchg), a page within rel32 reach of the image, and a
 * fault-safe read (below, gate_read_local).
 * No Egosoft code or bytes are included. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>

/* ---- Part 1: Win32 spellings used by the shared gate headers ---- */
typedef uint32_t DWORD;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int64_t LONG64;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef int SOCKET;
#define INVALID_SOCKET (-1)
typedef union {struct {uint32_t LowPart;int32_t HighPart;} u;int64_t QuadPart;} LARGE_INTEGER;
static inline ULONGLONG GetTickCount64(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (ULONGLONG)t.tv_sec*1000u+(ULONGLONG)t.tv_nsec/1000000u;
}
/* Microsecond counter: ft_us multiplies the difference by 10^6 before dividing. */
static inline int QueryPerformanceCounter(LARGE_INTEGER *value) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    value->QuadPart=(int64_t)t.tv_sec*1000000+t.tv_nsec/1000;return 1;
}
static inline int QueryPerformanceFrequency(LARGE_INTEGER *value) {value->QuadPart=1000000;return 1;}
/* Asked per hook call and only ever compared, so no system call (gettid is microseconds on a
 * virtual machine): the page of the thread's control block; each thread has its own stack. */
static inline DWORD GetCurrentThreadId(void) {return (DWORD)((uintptr_t)pthread_self()>>12);}
#define InterlockedIncrement64(p) __atomic_add_fetch((p),1,__ATOMIC_SEQ_CST)
#define InterlockedExchange(p,v) __atomic_exchange_n((p),(v),__ATOMIC_SEQ_CST)
#define InterlockedExchange64(p,v) __atomic_exchange_n((p),(v),__ATOMIC_SEQ_CST)
#define InterlockedAdd64(p,v) __atomic_add_fetch((p),(v),__ATOMIC_SEQ_CST)
/* Returns the prior value, like the Win32 call. */
#define InterlockedCompareExchange(p,v,e) __extension__({__typeof__(*(p)+0) gate_cas_=(e); /* +0 drops volatile */ \
    __atomic_compare_exchange_n((p),&gate_cas_,(v),0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);gate_cas_;})
#define InterlockedCompareExchange64(p,v,e) InterlockedCompareExchange((p),(v),(e))
typedef pthread_rwlock_t SRWLOCK;
#define SRWLOCK_INIT PTHREAD_RWLOCK_INITIALIZER
static inline void AcquireSRWLockExclusive(SRWLOCK *l) {pthread_rwlock_wrlock(l);}
static inline void ReleaseSRWLockExclusive(SRWLOCK *l) {pthread_rwlock_unlock(l);}
static inline void AcquireSRWLockShared(SRWLOCK *l) {pthread_rwlock_rdlock(l);}
static inline void ReleaseSRWLockShared(SRWLOCK *l) {pthread_rwlock_unlock(l);}
#define WSAETIMEDOUT ETIMEDOUT
#define WSAEWOULDBLOCK EWOULDBLOCK
/* A receive timeout (SO_RCVTIMEO) is EAGAIN on Linux; a signal (EINTR) is
 * retried like a timeout instead of stopping the session. */
static inline int WSAGetLastError(void) {return errno==EAGAIN || errno==EINTR?WSAEWOULDBLOCK:errno;}
static inline int closesocket(SOCKET s) {return close(s);}
static inline int ioctlsocket(SOCKET s,unsigned long request,u_long *value) {
    int count=0;int result=ioctl(s,request,&count);*value=(u_long)(count>0?count:0);return result;
}

/* ---- Part 2: Linux engine primitives ---- */
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000 /* Linux 4.17; older kernels treat it as a hint (checked) */
#endif
#define GATE_PAGE 4096u
/* The mapping holding address: permissions ("r-xp") and path ("" when anonymous). */
static inline int gate_maps_find(uintptr_t address,char perms[5],char *path,size_t capacity) {
    FILE *maps=fopen("/proc/self/maps","re");if(!maps) return 0;
    char line[PATH_MAX+128];int found=0;
    while(!found && fgets(line,sizeof(line),maps)) {
        unsigned long long start,end,offset;char p[5],dev[16];unsigned long inode;int consumed=0;
        if(sscanf(line,"%llx-%llx %4s %llx %15s %lu %n",&start,&end,p,&offset,dev,&inode,&consumed)<6) continue;
        if(address<start || address>=end) continue;
        found=1;memcpy(perms,p,5);
        if(path && capacity) {
            const char *name=consumed?line+consumed:"";size_t n=strcspn(name,"\n");
            if(n>=capacity) n=capacity-1;
            memcpy(path,name,n);path[n]=0;
        }
    }
    fclose(maps);return found;
}
/* The executable's own path, resolved once. */
static inline int gate_exe_realpath(char out[PATH_MAX]) {
    ssize_t n=readlink("/proc/self/exe",out,PATH_MAX-1);if(n<=0) return 0;
    out[n]=0;return 1;
}
/* address lies in a mapping of the running executable file with exactly perms. */
static inline int gate_maps_exe(uintptr_t address,const char *perms) {
    char p[5],path[PATH_MAX],exe[PATH_MAX];
    return gate_exe_realpath(exe) && gate_maps_find(address,p,path,sizeof(path)) && !strcmp(p,perms) && !strcmp(path,exe);
}
/* SHA-256 of a whole file of exactly size bytes. */
static inline int gate_file_sha256(const char *path,unsigned long long size,unsigned char out[32]) {
    int f=open(path,O_RDONLY|O_CLOEXEC);if(f<0) return 0;
    static unsigned char buffer[1<<16];unsigned long long total=0;int ok=1;
    lockstep_sha256_ctx c;lockstep_sha256_init(&c);
    for(;;) {
        ssize_t got=read(f,buffer,sizeof(buffer));
        if(got<0 && errno==EINTR) continue;
        if(got<0) {ok=0;break;}
        if(!got) break;
        total+=(unsigned long long)got;
        if(total>size) {ok=0;break;}
        lockstep_sha256_update(&c,buffer,(size_t)got);
    }
    close(f);
    if(!ok || total!=size) return 0;
    lockstep_sha256_final(&c,out);return 1;
}
/* Code patches: one lock cmpxchg on a naturally aligned 8 or 16 bytes inside one
 * page, with the page made writable only around it. Callers verify beforehand that
 * the page is r-x and restore exactly that. Serialised; never overwrites a foreign
 * value (1 only when the expected value was found and replaced). */
static pthread_mutex_t gate_code_lock=PTHREAD_MUTEX_INITIALIZER;
static inline int gate_code_open(void *at,size_t size) {
    uintptr_t a=(uintptr_t)at,page=a&~(uintptr_t)(GATE_PAGE-1);
    if(a%size || (a+size-1)/GATE_PAGE!=a/GATE_PAGE) return 0;
    return !mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC);
}
static inline int gate_code_close(void *at) {
    uintptr_t page=(uintptr_t)at&~(uintptr_t)(GATE_PAGE-1);
    return !mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_EXEC);
}
static inline int gate_code_swap64(void *at,uint64_t expected,uint64_t value) {
    pthread_mutex_lock(&gate_code_lock);
    int ok=gate_code_open(at,8);
    if(ok) {
        uint64_t seen=expected;
        ok=__atomic_compare_exchange_n((uint64_t*)at,&seen,value,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
        if(!gate_code_close(at)) ok=0;
    }
    pthread_mutex_unlock(&gate_code_lock);return ok;
}
static inline int gate_code_swap128(void *at,unsigned __int128 expected,unsigned __int128 value) {
    pthread_mutex_lock(&gate_code_lock);
    int ok=gate_code_open(at,16);
    if(ok) {
        unsigned __int128 actual=__sync_val_compare_and_swap((unsigned __int128*)at,expected,value);
        ok=actual==expected;
        if(!gate_code_close(at)) ok=0;
    }
    pthread_mutex_unlock(&gate_code_lock);return ok;
}
/* A fresh private RW page within rel32 reach of target: below it first (the image
 * of an ET_EXEC binary starts at 0x400000; above it lies the brk heap, whose growth
 * must not be blocked). MAP_FIXED_NOREPLACE never replaces a mapping; a kernel
 * that ignores the flag returns another address, which is released. */
static inline unsigned char *gate_near_page(const void *target) {
    uintptr_t at=(uintptr_t)target&~(uintptr_t)0xffff;
    for(uintptr_t distance=0x10000;distance<0x70000000u;distance+=0x10000) {
        for(int sign=-1;sign<=1;sign+=2) {
            if(sign<0 && distance>=at) continue;
            uintptr_t candidate=sign<0?at-distance:at+distance;
            if(candidate<0x10000) continue;
            void *p=mmap((void*)candidate,GATE_PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
            if(p==MAP_FAILED) continue;
            if((uintptr_t)p!=candidate) {munmap(p,GATE_PAGE);continue;}
            return p;
        }
    }
    return NULL;
}
/* One aligned word of a read-only data page (a vtable slot in RELRO): compare-exchange with
 * the page writable for the moment, read-only again afterwards. */
static inline int gate_data_swap64(void *at,uint64_t expected,uint64_t value) {
    uintptr_t a=(uintptr_t)at,page=a&~(uintptr_t)(GATE_PAGE-1);
    if(a%8) return 0;
    pthread_mutex_lock(&gate_code_lock);
    int ok=!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE);
    if(ok) {
        uint64_t seen=expected;
        ok=__atomic_compare_exchange_n((uint64_t*)at,&seen,value,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
        if(mprotect((void*)page,GATE_PAGE,PROT_READ)) ok=0;
    }
    pthread_mutex_unlock(&gate_code_lock);return ok;
}
/* Copy bytes from this process without faulting on an unmapped source, two ways, same answer.
 *
 * process_vm_readv needs no setup but is two system calls per read, which on a virtual machine
 * is what U::Region's 461 Moves per frame cost most (bundle 9: 47 ms per frame). So while the
 * gate runs (gate_fetch_install, from the pinned module) the read is a plain copy, and a fault
 * inside that copy returns 0: the SIGSEGV/SIGBUS handler moves RIP to the failure return, the
 * same idea as the SEH block on Windows and a JVM's SafeFetch. Every other fault goes on to the
 * handler that was there before, or to the default action, as if the gate were absent. A later
 * owner of the signal turns the fast way off (gate_fetch_check, once per frame). */
__asm__(".pushsection .text\n"
        ".p2align 4\n"
        ".type gate_fetch_copy,@function\n"
        "gate_fetch_copy:\n"             /* rdi dest, rsi source, rdx bytes; eax 1, or 0 after a fault */
        "    cmp $8,%rdx\n"
        "    jne 1f\n"
        "    mov (%rsi),%rax\n"
        "    mov %rax,(%rdi)\n"
        "    mov $1,%eax\n"
        "    ret\n"
        "1:  mov %rdx,%rcx\n"
        "    rep movsb\n"
        "    mov $1,%eax\n"
        "    ret\n"
        "gate_fetch_fault:\n"
        "    xor %eax,%eax\n"
        "    ret\n"
        ".size gate_fetch_copy,.-gate_fetch_copy\n"
        ".popsection\n");
extern int gate_fetch_copy(void *dest,const void *source,size_t bytes) __attribute__((visibility("hidden")));
extern unsigned char gate_fetch_fault[] __attribute__((visibility("hidden")));
static struct sigaction gate_fetch_prior[2]; /* SIGSEGV, SIGBUS */
static int gate_fetch_on,gate_fetch_installed;
static uint64_t gate_fetch_faults,gate_fetch_lost,gate_read_count; /* the last one: approximate, no lock prefix per read */
static uint64_t gate_fetch_cost_slow_ns,gate_fetch_cost_fast_dns,gate_fetch_cost_tid_ns; /* dns: tenths of a ns */
static void gate_fetch_signal(int sig,siginfo_t *info,void *context) {
    ucontext_t *u=context;uintptr_t at=(uintptr_t)u->uc_mcontext.gregs[REG_RIP];
    if(info->si_code>0 && at>=(uintptr_t)gate_fetch_copy && at<(uintptr_t)gate_fetch_fault) {
        u->uc_mcontext.gregs[REG_RIP]=(greg_t)(uintptr_t)gate_fetch_fault;
        __atomic_add_fetch(&gate_fetch_faults,1,__ATOMIC_RELAXED);
        return;
    }
    const struct sigaction *prior=&gate_fetch_prior[sig==SIGBUS];
    int none=prior->sa_handler==SIG_DFL || prior->sa_handler==SIG_IGN;
    if(none || (prior->sa_flags&SA_RESETHAND)) {
        struct sigaction fallback;memset(&fallback,0,sizeof(fallback));fallback.sa_handler=SIG_DFL;sigemptyset(&fallback.sa_mask);
        sigaction(sig,&fallback,NULL);
    }
    if(none) return; /* the instruction faults again, now under the default action */
    if(prior->sa_flags&SA_SIGINFO) prior->sa_sigaction(sig,info,context);else prior->sa_handler(sig);
}
static inline int gate_fetch_ours(int sig) {
    struct sigaction now;
    return !sigaction(sig,NULL,&now) && (now.sa_flags&SA_SIGINFO) && now.sa_sigaction==gate_fetch_signal;
}
static inline int gate_read_slow(const void *source,void *dest,size_t bytes) {
    struct iovec local={dest,bytes},remote={(void*)source,bytes};
    return process_vm_readv(getpid(),&local,1,&remote,1,0)==(ssize_t)bytes;
}
static inline int gate_read_local(const void *source,void *dest,size_t bytes) {
    if(!source) return 0;
    ++*(volatile uint64_t*)&gate_read_count;
    if(__atomic_load_n(&gate_fetch_on,__ATOMIC_RELAXED)) return gate_fetch_copy(dest,source,bytes);
    return gate_read_slow(source,dest,bytes);
}
/* What a read costs on this machine, once, for the log (a few ms at most, before the first frame). */
static inline uint64_t gate_now_ns(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;
}
static inline void gate_fetch_measure(void) {
    enum {SLOW=256,FAST=16384};
    volatile uint64_t word=1;uint64_t out=0;
    uint64_t a=gate_now_ns();
    for(unsigned i=0;i<SLOW;i++) gate_read_slow((const void*)&word,&out,8);
    uint64_t b=gate_now_ns();
    for(unsigned i=0;i<FAST;i++) gate_fetch_copy(&out,(const void*)&word,8);
    uint64_t c=gate_now_ns();
    for(unsigned i=0;i<SLOW;i++) syscall(SYS_gettid);
    uint64_t d=gate_now_ns();
    gate_fetch_cost_slow_ns=(b-a)/SLOW;gate_fetch_cost_fast_dns=(c-b)*10/FAST;gate_fetch_cost_tid_ns=(d-c)/SLOW;
}
/* The handler copies the prior's mask and flags, so a chained handler runs as it asked to. A
 * failure leaves the slow way, which is correct, only slower. */
static inline void gate_fetch_install(void) {
    static const int signals[2]={SIGSEGV,SIGBUS};
    if(gate_fetch_installed) return;
    for(int i=0;i<2;i++) {
        struct sigaction own;
        if(sigaction(signals[i],NULL,&gate_fetch_prior[i])) own.sa_flags=-1;
        else {
            own=gate_fetch_prior[i];own.sa_sigaction=gate_fetch_signal;
            own.sa_flags=(gate_fetch_prior[i].sa_flags|SA_SIGINFO|SA_ONSTACK)&~SA_RESETHAND;
        }
        if(own.sa_flags==-1 || sigaction(signals[i],&own,NULL)) {
            if(i) sigaction(signals[0],&gate_fetch_prior[0],NULL);
            return;
        }
    }
    gate_fetch_installed=1;__atomic_store_n(&gate_fetch_on,1,__ATOMIC_SEQ_CST);
    gate_fetch_measure();
}
static inline void gate_fetch_check(void) {
    if(!__atomic_load_n(&gate_fetch_on,__ATOMIC_RELAXED) || (gate_fetch_ours(SIGSEGV) && gate_fetch_ours(SIGBUS))) return;
    __atomic_store_n(&gate_fetch_on,0,__ATOMIC_SEQ_CST);++gate_fetch_lost;
}
/* Back to the prior handler; a later owner stays (it may chain to ours, whose data stays valid). */
static inline void gate_fetch_remove(void) {
    if(!gate_fetch_installed) return;
    __atomic_store_n(&gate_fetch_on,0,__ATOMIC_SEQ_CST);
    if(gate_fetch_ours(SIGSEGV)) sigaction(SIGSEGV,&gate_fetch_prior[0],NULL);
    if(gate_fetch_ours(SIGBUS)) sigaction(SIGBUS,&gate_fetch_prior[1],NULL);
    gate_fetch_installed=0;
}
