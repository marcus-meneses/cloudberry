.section .text
.global process_enter
.hidden process_enter
.type process_enter,@function
/* enter(entry, user_rsp, argument, &saved_kernel_rsp). IF is clear. */
process_enter:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp,(%rcx)
    pushq $0x2b
    pushq %rsi
    pushq $0x202
    pushq $0x33
    pushq %rdi
    movq %rdx,%rdi
    xorq %rax,%rax
    xorq %rbx,%rbx
    xorq %rcx,%rcx
    xorq %rdx,%rdx
    xorq %rsi,%rsi
    xorq %rbp,%rbp
    xorq %r8,%r8
    xorq %r9,%r9
    xorq %r10,%r10
    xorq %r11,%r11
    xorq %r12,%r12
    xorq %r13,%r13
    xorq %r14,%r14
    xorq %r15,%r15
    swapgs
    iretq
.global process_return
.hidden process_return
process_return:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.section .note.GNU-stack,"",@progbits
