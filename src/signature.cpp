/*****************************************************************/ /**
 * @file   signature.cpp
 * @brief  Signature construction: assigns each parameter to a register
 *         or a stack offset according to the target calling convention.
 *********************************************************************/
#include "internal.h"

#include <new>

using namespace callconv::detail;

namespace
{
  callconv_signature const* fail(callconv_result* out, callconv_result r) noexcept
  {
    if (out)
      *out = r;
    return nullptr;
  }

  constexpr std::uint64_t align_up(std::uint64_t v, std::uint64_t a) noexcept
  {
    return (v + (a - 1)) & ~(a - 1);
  }
} // namespace

extern "C" const callconv_signature* callconv_signature_init(
    void* storage, const callconv_class* params, uint32_t param_count,
    callconv_class ret, callconv_result* out_result) noexcept
{
  if (storage == nullptr || (param_count != 0 && params == nullptr))
    return fail(out_result, CALLCONV_ERR_NULL);
  if (reinterpret_cast<std::uintptr_t>(storage) % CALLCONV_SIGNATURE_ALIGN != 0)
    return fail(out_result, CALLCONV_ERR_MISALIGNED);
  if (ret >= CALLCONV_CLASS_COUNT)
    return fail(out_result, CALLCONV_ERR_INVALID_CLASS);
  for (std::uint32_t i = 0; i < param_count; ++i)
  {
    if (params[i] >= CALLCONV_CLASS_COUNT)
      return fail(out_result, CALLCONV_ERR_INVALID_CLASS);
    if (params[i] == CALLCONV_CLASS_VOID)
      return fail(out_result, CALLCONV_ERR_VOID_PARAM);
  }

  // Note: on failure past this point (CALLCONV_ERR_TOO_LARGE), the content
  // of the storage is unspecified.
  auto* const base   = static_cast<unsigned char*>(storage);
  auto* const entries = reinterpret_cast<param_entry*>(base + sizeof(signature_header));

  std::uint32_t used_gpr = 0;
  std::uint32_t used_fpr = 0;
  std::uint64_t stack    = 0; // bytes of stack arguments used so far
  (void)used_gpr;
  (void)used_fpr;

  for (std::uint32_t i = 0; i < param_count; ++i)
  {
    const callconv_class c = params[i];
    const std::uint8_t size = class_size(c);
    const bool fp           = is_float_class(c);

    param_entry e{};
    e.cls  = c;
    e.size = size;

#if CALLCONV_ABI_X86_64_WIN64
    // Positional: argument i uses slot i, either RCX/RDX/R8/R9 or XMM0-3.
    // Arguments past the 4th go on the stack, at 8 * i bytes from the start
    // of the home (shadow) area that the caller reserves for the first 4.
    if (i < NGPR)
    {
      e.loc    = fp ? LOC_FPR : LOC_GPR;
      e.offset = 8 * i;
    }
    else
    {
      e.loc = LOC_STACK;
      stack = 8 * (std::uint64_t)i;
      if (stack > 0xFFFFFFFFull)
        return fail(out_result, CALLCONV_ERR_TOO_LARGE);
      e.offset = (std::uint32_t)stack;
      stack += 8;
    }
#else
    // Integers and floats consume their own register banks independently.
    if (fp && used_fpr < NFPR)
    {
      e.loc    = LOC_FPR;
      e.offset = 8 * used_fpr++;
    }
    else if (!fp && used_gpr < NGPR)
    {
      e.loc    = LOC_GPR;
      e.offset = 8 * used_gpr++;
    }
    else
    {
      e.loc = LOC_STACK;
  #if CALLCONV_ABI_ARM64_APPLE
      // Apple arm64: stack arguments use their natural size and alignment.
      stack = align_up(stack, size);
      if (stack > 0xFFFFFFFFull)
        return fail(out_result, CALLCONV_ERR_TOO_LARGE);
      e.offset = (std::uint32_t)stack;
      stack += size;
  #else
      // System V x64 and AAPCS64: every stack argument takes an 8-byte slot.
      if (stack > 0xFFFFFFFFull)
        return fail(out_result, CALLCONV_ERR_TOO_LARGE);
      e.offset = (std::uint32_t)stack;
      stack += 8;
  #endif
    }
#endif
    // The storage may legitimately be reused: entries are written in place.
    ::new (static_cast<void*>(&entries[i])) param_entry(e);
  }

#if CALLCONV_ABI_X86_64_WIN64
  // The caller always reserves the 32-byte home area, even with fewer
  // than 4 arguments.
  if (stack < 32)
    stack = 32;
#endif
  stack = align_up(stack, 16);
  if (stack > 0xFFFFFFFFull)
    return fail(out_result, CALLCONV_ERR_TOO_LARGE);

  signature_header h{};
  h.magic       = SIGNATURE_MAGIC;
  h.param_count = param_count;
  h.stack_size  = (std::uint32_t)stack;
  h.ret_cls     = ret;
  h.abi_id      = (std::uint8_t)CALLCONV_ABI_ID;
  ::new (static_cast<void*>(base)) signature_header(h);

  if (out_result)
    *out_result = CALLCONV_OK;
  return reinterpret_cast<const callconv_signature*>(base);
}

extern "C" uint32_t callconv_signature_param_count(const callconv_signature* sig) noexcept
{
  return sig ? header_of(sig).param_count : 0;
}

extern "C" callconv_class callconv_signature_param(
    const callconv_signature* sig, uint32_t index) noexcept
{
  if (sig == nullptr || index >= header_of(sig).param_count)
    return CALLCONV_CLASS_VOID;
  return params_of(sig)[index].cls;
}

extern "C" callconv_class callconv_signature_return(const callconv_signature* sig) noexcept
{
  return sig ? header_of(sig).ret_cls : (callconv_class)CALLCONV_CLASS_VOID;
}

extern "C" uint32_t callconv_version(void) noexcept
{
  return CALLCONV_VERSION;
}

extern "C" const char* callconv_abi_name(void) noexcept
{
  return CALLCONV_ABI_NAME;
}
