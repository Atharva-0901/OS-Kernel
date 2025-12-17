; boot.asm - Multiboot header and entry point

bits 32                         ; 32-bit mode

section .multiboot
    ; Multiboot header constants
    MBOOT_MAGIC     equ 0x1BADB002
    MBOOT_FLAGS     equ (1 << 0) | (1 << 1)  ; align modules, memory info
    MBOOT_CHECKSUM  equ -(MBOOT_MAGIC + MBOOT_FLAGS)

    ; Multiboot header
    align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM

section .bss
    align 16
    stack_bottom:
        resb 16384      ; 16 KB stack
    stack_top:

section .text
    global _start
    extern kernel_main

_start:
    ; Set up stack
    mov esp, stack_top

    ; Push multiboot info (optional for future expansion)
    push ebx        ; Multiboot info structure
    push eax        ; Multiboot magic number

    ; Call kernel main
    call kernel_main

    ; Halt if kernel returns
    cli
.hang:
    hlt
    jmp .hang

; Global Descriptor Table (GDT) - basic setup
global gdt_flush
extern gp

gdt_flush:
    lgdt [gp]       ; Load GDT
    mov ax, 0x10    ; Data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush ; Far jump to code segment
.flush:
    ret

; Interrupt Service Routines (ISR) stubs
global isr0
global isr1

isr0:
    cli
    push byte 0
    push byte 0
    jmp isr_common_stub

isr1:
    cli
    push byte 0
    push byte 1
    jmp isr_common_stub

isr_common_stub:
    pusha           ; Push all registers
    push ds
    push es
    push fs
    push gs
    
    mov ax, 0x10    ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    extern isr_handler
    call isr_handler
    
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8      ; Clean up pushed error code and ISR number
    iret            ; Return from interrupt
