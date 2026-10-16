.text
.global test_stack_call
.type test_stack_call,@function
/* Test-only call on an allocated stack; restore the original stack afterwards.
 * (top, mediated method, id, owner) -> normal two-register surface result. */
test_stack_call:
    pushq %rbx
    pushq %r12
    movq %rsp,%rbx
    movq %rsi,%r12
    movq %rdi,%rsp
    andq $-16,%rsp
    movq %rdx,%rdi
    movq %rcx,%rsi
    xorl %edx,%edx
    xorl %ecx,%ecx
    xorl %r8d,%r8d
    xorl %r9d,%r9d
    call *%r12
    movq %rbx,%rsp
    popq %r12
    popq %rbx
    ret
.size test_stack_call,.-test_stack_call
.section .note.GNU-stack,"",@progbits
