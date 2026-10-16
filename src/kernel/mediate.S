.section .text
.global kernel_module_gate
.type kernel_module_gate,@function
kernel_module_gate:
    subq $8, %rsp
    movq %r10, (%rsp)
    call kernel_module_invoke
    addq $8, %rsp
    ret
.size kernel_module_gate, .-kernel_module_gate
.section .note.GNU-stack,"",@progbits
