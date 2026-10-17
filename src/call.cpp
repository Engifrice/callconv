/*****************************************************************/ /**
 * @file   call.cpp
 * @brief  Dynamic call: builds the register image and the outgoing stack
 *         arguments from a signature, then lets the assembly do the call.
 *********************************************************************/
#include "internal.h"

using namespace callconv::detail;

extern "C" void callconv_call(
    const callconv_signature* sig, callconv_fn fn, void* const* args,
    callconv_retval* ret) noexcept
{
  const signature_header& h = header_of(sig);
  const param_entry* params = params_of(sig);

  call_frame f;
  f.fn         = fn;
  f.stack_size = h.stack_size;
  f.ret_gpr    = 0;
  f.ret_fpr    = 0;
  for (std::uint32_t i = 0; i < NGPR; ++i)
    f.gpr[i] = 0;
  for (std::uint32_t i = 0; i < NFPR; ++i)
    f.fpr[i] = 0;

  // Image of the outgoing stack arguments, copied by the assembly to the
  // bottom of its own frame right before the call.
  unsigned char* stack = nullptr;
  if (h.stack_size != 0)
    stack = static_cast<unsigned char*>(CALLCONV_ALLOCA(h.stack_size));
  f.stack = stack;

  for (std::uint32_t i = 0; i < h.param_count; ++i)
  {
    const param_entry& p = params[i];
    // Always extended to 64 bits: satisfies every convention that requires
    // the caller to extend small integers (Apple arm64, clang on x86_64),
    // and full 8-byte writes avoid store-forwarding stalls in the assembly.
    const std::uint64_t v = load_extended(p.cls, args[i]);
    switch (p.loc)
    {
    case LOC_GPR:
      f.gpr[p.offset / 8] = v;
      break;
    case LOC_FPR:
      f.fpr[p.offset / 8] = v;
      break;
    default:
#if CALLCONV_ABI_ARM64_APPLE
      // natural size: neighbours may share the same 8 bytes
      std::memcpy(stack + p.offset, &v, p.size);
#else
      std::memcpy(stack + p.offset, &v, 8);
#endif
      break;
    }
  }

  callconv_internal_call(&f);

  if (ret == nullptr)
    return;
  const callconv_class rc = h.ret_cls;
  if (rc == CALLCONV_CLASS_VOID)
    ret->u64 = 0;
  else if (is_float_class(rc))
    // load_extended clears the upper half of a float
    ret->u64 = load_extended(rc, &f.ret_fpr);
  else
    // the callee may leave garbage above the value: only its low bytes count
    ret->u64 = load_extended(rc, &f.ret_gpr);
}
