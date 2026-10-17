/*****************************************************************/ /**
 * @file   arm64_aapcs_elf_macho_gas.S
 * @brief  AAPCS64 calling convention, ELF and Mach-O, GNU assembler syntax.
 *         Targets: Linux arm64, Android, (Switch), macOS and iOS arm64.
 *
 * Integer arguments: x0-x7. Float arguments: v0-v7 (d0-d7 / s0-s7).
 * Integer return: x0. Float return: v0.
 * The register usage is identical on Apple platforms; only the layout of
 * stack arguments differs, which is handled in C++ (src/signature.cpp).
 *
 * Offsets must match `call_frame` and `spill_tray` in src/internal.h:
 *   call_frame: fn 0, stack_size 8, stack 16, ret_gpr 24, ret_fpr 32,
 *               gpr[8] 40..96, fpr[8] 104..160
 *   spill_tray: ret_gpr 0, ret_fpr 8, gpr[8] 16..72, fpr[8] 80..136
 *
 * Hardening (ELF only): every entry starts with BTI c and signs the return
 * address with PAC (both are NOPs on hardware without the features), and
 * the object carries the BTI, PAC and GCS GNU properties, so linking
 * callconv never disables these features for the final binary.
 *********************************************************************/

#if defined(__APPLE__)
  #define CC_SYM(name)        _##name
  #define CC_HIDDEN(name)     .private_extern _##name
  #define CC_TYPE(name)
  #define CC_SIZE(name)
  #define CC_BTI_C
  #define CC_PAC_SIGN
  #define CC_PAC_AUTH
  #define CC_PAC_CFI
#else
  #define CC_SYM(name)        name
  #define CC_HIDDEN(name)     .hidden name
  #define CC_TYPE(name)       .type name, %function
  #define CC_SIZE(name)       .size name, .-name
  /* hint encodings: accepted by assemblers targeting any armv8 level */
  #define CC_BTI_C            hint #34
  #define CC_PAC_SIGN         hint #25
  #define CC_PAC_AUTH         hint #29
  #define CC_PAC_CFI          .cfi_negate_ra_state
#endif

        .text

/*-------------------------------------------------------------------
 * void callconv_internal_call(call_frame* frame)
 *-------------------------------------------------------------------*/
        .p2align 2
        .globl  CC_SYM(callconv_internal_call)
        CC_HIDDEN(callconv_internal_call)
        CC_TYPE(callconv_internal_call)
CC_SYM(callconv_internal_call):
        .cfi_startproc
        CC_BTI_C
        CC_PAC_SIGN
        CC_PAC_CFI
        stp     x29, x30, [sp, #-32]!
        .cfi_def_cfa_offset 32
        .cfi_offset x29, -32
        .cfi_offset x30, -24
        str     x19, [sp, #16]
        .cfi_offset x19, -16
        mov     x29, sp
        .cfi_def_cfa x29, 32

        mov     x19, x0                 /* x19 = frame (callee-saved) */

        /* reserve and fill the outgoing stack arguments */
        ldr     x9, [x19, #8]           /* stack_size, multiple of 16 */
        sub     sp, sp, x9
        ldr     x10, [x19, #16]         /* source image */
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
        ldr     x16, [x19, #0]          /* fn */
        ldp     x0, x1, [x19, #40]
        ldp     x2, x3, [x19, #56]
        ldp     x4, x5, [x19, #72]
        ldp     x6, x7, [x19, #88]

        blr     x16

        str     x0, [x19, #24]
        str     d0, [x19, #32]

        mov     sp, x29
        .cfi_def_cfa sp, 32
        ldr     x19, [sp, #16]
        .cfi_restore x19
        ldp     x29, x30, [sp], #32
        .cfi_def_cfa_offset 0
        .cfi_restore x29
        .cfi_restore x30
        CC_PAC_AUTH
        ret
        .cfi_endproc
        CC_SIZE(callconv_internal_call)

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
        .p2align 2
        .globl  CC_SYM(callconv_internal_spill)
        CC_HIDDEN(callconv_internal_spill)
        CC_TYPE(callconv_internal_spill)
CC_SYM(callconv_internal_spill):
        .cfi_startproc
        CC_BTI_C
        CC_PAC_SIGN
        CC_PAC_CFI
        stp     x29, x30, [sp, #-160]!
        .cfi_def_cfa_offset 160
        .cfi_offset x29, -160
        .cfi_offset x30, -152
        mov     x29, sp
        .cfi_def_cfa x29, 160

        stp     x0, x1, [sp, #32]
        stp     x2, x3, [sp, #48]
        stp     x4, x5, [sp, #64]
        stp     x6, x7, [sp, #80]
        stp     d0, d1, [sp, #96]
        stp     d2, d3, [sp, #112]
        stp     d4, d5, [sp, #128]
        stp     d6, d7, [sp, #144]

        add     x0, sp, #16             /* tray */
        add     x1, sp, #160            /* caller stack arguments */
        bl      CC_SYM(callconv_internal_spill_dispatch)

        ldr     x0, [sp, #16]
        ldr     d0, [sp, #24]

        .cfi_def_cfa sp, 160
        ldp     x29, x30, [sp], #160
        .cfi_def_cfa_offset 0
        .cfi_restore x29
        .cfi_restore x30
        CC_PAC_AUTH
        ret
        .cfi_endproc
        CC_SIZE(callconv_internal_spill)

#if !defined(__APPLE__)
/*-------------------------------------------------------------------
 * GNU property note: BTI (bit 0), PAC (bit 1) and GCS (bit 2)
 *
 * GCS (Guarded Control Stack) checks every return against a shadow stack
 * of return addresses. Both routines only return through the x30 pushed
 * by their caller's BL (saved and restored unchanged), so they are
 * compatible. Compilers emit the GCS bit for -mbranch-protection=standard
 * (Clang 18+, GCC 15+): without it here, linking callconv would disable
 * GCS for the whole binary.
 *-------------------------------------------------------------------*/
        .section .note.gnu.property, "a"
        .p2align 3
        .long   4                       /* name size */
        .long   16                      /* descriptor size */
        .long   5                       /* NT_GNU_PROPERTY_TYPE_0 */
        .asciz  "GNU"
        .long   0xc0000000              /* GNU_PROPERTY_AARCH64_FEATURE_1_AND */
        .long   4                       /* data size */
        .long   7                       /* BTI | PAC | GCS */
        .long   0                       /* padding */

        .section .note.GNU-stack, "", %progbits
#endif