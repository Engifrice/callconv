;*****************************************************************
; @file   x86_64_ms_pe_masm.asm
; @brief  Microsoft x64 calling convention, PE/COFF, MASM (ml64) syntax.
;         Targets: Windows x86_64 and Xbox with MSVC.
;         (src/asm/x86_64_ms_pe_gas.S is the same code for clang-cl.)
;
; Arguments are positional: argument i (i < 4) is in RCX/RDX/R8/R9 or in
; XMM0-3 depending on its type. The caller always reserves a 32-byte home
; area above the return address; argument i >= 4 is at [home + 8 * i].
; Integer return: rax. Float return: xmm0.
;
; Offsets must match call_frame and spill_tray in src/internal.h:
;   call_frame: fn 0, stack_size 8, stack 16, ret_gpr 24, ret_fpr 32,
;               gpr[4] 40..64, fpr[4] 72..96
;   spill_tray: ret_gpr 0, ret_fpr 8, gpr[4] 16..40, fpr[4] 48..72
;*****************************************************************

EXTERN callconv_internal_spill_dispatch:PROC

PUBLIC callconv_internal_call
PUBLIC callconv_internal_spill

.code

;-------------------------------------------------------------------
; void callconv_internal_call(call_frame* frame)
;-------------------------------------------------------------------
ALIGN 16
callconv_internal_call PROC FRAME
        push    rbp
        .pushreg rbp
        push    rbx
        .pushreg rbx
        sub     rsp, 8
        .allocstack 8
        mov     rbp, rsp                ; rbp is 16-byte aligned
        .setframe rbp, 0
        .endprolog

        mov     rbx, rcx                ; rbx = frame (callee-saved)

        ; reserve and fill the outgoing stack arguments (includes the home area)
        mov     rax, QWORD PTR [rbx+8]  ; stack_size, multiple of 16, >= 32
        sub     rsp, rax
        mov     r10, QWORD PTR [rbx+16]
        xor     r11d, r11d
copy_loop:
        cmp     r11, rax
        jae     copy_done
        mov     rdx, QWORD PTR [r10+r11]
        mov     QWORD PTR [rsp+r11], rdx
        add     r11, 8
        jmp     copy_loop
copy_done:
        movsd   xmm0, QWORD PTR [rbx+72]
        movsd   xmm1, QWORD PTR [rbx+80]
        movsd   xmm2, QWORD PTR [rbx+88]
        movsd   xmm3, QWORD PTR [rbx+96]
        mov     rcx, QWORD PTR [rbx+40]
        mov     rdx, QWORD PTR [rbx+48]
        mov     r8,  QWORD PTR [rbx+56]
        mov     r9,  QWORD PTR [rbx+64]

        call    QWORD PTR [rbx]

        mov     QWORD PTR [rbx+24], rax
        movsd   QWORD PTR [rbx+32], xmm0

        lea     rsp, [rbp+8]
        pop     rbx
        pop     rbp
        ret
callconv_internal_call ENDP

;-------------------------------------------------------------------
; callconv_internal_spill: typed entry for any signature.
; rcx holds the closure (first argument).
;
; Frame (120 bytes below the return address, rsp 16-byte aligned):
;   [rsp + 0 .. 31]     home area for the call to the dispatcher
;   [rsp + 32 .. 111]   spill_tray
;   [rsp + 112]         padding
;   [rsp + 120]         return address
;   [rsp + 128 ..]      caller home area, then stack arguments
;-------------------------------------------------------------------
ALIGN 16
callconv_internal_spill PROC FRAME
        sub     rsp, 120
        .allocstack 120
        .endprolog

        mov     QWORD PTR [rsp+48], rcx
        mov     QWORD PTR [rsp+56], rdx
        mov     QWORD PTR [rsp+64], r8
        mov     QWORD PTR [rsp+72], r9
        movsd   QWORD PTR [rsp+80], xmm0
        movsd   QWORD PTR [rsp+88], xmm1
        movsd   QWORD PTR [rsp+96], xmm2
        movsd   QWORD PTR [rsp+104], xmm3

        lea     rcx, [rsp+32]           ; tray
        lea     rdx, [rsp+128]          ; caller home area = base of stack arguments
        call    callconv_internal_spill_dispatch

        mov     rax, QWORD PTR [rsp+32]
        movsd   xmm0, QWORD PTR [rsp+40]

        add     rsp, 120
        ret
callconv_internal_spill ENDP

END
