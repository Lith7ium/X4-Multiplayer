/* Original owned fixture, never native engine instructions. Copied as one
 * position-independent block into the test image; only the sampler CALL and
 * the private RNG data pointer are relocated by the test. */
.text
.p2align 4
.globl wp_test_range
.globl wp_test_radius
.globl wp_test_frame_end
.globl wp_test_rng
.seh_proc wp_test_range
wp_test_range:
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
 subq $0x100,%rsp
 .seh_stackalloc 0x100
 .seh_savereg %rbx,0x150
 movaps %xmm6,0xf0(%rsp)
 .seh_savexmm %xmm6,0xf0
 .seh_endprologue
 movq $5,0x30(%rsp)
 movq $0x42c80000,0x38(%rsp)
 movq $5,0x50(%rsp)
 movq $0,0x58(%rsp)
 movq wp_test_rng(%rip),%rcx
 movl $0x42c80000,%eax
 movd %eax,%xmm1
 movl $1,%r8d
 movl $1,%r9d
 .org wp_test_range+0xbac,0x90
 call wp_test_range /* relocated to the owned sampler entry */
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
 .org wp_test_range+0xd60,0x90
.seh_proc wp_test_radius
wp_test_radius:
 subq $0x58,%rsp
 .seh_stackalloc 0x58
 movaps %xmm6,0x40(%rsp)
 .seh_savexmm %xmm6,0x40
 .seh_endprologue
 .org wp_test_range+0xd8a,0x90
 call wp_test_range
 movaps 0x40(%rsp),%xmm6
 addq $0x58,%rsp
 ret
.seh_endproc
.p2align 3
wp_test_rng:
 .quad 0
wp_test_frame_end:

/* Owned native-shaped outer body. The test copies this to the selected RVA,
 * patches its real entry and radius CALL, and enters through an owned caller.
 * There is no engine expression/getter execution. */
.p2align 4
.globl wp_test_optional_outer
.globl wp_test_optional_end
.seh_proc wp_test_optional_outer
wp_test_optional_outer:
 .byte 0x48,0x8b,0xc4,0x48,0x89,0x58,0x10
 movq %rsi,0x18(%rax)
 movq %rdi,0x20(%rax)
 pushq %rbp
 .seh_pushreg %rbp
 pushq %r12
 .seh_pushreg %r12
 pushq %r13
 .seh_pushreg %r13
 pushq %r14
 .seh_pushreg %r14
 pushq %r15
 .seh_pushreg %r15
 leaq -0x5f(%rax),%rbp
 subq $0x100,%rsp
 .seh_stackalloc 0x100
 .seh_savereg %rbx,0x138
 .seh_savereg %rsi,0x140
 .seh_savereg %rdi,0x148
 movaps %xmm6,0xf0(%rsp)
 .seh_savexmm %xmm6,0xf0
 movaps %xmm7,0xe0(%rsp)
 .seh_savexmm %xmm7,0xe0
 movaps %xmm8,0xd0(%rsp)
 .seh_savexmm %xmm8,0xd0
 movaps %xmm9,0xc0(%rsp)
 .seh_savexmm %xmm9,0xc0
 movaps %xmm10,0xb0(%rsp)
 .seh_savexmm %xmm10,0xb0
 .seh_endprologue
 movq %r8,%r12
 movq %rdx,%r15
 movq %rcx,%r14
 xorps %xmm0,%xmm0
 movups %xmm0,(%r15)
 movq $0,0x67(%rbp)
 leaq 0x67(%rbp),%rax
 leaq 0x68(%r14),%rcx
 xor %r8d,%r8d
 movq %r12,%rdx
 .org wp_test_optional_outer+0x3ba,0x90
 call wp_test_optional_outer /* relocated to the original radius entry */
 movq %r15,%rax
 leaq 0x100(%rsp),%r11
 movq 0x38(%r11),%rbx
 movq 0x40(%r11),%rsi
 movq 0x48(%r11),%rdi
 movaps -0x10(%r11),%xmm6
 movaps -0x20(%r11),%xmm7
 movaps -0x30(%r11),%xmm8
 movaps -0x40(%r11),%xmm9
 movaps -0x50(%r11),%xmm10
 movq %r11,%rsp
 popq %r15
 popq %r14
 popq %r13
 popq %r12
 popq %rbp
 ret
.seh_endproc
wp_test_optional_end:
