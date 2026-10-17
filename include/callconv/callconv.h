/*****************************************************************/ /**
 * @file   callconv.h
 * @brief  Low-level calling convention building blocks.
 *
 * `callconv` provides two primitives that bridge "typed" native calls
 * (arguments in CPU registers, as emitted by a compiler) and "generic"
 * calls (arguments described at runtime and passed as an array):
 *
 * - `callconv_call`: calls an ordinary compiled function whose
 *   signature is only known at runtime (a "dynamic call").
 * - `callconv_spill_entry`: returns machine code that can be called
 *   through a typed function pointer of ANY signature and forwards the
 *   call to a generic handler (a "spill").
 *
 * Both are driven by a `callconv_signature`, which describes the
 * parameter and return classes of a function and precomputes where each
 * value lives for the current platform calling convention.
 *
 * Design rules:
 * - Only scalars: bool, 8/16/32/64-bit integers, float, double, pointers.
 *   Aggregates must be passed by pointer. Variadic functions are not
 *   supported.
 * - Signatures are immutable once built and may be shared between threads.
 * - Functions called through `callconv_call` and handlers invoked through
 *   the spill entry must not throw or unwind through the library.
 * - 64-bit little-endian targets only (at least for now).
 *********************************************************************/
#ifndef CALLCONV_CALLCONV_H
#define CALLCONV_CALLCONV_H

#include <stddef.h>
#include <stdint.h>

/*****************************/
/* VERSION                   */
/*****************************/

#define CALLCONV_VERSION_MAJOR 0
#define CALLCONV_VERSION_MINOR 1
#define CALLCONV_VERSION_PATCH 0

/** @brief Encodes a version as a single integer: 0x00MMmmpp. */
#define CALLCONV_MAKE_VERSION(major, minor, patch) \
  ((((uint32_t)(major)) << 16) | (((uint32_t)(minor)) << 8) | ((uint32_t)(patch)))

/** @brief The version of this header. Compare with `callconv_version()`. */
#define CALLCONV_VERSION                                       \
  CALLCONV_MAKE_VERSION(                                       \
      CALLCONV_VERSION_MAJOR, CALLCONV_VERSION_MINOR, CALLCONV_VERSION_PATCH)

#ifdef __cplusplus
  #define CALLCONV_NOEXCEPT noexcept
