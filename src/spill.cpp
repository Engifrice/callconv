/*****************************************************************/ /**
 * @file   spill.cpp
 * @brief  Spill: the C++ half of the universal typed entry.
 *
 * `callconv_internal_spill` (assembly) saves every argument register into
 * a `spill_tray` and calls `callconv_internal_spill_dispatch`, which finds
 * the closure through the first argument, builds the `args` array from the
 * signature, calls the handler, and writes the extended result back into
 * the tray for the assembly to load into the return registers.
 *********************************************************************/
#include "internal.h"

using namespace callconv::detail;

extern "C" void callconv_internal_spill_dispatch(
    spill_tray* tray, const unsigned char* stack_args) noexcept
{
  // The first argument is always a pointer, hence always in the first
  // integer register (slot 0 on Win64, first GPR elsewhere).
  const auto* self = reinterpret_cast<const callconv_spill_closure*>(tray->gpr[0]);
  const callconv_signature* sig = self->sig;
  const signature_header& h     = header_of(sig);
  const param_entry* params     = params_of(sig);

  // param_count >= 1 is guaranteed by callconv_spill_entry
  auto** args = static_cast<void**>(CALLCONV_ALLOCA(sizeof(void*) * h.param_count));
  for (std::uint32_t i = 0; i < h.param_count; ++i)
  {
    const param_entry& p = params[i];
    switch (p.loc)
    {
    case LOC_GPR:
      // little-endian: the value is in the low bytes of the saved register
      args[i] = &tray->gpr[p.offset / 8];
      break;
    case LOC_FPR:
      args[i] = &tray->fpr[p.offset / 8];
      break;
    default:
      args[i] = const_cast<unsigned char*>(stack_args + p.offset);
      break;
    }
  }

  callconv_retval r;
  r.u64 = 0;
  self->handler(self, args, &r);

  // The handler only wrote the field of its class: extend it so that every
  // convention is satisfied (Apple arm64 requires the callee to extend
  // small integers, System V requires bool to be 0/1).
  const callconv_class rc = h.ret_cls;
  if (rc == CALLCONV_CLASS_VOID)
  {
    tray->ret_gpr = 0;
    tray->ret_fpr = 0;
  }
  else if (is_float_class(rc))
  {
    tray->ret_gpr = 0;
    tray->ret_fpr = load_extended(rc, &r);
  }
  else
  {
    tray->ret_gpr = load_extended(rc, &r);
    tray->ret_fpr = 0;
  }
}

extern "C" callconv_fn callconv_spill_entry(const callconv_signature* sig) noexcept
{
  if (sig == nullptr)
    return nullptr;
  const signature_header& h = header_of(sig);
  if (h.magic != SIGNATURE_MAGIC || h.param_count == 0
      || params_of(sig)[0].cls != CALLCONV_CLASS_PTR)
    return nullptr;
  // Taking the address here (in C++) also registers the entry as a valid
  // indirect call target for Windows Control Flow Guard.
  return &callconv_internal_spill;
}
