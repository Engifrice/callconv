/*****************************************************************/ /**
 * @file   x86_64_ms_pe_gas.S
 * @brief  Microsoft x64 calling convention, PE/COFF, GNU assembler syntax.
 *         Targets: Windows x86_64 and Xbox with clang-cl.
 *         (src/asm/x86_64_ms_pe_masm.asm is the same code for MSVC.)
 *
 * Arguments are positional: argument i (i < 4) is in RCX/RDX/R8/R9 or in
 * XMM0-3 depending on its type. The caller always reserves a 32-byte home
 * area above the return address; argument i >= 4 is at [home + 8 * i].
 * Integer return: rax. Float return: xmm0.
 *
 * Offsets must match `call_frame` and `spill_tray` in src/internal.h:
 *   call_frame: fn 0, stack_size 8, stack 16, ret_gpr 24, ret_fpr 32,
 *               gpr[4] 40..64, fpr[4] 72..96
 *   spill_tray: ret_gpr 0, ret_fpr 8, gpr[4] 16..40, fpr[4] 48..72
 *
 * Unwind information uses the .seh_* directives.
 *********************************************************************/

        .text

/*-------------------------------------------------------------------
 * void callconv_internal_call(call_frame* frame)
 *-------------------------------------------------------------------*/
        .globl  callconv_internal_call
        .def    callconv_internal_call
        .scl    2
        .type   32
        .endef
        .p2align 4
callconv_internal_call:
        .seh_proc callconv_internal_call
        pushq   %rbp
        .seh_pushreg %rbp
        pushq   %rbx
        .seh_pushreg %rbx
        subq    $8, %rsp
        .seh_stackalloc 8
        movq    %rsp, %rbp              /* rbp is 16-byte aligned */
        .seh_setframe %rbp, 0
        .seh_endprologue

        movq    %rcx, %rbx              /* rbx = frame (callee-saved) */

        /* reserve and fill the outgoing stack arguments (includes the home area) */
        movq    8(%rbx), %rax           /* stack_size, multiple of 16, >= 32 */
        subq    %rax, %rsp
        movq    16(%rbx), %r10
        xorl    %r11d, %r11d
1:
        cmpq    %rax, %r11
        jae     2f
        movq    (%r10,%r11), %rdx
        movq    %rdx, (%rsp,%r11)
        addq    $8, %r11
        jmp     1b
2:
        movsd   72(%rbx), %xmm0
        movsd   80(%rbx), %xmm1
        movsd   88(%rbx), %xmm2
        movsd   96(%rbx), %xmm3
        movq    40(%rbx), %rcx
        movq    48(%rbx), %rdx
        movq    56(%rbx), %r8
        movq    64(%rbx), %r9

        callq   *(%rbx)

        movq    %rax, 24(%rbx)
        movsd   %xmm0, 32(%rbx)

        leaq    8(%rbp), %rsp
        popq    %rbx
        popq    %rbp
        retq
        .seh_endproc

/*-------------------------------------------------------------------
 * callconv_internal_spill: typed entry for any signature.
 * rcx holds the closure (first argument).
 *
 * Frame (120 bytes below the return address, rsp 16-byte aligned):
 *   [rsp + 0 .. 31]     home area for the call to the dispatcher
 *   [rsp + 32 .. 111]   spill_tray
 *   [rsp + 112]         padding
 *   [rsp + 120]         return address
 *   [rsp + 128 ..]      caller home area, then stack arguments
 *-------------------------------------------------------------------*/
        .globl  callconv_internal_spill
        .def    callconv_internal_spill
        .scl    2
        .type   32
        .endef
        .p2align 4
callconv_internal_spill:
        .seh_proc callconv_internal_spill
        subq    $120, %rsp
        .seh_stackalloc 120
        .seh_endprologue

        movq    %rcx, 48(%rsp)
        movq    %rdx, 56(%rsp)
        movq    %r8, 64(%rsp)
        movq    %r9, 72(%rsp)
        movsd   %xmm0, 80(%rsp)
        movsd   %xmm1, 88(%rsp)
        movsd   %xmm2, 96(%rsp)
        movsd   %xmm3, 104(%rsp)

        leaq    32(%rsp), %rcx          /* tray */
        leaq    128(%rsp), %rdx         /* caller home area = base of stack arguments */
        callq   callconv_internal_spill_dispatch

        movq    32(%rsp), %rax
        movsd   40(%rsp), %xmm0

        addq    $120, %rsp
        retq
        .seh_endproc
