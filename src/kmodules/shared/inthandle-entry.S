.section .text
.global inthandle_activate
inthandle_activate:
    lgdt (%rdi)
    pushq $8
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $16, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $24, %ax
    ltr %ax
    lidt (%rsi)
    ret

.altmacro
.macro VECTOR number
inthandle_vector_\number:
    .if (\number != 8) && (\number != 10) && (\number != 11) && (\number != 12) && (\number != 13) && (\number != 14) && (\number != 17) && (\number != 21) && (\number != 29) && (\number != 30)
        pushq $0
    .endif
    pushq $\number
    jmp inthandle_common
.endm
.set vector,0
.rept 256
    VECTOR %vector
    .set vector,vector+1
.endr

inthandle_common:
    testb $3,24(%rsp)
    jz 8f
    swapgs
8:
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
    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $-16, %rsp
    call inthandle_dispatch
    movq %rbx, %rsp
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
    addq $16, %rsp
    testb $3,8(%rsp)
    jz 9f
    andq $0x250fd7,16(%rsp)
    orq $0x202,16(%rsp)
    swapgs
9:
    iretq
.section .rodata
.balign 4
.global inthandle_vectors
inthandle_vectors:
.macro VECTOR_OFFSET number
    .long inthandle_vector_\number - inthandle_vectors
.endm
.set vector,0
.rept 256
    VECTOR_OFFSET %vector
    .set vector,vector+1
.endr
.section .note.GNU-stack,"",@progbits
