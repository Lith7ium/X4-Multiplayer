/* Original owned test bodies, copied only into a private allocated image. */
.text
.p2align 4
.globl gps_test_action
.globl gps_test_action_end
.seh_proc gps_test_action
gps_test_action:
 subq $0x68,%rsp
 .seh_stackalloc 0x68
 .seh_endprologue
 movq %r9,0x20(%rsp)
 movq $0,0x28(%rsp)
 movl $0,0x30(%rsp)
 .org gps_test_action+0x5d,0x90
 call gps_test_action /* relocated to owned evaluator */
 addq $0x68,%rsp
 ret
.seh_endproc
gps_test_action_end:
.p2align 4
.globl gps_test_evaluator
.globl gps_test_evaluator_end
.seh_proc gps_test_evaluator
gps_test_evaluator:
 movq %rsp,%rax
 movq %rbx,8(%rax)
 movq %r8,0x18(%rax)
 movq %rdx,0x10(%rax)
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
 leaq -0x128(%rax),%rbp
 subq $0x220,%rsp
 .seh_stackalloc 0x220
 .seh_savereg %rbx,0x260
 movaps %xmm6,-0x48(%rax)
 .seh_savexmm %xmm6,0x210
 movaps %xmm7,-0x58(%rax)
 .seh_savexmm %xmm7,0x200
 movaps %xmm8,-0x68(%rax)
 .seh_savexmm %xmm8,0x1f0
 .seh_endprologue
 movq %r9,%rbx
 movq %r8,%r12
 movq %rcx,%r15
 movq 0x150(%rbp),%rdi
 movq %r15,%rcx
 leaq -0x70(%rbp),%rdx
 movq %r12,%r8
 movq %rbx,%r9
 .org gps_test_evaluator+0x580,0x90
 call gps_test_evaluator /* relocated to owned candidate entry */
 movups -0x70(%rbp),%xmm0
 movq 0x138(%rbp),%rax
 movups %xmm0,(%rax)
 leaq 0x220(%rsp),%r11
 movq 0x40(%r11),%rbx
 movaps -0x10(%r11),%xmm6
 movaps -0x20(%r11),%xmm7
 movaps -0x30(%r11),%xmm8
 movq %r11,%rsp
 popq %r15
 popq %r14
 popq %r13
 popq %r12
 popq %rdi
 popq %rsi
 popq %rbp
 ret
.seh_endproc
gps_test_evaluator_end:

.p2align 4
.globl gps_test_outer
.globl gps_test_outer_end
.seh_proc gps_test_outer
gps_test_outer:
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
 call *gps_test_enter(%rip)
 xorps %xmm0,%xmm0
 movups %xmm0,(%r15)
 movq $0,0x67(%rbp)
 leaq 0x67(%rbp),%rax
 leaq 0x68(%r14),%rcx
 xor %r8d,%r8d
 movq %r12,%rdx
 .org gps_test_outer+0x3ba,0x90
 call gps_test_outer /* relocated to the original radius entry */
 movq %r15,%rcx
 call *gps_test_finish(%rip)
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
gps_test_outer_end:

.p2align 3
.globl gps_test_enter
.globl gps_test_finish
gps_test_enter:
 .quad 0
gps_test_finish:
 .quad 0
.globl gps_test_outer_data_end
gps_test_outer_data_end:
