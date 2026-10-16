.section .rodata.logo,"a",@progbits
.balign 8
.global splash_logo,splash_logo_end
.hidden splash_logo,splash_logo_end
splash_logo:
.incbin SPLASH_LOGO_PATH
splash_logo_end:
.section .note.GNU-stack,"",@progbits