extern "C"
{
#else
  #define CALLCONV_NOEXCEPT
#endif

  /*****************************/
  /* CLASSES                   */
  /*****************************/

  /** @brief The class of a parameter or return value. Storage for CALLCONV_CLASS_* */
  typedef uint8_t callconv_class;

/** @brief No value. Only valid as a return class. */
#define CALLCONV_CLASS_VOID 0
/** @brief A boolean, only 0 and 1 are valid values. */
#define CALLCONV_CLASS_BOOL 1
/** @brief Signed 8-bit integer. */
#define CALLCONV_CLASS_I8 2
/** @brief Unsigned 8-bit integer. */
#define CALLCONV_CLASS_U8 3
/** @brief Signed 16-bit integer. */
#define CALLCONV_CLASS_I16 4
/** @brief Unsigned 16-bit integer. */
#define CALLCONV_CLASS_U16 5
/** @brief Signed 32-bit integer. */
#define CALLCONV_CLASS_I32 6
/** @brief Unsigned 32-bit integer. */
#define CALLCONV_CLASS_U32 7
/** @brief Signed 64-bit integer. */
#define CALLCONV_CLASS_I64 8
/** @brief Unsigned 64-bit integer. */
#define CALLCONV_CLASS_U64 9
/** @brief IEEE 754 binary32. */
#define CALLCONV_CLASS_F32 10
/** @brief IEEE 754 binary64. */
#define CALLCONV_CLASS_F64 11
/** @brief Any data or function pointer. */
#define CALLCONV_CLASS_PTR 12
/** @brief Number of classes (not a valid class). */
#define CALLCONV_CLASS_COUNT 13

  /*****************************/
  /* RESULTS                   */
  /*****************************/

  /** @brief Result of `callconv_signature_init`. Storage for CALLCONV_OK / CALLCONV_ERR_* */
  typedef uint8_t callconv_result;

/** @brief Success. */
#define CALLCONV_OK 0
/** @brief A required pointer was null (storage, or params with a non-zero count). */
#define CALLCONV_ERR_NULL 1
/** @brief The storage is not aligned to CALLCONV_SIGNATURE_ALIGN. */
#define CALLCONV_ERR_MISALIGNED 2
/** @brief A class is not one of CALLCONV_CLASS_*. */
#define CALLCONV_ERR_INVALID_CLASS 3
/** @brief CALLCONV_CLASS_VOID was used as a parameter class. */
#define CALLCONV_ERR_VOID_PARAM 4
/** @brief The signature needs more stack than can be described. */
#define CALLCONV_ERR_TOO_LARGE 5

  /*****************************/
  /* RETURN STORAGE            */
  /*****************************/

  /**
   * @brief Storage for any scalar return value.
   *
   * After `callconv_call`, the whole 64 bits are meaningful: integer
   * results are sign- or zero-extended according to their class, `bool`
   * is 0 or 1, and `float` results have their upper 32 bits cleared.
   *
   * A spill handler only writes the field matching its return class;
   * the library performs the extension.
   */
  typedef union callconv_retval
  {
    /** @brief Boolean, true (1) or false (0) */
    uint8_t boolean;
    /** @brief Signed 8-bit integer */
    int8_t i8;
    /** @brief Unsigned 8-bit integer */
    uint8_t u8;
    /** @brief Signed 16-bit integer */
    int16_t i16;
    /** @brief Unsigned 16-bit integer */
    uint16_t u16;
    /** @brief Signed 32-bit integer */
    int32_t i32;
    /** @brief Unsigned 32-bit integer */
    uint32_t u32;
    /** @brief Signed 64-bit integer */
    int64_t i64;
    /** @brief Unsigned 64-bit integer */
    uint64_t u64;
    /** @brief 32-bit floating point */
    float f32;
    /** @brief 64-bit floating point */
    double f64;
    /** @brief Pointer to any type */
    void* ptr;
  } callconv_retval;

  /*****************************/
  /* SIGNATURES                */
  /*****************************/

  /** @brief Opaque, immutable description of a function signature. */
  typedef struct callconv_signature callconv_signature;

/** @brief Required alignment (in bytes) of signature storage. */
#define CALLCONV_SIGNATURE_ALIGN 8
/** @brief Size (in bytes) of storage for a signature with `param_count` parameters. */
#define CALLCONV_SIGNATURE_SIZE(param_count) \
  ((size_t)16 + (size_t)8 * (size_t)(param_count))

  /**
   * @brief Builds a signature in caller-provided storage.
   * @param storage At least CALLCONV_SIGNATURE_SIZE(param_count) bytes,
   * aligned to CALLCONV_SIGNATURE_ALIGN. Must outlive every use of the signature.
   * @param params The parameter classes (may be null if `param_count` is 0).
   * The classes are copied: the array need not outlive the call.
   * @param param_count The number of parameters
   * @param ret The return class (CALLCONV_CLASS_VOID for none)
   * @param out_result Optional (may be null): receives the result code
   * @return The signature (pointing into `storage`) or null on failure
   */
  const callconv_signature* callconv_signature_init(
      void* storage, const callconv_class* params, uint32_t param_count,
      callconv_class ret, callconv_result* out_result) CALLCONV_NOEXCEPT;

  /** @brief Returns the number of parameters of a signature. */
  uint32_t callconv_signature_param_count(const callconv_signature* sig) CALLCONV_NOEXCEPT;

  /** @brief Returns the class of parameter `index`, or CALLCONV_CLASS_VOID if out of range. */
  callconv_class callconv_signature_param(
      const callconv_signature* sig, uint32_t index) CALLCONV_NOEXCEPT;

  /** @brief Returns the return class of a signature. */
  callconv_class callconv_signature_return(const callconv_signature* sig) CALLCONV_NOEXCEPT;

  /*****************************/
  /* DYNAMIC CALL              */
  /*****************************/

  /**
   * @brief Generic function pointer type.
   * This type is used wherever an arbitrary function address is needed.
   */
  typedef void (*callconv_fn)(void);

  /**
   * @brief Calls a compiled function described by a runtime signature.
   * @param sig The signature of `fn`
   * @param fn The function to call, cast to `callconv_fn`
   * @param args `args[i]` points to a value of the class of parameter `i`
   * (may be null if the signature has no parameters)
   * @param ret Receives the result, fully extended (may be null to discard it).
   * For a void return, `ret->u64` is set to 0.
   * @pre `fn` really has the signature described by `sig`, and does not throw.
   */
  void callconv_call(
      const callconv_signature* sig, callconv_fn fn, void* const* args,
      callconv_retval* ret) CALLCONV_NOEXCEPT;

  /*****************************/
  /* SPILL                     */
  /*****************************/

  typedef struct callconv_spill_closure callconv_spill_closure;

  /**
   * @brief A generic implementation invoked through the spill entry.
   * @param self The closure (the first argument of the typed call)
   * @param args `args[i]` points to the value of parameter `i`;
   * `args[0]` points to the first argument itself (the closure pointer)
   * @param ret Write the field matching the return class. It is zeroed
   * before the handler runs; the library extends it as required.
   * @pre Must not throw.
   */
  typedef void (*callconv_handler)(
      const callconv_spill_closure* self, void* const* args, callconv_retval* ret);

  /**
   * @brief What the spill entry needs to forward a call.
   *
   * The first argument of every call made through the spill entry must
   * point to a closure. To attach your own data, make the closure the
   * FIRST member of your own struct and cast `self` back in the handler.
   */
  struct callconv_spill_closure
  {
    /** @brief The signature of the typed calls made through the spill entry */
    const callconv_signature* sig;
    /** @brief The generic implementation */
    callconv_handler handler;
  };

  /**
   * @brief Returns the spill entry for a signature.
   *
   * The returned address must be cast to the real function pointer type
   * described by `sig` before being called. Every call through it must
   * pass a pointer to a `callconv_spill_closure` (whose `sig` describes
   * the same signature) as its first argument.
   *
   * @param sig The signature of the typed calls
   * @return The entry, or null if `sig` is null or its first parameter is
   * not CALLCONV_CLASS_PTR
   */
  callconv_fn callconv_spill_entry(const callconv_signature* sig) CALLCONV_NOEXCEPT;

  /*****************************/
  /* INFORMATION               */
  /*****************************/

  /** @brief Returns the version the library was built with (see CALLCONV_VERSION). */
  uint32_t callconv_version(void) CALLCONV_NOEXCEPT;

  /**
   * @brief Returns the name of the calling convention implemented by this build:
   * "x86_64-sysv", "x86_64-win64", "arm64-aapcs", "arm64-apple" or "arm64-win".
   */
  const char* callconv_abi_name(void) CALLCONV_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* CALLCONV_CALLCONV_H */
