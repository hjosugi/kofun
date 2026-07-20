/*
 * Linux x86-64 process entry for the freestanding CLI runtime.
 * Kernel stack: argc, argv[], NULL, envp[], NULL.
 */

.section .text.start,"ax",@progbits
.global _start
.type _start,@function
_start:
    xor %rbp, %rbp
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi
    lea 16(%rsp,%rdi,8), %rdx
    and $-16, %rsp
    call cli_main
    mov %eax, %edi
    mov $60, %eax
    syscall
    ud2
.size _start, .-_start

.section .cli_config,"a",@progbits
.global cli_config_anchor
.type cli_config_anchor,@object
cli_config_anchor:
    .ascii "KOFUN-CLI-CONFIG"
.size cli_config_anchor, .-cli_config_anchor

.section .note.GNU-stack,"",@progbits
