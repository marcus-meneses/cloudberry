/* Appended to the ordinary exports only in the disposable broker variant. */
.section .module_exports,"a",@progbits
.asciz "handles.profile"
.zero 16
.quad handles_surface_profile
.short 1, 0
.long 0
.section .note.GNU-stack,"",@progbits
