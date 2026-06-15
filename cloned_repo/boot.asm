.global _start

.text
_start:
    mov x0, 1
    ldr x1, =hello_wrld
    mov x2, 13
    mov x8, 64
    svc 0

    mov x0, 69
    mov x8, 93
    svc 0

.data
hello_wrld:
    .ascii "Hello, World!\n"
