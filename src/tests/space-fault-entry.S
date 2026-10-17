.section .text
.global space_fault_entry
.hidden space_fault_entry
.hidden space_fault_dispatch
space_fault_entry:
    pushq $14
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    cld
    movq %rsp,%rdi
    movq %rsp,%rbx
    andq $-16,%rsp
    call space_fault_dispatch
    movq %rbx,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16,%rsp
    iretq
.global space_fault_read,space_fault_read_ip,space_fault_read_resume
.hidden space_fault_read,space_fault_read_ip,space_fault_read_resume
space_fault_read:
space_fault_read_ip:
    movq (%rdi),%rax
space_fault_read_resume:
    ret
.global space_fault_write,space_fault_write_ip,space_fault_write_resume
.hidden space_fault_write,space_fault_write_ip,space_fault_write_resume
space_fault_write:
space_fault_write_ip:
    movq %rsi,(%rdi)
space_fault_write_resume:
    ret
.global space_fault_execute,space_fault_execute_resume
.hidden space_fault_execute,space_fault_execute_resume
space_fault_execute:
    call *%rdi
    ret
space_fault_execute_resume:
    addq $8,%rsp
    ret
.section .note.GNU-stack,"",@progbits
