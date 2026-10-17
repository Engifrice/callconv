/*****************************************************************/ /**
 * @file   arm64_aapcs_pe_gas.S
 * @brief  Windows arm64 calling convention, PE/COFF, GNU assembler syntax.
 *         Targets: Windows arm64 with clang-cl.
 *         (src/asm/arm64_aapcs_pe_armasm.asm is the same code for MSVC.)
 *
 * For non-variadic functions, Windows arm64 follows AAPCS64:
 * integer arguments x0-x7, float arguments v0-v7, stack arguments in
 * 8-byte slots. Integer return: x0. Float return: v0. x18 is reserved
 * by the platform and never touched.
 *
 * Offsets must match `call_frame` and `spill_tray` in src/internal.h:
 *   call_frame: fn 0, stack_size 8, stack 16, ret_gpr 24, ret_fpr 32,
 *               gpr[8] 40..96, fpr[8] 104..160
 *   spill_tray: ret_gpr 0, ret_fpr 8, gpr[8] 16..72, fpr[8] 80..136
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
        .p2align 2
callconv_internal_call:
        .seh_proc callconv_internal_call
        stp     x29, x30, [sp, #-32]!
        .seh_save_fplr_x 32
        str     x19, [sp, #16]
        .seh_save_reg x19, 16
        mov     x29, sp
        .seh_set_fp
        .seh_endprologue

        mov     x19, x0                 // x19 = frame (callee-saved)

        // reserve and fill the outgoing stack arguments
        ldr     x9, [x19, #8]           // stack_size, multiple of 16
        sub     sp, sp, x9
        ldr     x10, [x19, #16]         // source image
        mov     x11, #0
1:
        cmp     x11, x9
        b.hs    2f
        ldr     x12, [x10, x11]
        str     x12, [sp, x11]
        add     x11, x11, #8
        b       1b
2:
        ldp     d0, d1, [x19, #104]
        ldp     d2, d3, [x19, #120]
        ldp     d4, d5, [x19, #136]
        ldp     d6, d7, [x19, #152]
        ldr     x16, [x19, #0]          // fn
        ldp     x0, x1, [x19, #40]
        ldp     x2, x3, [x19, #56]
        ldp     x4, x5, [x19, #72]
        ldp     x6, x7, [x19, #88]

        blr     x16

        str     x0, [x19, #24]
        str     d0, [x19, #32]

        .seh_startepilogue
        mov     sp, x29
        .seh_set_fp
        ldr     x19, [sp, #16]
        .seh_save_reg x19, 16
        ldp     x29, x30, [sp], #32
        .seh_save_fplr_x 32
        .seh_endepilogue
        ret
        .seh_endproc

/*-------------------------------------------------------------------
 * callconv_internal_spill: typed entry for any signature.
 * x0 holds the closure (first argument).
 *
 * Frame (160 bytes, sp 16-byte aligned):
 *   [sp + 0]           saved x29
 *   [sp + 8]           saved x30
 *   [sp + 16 .. 159]   spill_tray
 *   [sp + 160 ..]      caller stack arguments
 *-------------------------------------------------------------------*/
        .globl  callconv_internal_spill
        .def    callconv_internal_spill
        .scl    2
        .type   32
        .endef
        .p2align 2
callconv_internal_spill:
        .seh_proc callconv_internal_spill
        stp     x29, x30, [sp, #-160]!
        .seh_save_fplr_x 160
        mov     x29, sp
        .seh_set_fp
        .seh_endprologue

        stp     x0, x1, [sp, #32]
        stp     x2, x3, [sp, #48]
        stp     x4, x5, [sp, #64]
        stp     x6, x7, [sp, #80]
        stp     d0, d1, [sp, #96]
        stp     d2, d3, [sp, #112]
        stp     d4, d5, [sp, #128]
        stp     d6, d7, [sp, #144]

        add     x0, sp, #16             // tray
        add     x1, sp, #160            // caller stack arguments
        bl      callconv_internal_spill_dispatch

        ldr     x0, [sp, #16]
        ldr     d0, [sp, #24]

        .seh_startepilogue
        mov     sp, x29
        .seh_set_fp
        ldp     x29, x30, [sp], #160
        .seh_save_fplr_x 160
        .seh_endepilogue
        ret
        .seh_endproc
