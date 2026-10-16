.section .text
.global smp_entry
.hidden smp_entry
.hidden smp_ap_main
smp_entry:
    cli
    movq 24(%rdi),%rdi
    movq 8(%rdi),%rax
    movq %rax,%cr3
    movq (%rdi),%rsp
    xorq %rbp,%rbp
    call smp_ap_main
    ud2
.global smp_boot_run
.hidden smp_boot_run
.hidden smp_boot_main
smp_boot_run:
    cli
    movq %rdi,%rsp
    xorq %rbp,%rbp
    movq %rsi,%rdi
    call smp_boot_main
    ud2
.section .note.GNU-stack,"",@progbits
