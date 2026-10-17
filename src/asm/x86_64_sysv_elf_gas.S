/*****************************************************************/ /**
 * @file   x86_64_sysv_elf_gas.S
 * @brief  System V AMD64 calling convention, ELF, GNU assembler syntax.
 *         Targets: Linux x86_64 (GCC, Clang).
 *
 * Integer arguments: rdi, rsi, rdx, rcx, r8, r9. Float arguments: xmm0-7.
 * Integer return: rax. Float return: xmm0.
 *
 * Offsets must match `call_frame` and `spill_tray` in src/internal.h:
 *   call_frame: fn 0, stack_size 8, stack 16, ret_gpr 24, ret_fpr 32,
 *               gpr[6] 40..80, fpr[8] 88..144
 *   spill_tray: ret_gpr 0, ret_fpr 8, gpr[6] 16..56, fpr[8] 64..120
 *
 * Hardening: every entry starts with endbr64 (a NOP without CET) and the
 * object carries the IBT and SHSTK GNU properties, so linking callconv
 * never disables CET for the final binary. Both routines only use
 * matched call/ret pairs, which is what shadow stacks require.
 *********************************************************************/

        .text

/*-------------------------------------------------------------------
 * void callconv_internal_call(call_frame* frame)
 *-------------------------------------------------------------------*/
        .p2align 4
        .globl  callconv_internal_call
        .hidden callconv_internal_call
        .type   callconv_internal_call, @function
callconv_internal_call:
        .cfi_startproc
        endbr64
        pushq   %rbp
        .cfi_def_cfa_offset 16
        .cfi_offset %rbp, -16
        movq    %rsp, %rbp
        .cfi_def_cfa_register %rbp
        pushq   %rbx
        .cfi_offset %rbx, -24
        subq    $8, %rsp                /* rsp is now 16-byte aligned */

        movq    %rdi, %rbx              /* rbx = frame (callee-saved) */

        /* reserve and fill the outgoing stack arguments */
        movq    8(%rbx), %rax           /* stack_size, multiple of 16 */
        subq    %rax, %rsp
        movq    16(%rbx), %r10          /* source image */
        xorl    %r11d, %r11d
1:
        cmpq    %rax, %r11
        jae     2f
        movq    (%r10,%r11), %rdx
        movq    %rdx, (%rsp,%r11)
        addq    $8, %r11
        jmp     1b
2:
        /* argument registers */
        movsd   88(%rbx), %xmm0
        movsd   96(%rbx), %xmm1
        movsd   104(%rbx), %xmm2
        movsd   112(%rbx), %xmm3
        movsd   120(%rbx), %xmm4
        movsd   128(%rbx), %xmm5
        movsd   136(%rbx), %xmm6
        movsd   144(%rbx), %xmm7
        movq    40(%rbx), %rdi
        movq    48(%rbx), %rsi
        movq    56(%rbx), %rdx
        movq    64(%rbx), %rcx
        movq    72(%rbx), %r8
        movq    80(%rbx), %r9

        callq   *(%rbx)

        movq    %rax, 24(%rbx)
        movsd   %xmm0, 32(%rbx)

        leaq    -8(%rbp), %rsp
        popq    %rbx
        .cfi_restore %rbx
        popq    %rbp
        .cfi_restore %rbp
        .cfi_def_cfa %rsp, 8
        ret
        .cfi_endproc
        .size   callconv_internal_call, .-callconv_internal_call

/*-------------------------------------------------------------------
 * callconv_internal_spill: typed entry for any signature.
 * rdi holds the closure (first argument).
 *
 * Frame (136 bytes below the return address, rsp 16-byte aligned):
 *   [rsp + 0 .. 127]   spill_tray
 *   [rsp + 128]        padding
 *   [rsp + 136]        return address
 *   [rsp + 144 ..]     caller stack arguments
 *-------------------------------------------------------------------*/
        .p2align 4
        .globl  callconv_internal_spill
        .hidden callconv_internal_spill
        .type   callconv_internal_spill, @function
callconv_internal_spill:
        .cfi_startproc
        endbr64
        subq    $136, %rsp
        .cfi_def_cfa_offset 144

        movq    %rdi, 16(%rsp)
        movq    %rsi, 24(%rsp)
        movq    %rdx, 32(%rsp)
        movq    %rcx, 40(%rsp)
        movq    %r8, 48(%rsp)
        movq    %r9, 56(%rsp)
        movsd   %xmm0, 64(%rsp)
        movsd   %xmm1, 72(%rsp)
        movsd   %xmm2, 80(%rsp)
        movsd   %xmm3, 88(%rsp)
        movsd   %xmm4, 96(%rsp)
        movsd   %xmm5, 104(%rsp)
        movsd   %xmm6, 112(%rsp)
        movsd   %xmm7, 120(%rsp)

        movq    %rsp, %rdi              /* tray */
        leaq    144(%rsp), %rsi         /* caller stack arguments */
        call    callconv_internal_spill_dispatch@PLT

        movq    0(%rsp), %rax
        movsd   8(%rsp), %xmm0

        addq    $136, %rsp
        .cfi_def_cfa_offset 8
        ret
        .cfi_endproc
        .size   callconv_internal_spill, .-callconv_internal_spill

/*-------------------------------------------------------------------
 * GNU property note: IBT (bit 0) and SHSTK (bit 1)
 *-------------------------------------------------------------------*/
        .section .note.gnu.property, "a"
        .p2align 3
        .long   4                       /* name size */
        .long   16                      /* descriptor size */
        .long   5                       /* NT_GNU_PROPERTY_TYPE_0 */
        .asciz  "GNU"
        .long   0xc0000002              /* GNU_PROPERTY_X86_FEATURE_1_AND */
        .long   4                       /* data size */
        .long   3                       /* IBT | SHSTK */
        .long   0                       /* padding */

        .section .note.GNU-stack, "", @progbits
