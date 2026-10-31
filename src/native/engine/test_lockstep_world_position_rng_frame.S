/* Original owned fixture, never native engine instructions. Copied as one
 * position-independent block into the test image; only the sampler CALL and
 * the private RNG data pointer are relocated by the test. */
.text
.p2align 4
.globl wpr_test_range
.globl wpr_test_radius
.globl wpr_test_frame_end
.globl wpr_test_rng
.seh_proc wpr_test_range
wpr_test_range:
 movq %rsp,%rax
 movq %rbx,0x18(%rax)
 movq %rdx,0x10(%rax)
 movq %rcx,0x8(%rax)
 pushq %rbp
 .seh_pushreg %rbp
 pushq %rsi
 .seh_pushreg %rsi
 pushq %rdi
 .seh_pushreg %rdi
 pushq %r12
 .seh_pushreg %r12
 pushq %r13
 .seh_pushreg %r13
 pushq %r14
 .seh_pushreg %r14
 pushq %r15
 .seh_pushreg %r15
 leaq -0x4f(%rax),%rbp
 subq $0x100,%rsp
 .seh_stackalloc 0x100
 .seh_savereg %rbx,0x150
 movaps %xmm6,0xf0(%rsp)
 .seh_savexmm %xmm6,0xf0
 .seh_endprologue
 movq $5,0x30(%rsp)
 movq $0x47435000,0x38(%rsp)
 movq $0,0x50(%rsp)
 movq $0,0x58(%rsp)
 movq $5,-0x79(%rbp)
 movq $0x47435000,-0x71(%rbp)
 movq wpr_test_result_sink(%rip),%rax
 movq %r8,(%rax)
 movq wpr_test_rng(%rip),%rcx
 movl $0x47435000,%eax
 movd %eax,%xmm1
 movl $1,%r8d
 movl $1,%r9d
 .org wpr_test_range+0xbac,0x90
 call wpr_test_range /* relocated to the owned sampler entry */
 movaps 0xf0(%rsp),%xmm6
 addq $0x100,%rsp
 popq %r15
 popq %r14
 popq %r13
 popq %r12
 popq %rdi
 popq %rsi
 popq %rbp
 movq 0x18(%rsp),%rbx
 ret
.seh_endproc
 .org wpr_test_range+0xd60,0x90
.seh_proc wpr_test_radius
wpr_test_radius:
 subq $0x58,%rsp
 .seh_stackalloc 0x58
 movaps %xmm6,0x40(%rsp)
 .seh_savexmm %xmm6,0x40
 .seh_endprologue
 movq $0,0x30(%rsp)
 movl $0x5a5aa5a5,0x34(%rsp)
 movq $0,0x38(%rsp)
 leaq 0x30(%rsp),%r8
 .org wpr_test_range+0xd8a,0x90
 call wpr_test_range
 movaps 0x40(%rsp),%xmm6
 addq $0x58,%rsp
 ret
.seh_endproc
.p2align 3
wpr_test_rng:
 .quad 0
.globl wpr_test_result_sink
wpr_test_result_sink:
 .quad 0
wpr_test_frame_end:
