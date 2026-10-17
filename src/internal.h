/*****************************************************************/ /**
 * @file   internal.h
 * @brief  Internal: signature layout and the contract with the assembly.
 *
 * The structs `call_frame` and `spill_tray` are read and written by the
 * assembly files at hard-coded offsets. Any change here must be mirrored
 * in every file under src/asm, the static_asserts below document the
 * offsets they rely on.
 *********************************************************************/
#ifndef CALLCONV_SRC_INTERNAL_H
#define CALLCONV_SRC_INTERNAL_H

#include <callconv/callconv.h>
#include "platform.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

static_assert(sizeof(void*) == 8, "callconv: only 64-bit targets are supported");
static_assert(
    std::endian::native == std::endian::little,
    "callconv: only little-endian targets are supported");
static_assert(sizeof(float) == 4 && sizeof(double) == 8);
static_assert(sizeof(callconv_retval) == 8 && alignof(callconv_retval) == 8);

namespace callconv::detail
{
  /***********************/
  // ABI PARAMETERS
  /***********************/

#if CALLCONV_ABI_X86_64_SYSV
  /// @brief Number of integer argument registers
  inline constexpr std::uint32_t NGPR = 6;
  /// @brief Number of floating point argument registers
  inline constexpr std::uint32_t NFPR = 8;
#elif CALLCONV_ABI_X86_64_WIN64
  inline constexpr std::uint32_t NGPR = 4;
  inline constexpr std::uint32_t NFPR = 4;
#else // all arm64 variants
  inline constexpr std::uint32_t NGPR = 8;
  inline constexpr std::uint32_t NFPR = 8;
#endif

  /***********************/
  // SIGNATURE LAYOUT
  /***********************/

  /// @brief Location kinds of a parameter
  enum : std::uint8_t
  {
    /// @brief In an integer register: `offset` is the byte offset in the register array
    LOC_GPR = 0,
    /// @brief In a floating point register: `offset` is the byte offset in the register array
    LOC_FPR = 1,
    /// @brief On the stack: `offset` is the byte offset from the start of the stack arguments
    LOC_STACK = 2,
  };

  /// @brief Precomputed information about one parameter
  struct param_entry
  {
    /// @brief The class of the parameter
    std::uint8_t cls;
    /// @brief LOC_*
    std::uint8_t loc;
    /// @brief Size in bytes of the value (1, 2, 4 or 8)
    std::uint8_t size;
    std::uint8_t _pad;
    /// @brief Byte offset, meaning depends on `loc`
    std::uint32_t offset;
  };
  static_assert(sizeof(param_entry) == 8, "must match CALLCONV_SIGNATURE_SIZE");

  /// @brief Marks initialized signatures
  inline constexpr std::uint32_t SIGNATURE_MAGIC = 0x56434343u; // "CCCV"

  /// @brief Header of every signature, followed by `param_count` param_entry
  struct signature_header
  {
    std::uint32_t magic;
    std::uint32_t param_count;
    /// @brief Bytes of outgoing stack arguments to reserve for a call
    /// (multiple of 16, includes the Win64 shadow space)
    std::uint32_t stack_size;
    std::uint8_t ret_cls;
    std::uint8_t abi_id;
    std::uint8_t _pad[2];
  };
  static_assert(sizeof(signature_header) == 16, "must match CALLCONV_SIGNATURE_SIZE");
  static_assert(CALLCONV_SIGNATURE_SIZE(3) == sizeof(signature_header) + 3 * sizeof(param_entry));

  inline const signature_header& header_of(const callconv_signature* sig) noexcept
  {
    return *reinterpret_cast<const signature_header*>(sig);
  }
  inline const param_entry* params_of(const callconv_signature* sig) noexcept
  {
    return reinterpret_cast<const param_entry*>(
        reinterpret_cast<const unsigned char*>(sig) + sizeof(signature_header));
  }

  /// @brief Size in bytes of a value of a class (0 for void)
  constexpr std::uint8_t class_size(callconv_class c) noexcept
  {
    switch (c)
    {
    case CALLCONV_CLASS_BOOL:
    case CALLCONV_CLASS_I8:
    case CALLCONV_CLASS_U8:
      return 1;
    case CALLCONV_CLASS_I16:
    case CALLCONV_CLASS_U16:
      return 2;
    case CALLCONV_CLASS_I32:
    case CALLCONV_CLASS_U32:
    case CALLCONV_CLASS_F32:
      return 4;
    case CALLCONV_CLASS_I64:
    case CALLCONV_CLASS_U64:
    case CALLCONV_CLASS_F64:
    case CALLCONV_CLASS_PTR:
      return 8;
    default:
      return 0;
    }
  }

  constexpr bool is_float_class(callconv_class c) noexcept
  {
    return c == CALLCONV_CLASS_F32 || c == CALLCONV_CLASS_F64;
  }

  /// @brief Loads a value of class `c` from `p` and returns it extended to 64 bits:
  /// integers are sign/zero extended, bool is normalized to 0/1, float has
  /// its upper 32 bits cleared.
  inline std::uint64_t load_extended(callconv_class c, const void* p) noexcept
  {
    switch (c)
    {
    case CALLCONV_CLASS_BOOL:
    {
      std::uint8_t v;
      std::memcpy(&v, p, 1);
      return v != 0 ? 1u : 0u;
    }
    case CALLCONV_CLASS_I8:
    {
      std::int8_t v;
      std::memcpy(&v, p, 1);
      return (std::uint64_t)(std::int64_t)v;
    }
    case CALLCONV_CLASS_U8:
    {
      std::uint8_t v;
      std::memcpy(&v, p, 1);
      return v;
    }
    case CALLCONV_CLASS_I16:
    {
      std::int16_t v;
      std::memcpy(&v, p, 2);
      return (std::uint64_t)(std::int64_t)v;
    }
    case CALLCONV_CLASS_U16:
    {
      std::uint16_t v;
      std::memcpy(&v, p, 2);
      return v;
    }
    case CALLCONV_CLASS_I32:
    {
      std::int32_t v;
      std::memcpy(&v, p, 4);
      return (std::uint64_t)(std::int64_t)v;
    }
    case CALLCONV_CLASS_U32:
    case CALLCONV_CLASS_F32:
    {
      std::uint32_t v;
      std::memcpy(&v, p, 4);
      return v;
    }
    case CALLCONV_CLASS_I64:
    case CALLCONV_CLASS_U64:
    case CALLCONV_CLASS_F64:
    case CALLCONV_CLASS_PTR:
    {
      std::uint64_t v;
      std::memcpy(&v, p, 8);
      return v;
    }
    default:
      return 0;
    }
  }

  /***********************/
  // CONTRACT WITH THE ASSEMBLY
  /***********************/

  /// @brief Filled by C++ and consumed by `callconv_internal_call`.
  struct call_frame
  {
    /// @brief The function to call
    callconv_fn fn;
    /// @brief Bytes to copy from `stack` to the outgoing argument area (multiple of 16)
    std::uint64_t stack_size;
    /// @brief Image of the outgoing stack arguments
    const unsigned char* stack;
    /// @brief Written by the assembly: integer return register
    std::uint64_t ret_gpr;
    /// @brief Written by the assembly: floating point return register (low 64 bits)
    std::uint64_t ret_fpr;
    /// @brief Loaded into the integer argument registers
    std::uint64_t gpr[NGPR];
    /// @brief Loaded into the floating point argument registers (low 64 bits)
    std::uint64_t fpr[NFPR];
  };
  static_assert(offsetof(call_frame, fn) == 0);
  static_assert(offsetof(call_frame, stack_size) == 8);
  static_assert(offsetof(call_frame, stack) == 16);
  static_assert(offsetof(call_frame, ret_gpr) == 24);
  static_assert(offsetof(call_frame, ret_fpr) == 32);
  static_assert(offsetof(call_frame, gpr) == 40);
  static_assert(offsetof(call_frame, fpr) == 40 + 8 * NGPR);

  /// @brief Written by `callconv_internal_spill`, read by the dispatcher.
  struct spill_tray
  {
    /// @brief Written by the dispatcher, loaded into the integer return register
    std::uint64_t ret_gpr;
    /// @brief Written by the dispatcher, loaded into the floating point return register
    std::uint64_t ret_fpr;
    /// @brief Saved integer argument registers
    std::uint64_t gpr[NGPR];
    /// @brief Saved floating point argument registers (low 64 bits)
    std::uint64_t fpr[NFPR];
  };
  static_assert(offsetof(spill_tray, ret_gpr) == 0);
  static_assert(offsetof(spill_tray, ret_fpr) == 8);
  static_assert(offsetof(spill_tray, gpr) == 16);
  static_assert(offsetof(spill_tray, fpr) == 16 + 8 * NGPR);
} // namespace callconv::detail

extern "C"
{
  /// @brief Assembly: loads the registers from `frame`, copies the stack
  /// arguments, calls `frame->fn` and stores the return registers.
  CALLCONV_HIDDEN void callconv_internal_call(callconv::detail::call_frame* frame);

  /// @brief Assembly: the universal typed entry. Never called directly from C++.
  CALLCONV_HIDDEN void callconv_internal_spill(void);

  /// @brief Called by `callconv_internal_spill` with the saved registers and
  /// the address of the caller stack arguments.
  CALLCONV_HIDDEN void callconv_internal_spill_dispatch(
      callconv::detail::spill_tray* tray, const unsigned char* stack_args) noexcept;
}

#endif // !CALLCONV_SRC_INTERNAL_H
