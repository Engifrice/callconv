/*****************************************************************/ /**
 * @file   support.hpp
 * @brief  Test helpers: deterministic expected values per (class, index,
 *         seed), byte comparisons, and a checking spill handler.
 *********************************************************************/
#ifndef CALLCONV_TESTS_SUPPORT_HPP
#define CALLCONV_TESTS_SUPPORT_HPP

#include <callconv/callconv.hpp>

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace test
{
  /// @brief Index used for the expected return value
  inline constexpr std::uint32_t RET_INDEX = 1000;

  inline std::uint64_t mix(std::uint64_t x) noexcept
  {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
  }

  /// @brief Size in bytes of a value of a class
  inline std::size_t class_size(callconv_class c) noexcept
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
    case CALLCONV_CLASS_VOID:
      return 0;
    default:
      return 8;
    }
  }

  /// @brief The expected value of class `c` at position `index`, as the fully
  /// extended 64-bit pattern a `callconv_retval` must hold after `callconv_call`.
  /// Odd indices always have the top bit of small integers set, to catch
  /// missing or wrong (sign vs zero) extensions.
  inline std::uint64_t expected_bits(
      callconv_class c, std::uint32_t index, std::uint32_t seed) noexcept
  {
    const std::uint64_t k =
        mix(((std::uint64_t)seed << 32) ^ ((std::uint64_t)index << 8) ^ c);
    const bool top = (index & 1u) != 0;
    switch (c)
    {
    case CALLCONV_CLASS_BOOL:
      return k & 1u;
    case CALLCONV_CLASS_I8:
      return (std::uint64_t)(std::int64_t)(std::int8_t)(std::uint8_t)(k | (top ? 0x80u : 0u));
    case CALLCONV_CLASS_U8:
      return (std::uint8_t)(k | (top ? 0x80u : 0u));
    case CALLCONV_CLASS_I16:
      return (std::uint64_t)(std::int64_t)(std::int16_t)(std::uint16_t)(k | (top ? 0x8000u : 0u));
    case CALLCONV_CLASS_U16:
      return (std::uint16_t)(k | (top ? 0x8000u : 0u));
    case CALLCONV_CLASS_I32:
      return (std::uint64_t)(std::int64_t)(std::int32_t)(std::uint32_t)(k | (top ? 0x80000000u : 0u));
    case CALLCONV_CLASS_U32:
      return (std::uint32_t)(k | (top ? 0x80000000u : 0u));
    case CALLCONV_CLASS_I64:
    case CALLCONV_CLASS_U64:
      return k;
    case CALLCONV_CLASS_F32:
    {
      // exactly representable: |v| <= 2^19 / 8
      const float f = (float)((std::int32_t)(k & 0xFFFFFu) - 0x80000) * 0.125f;
      std::uint32_t bits;
      std::memcpy(&bits, &f, 4);
      return bits;
    }
    case CALLCONV_CLASS_F64:
    {
      // exactly representable: |v| <= 2^39 / 16
      const double d = (double)((std::int64_t)(k & 0xFFFFFFFFFFull) - 0x8000000000ll) * 0.0625;
      std::uint64_t bits;
      std::memcpy(&bits, &d, 8);
      return bits;
    }
    case CALLCONV_CLASS_PTR:
      // never dereferenced, only compared
      return (k & 0x00007FFFFFFFFFF0ull) | 0x10u;
    default:
      return 0;
    }
  }

  /// @brief The expected value of C++ type T at position `index`.
  template<class T>
  T expected(std::uint32_t index, std::uint32_t seed) noexcept
  {
    callconv_retval r;
    r.u64 = expected_bits(callconv::class_of<T>(), index, seed);
    return callconv::retval_get<T>(r);
  }

  /// @brief Exact comparison (bitwise for floating point).
  template<class T>
  bool same(T a, T b) noexcept
  {
    if constexpr (std::is_floating_point_v<T>)
      return std::memcmp(&a, &b, sizeof(T)) == 0;
    else
      return a == b;
  }

  /// @brief True if the value of class `c` at `p` matches `bits` (low bytes).
  inline bool bytes_match(const void* p, callconv_class c, std::uint64_t bits) noexcept
  {
    return std::memcmp(p, &bits, class_size(c)) == 0;
  }

  /// @brief A function implemented by `check_handler`: the closure must be first.
  struct fn_state
  {
    callconv_spill_closure closure;
    std::uint32_t seed;
    int calls;
    bool ok;
  };

  /// @brief Checks every argument against `expected_bits(class, i, seed)`,
  /// checks that the first argument is the closure itself, then returns
  /// `expected_bits(return class, RET_INDEX, seed)`. Only the bytes of the
  /// return class are written, the library must extend them.
  inline void check_handler(
      const callconv_spill_closure* self, void* const* args, callconv_retval* ret)
  {
    // the closure is the first member of fn_state, which is not const
    auto* st = reinterpret_cast<fn_state*>(const_cast<callconv_spill_closure*>(self));
    const callconv_signature* sig = self->sig;
    const std::uint32_t n         = callconv_signature_param_count(sig);

    void* first = nullptr;
    std::memcpy(&first, args[0], sizeof(void*));
    bool ok = first == static_cast<void*>(st);
    for (std::uint32_t i = 1; i < n; ++i)
    {
      const callconv_class c = callconv_signature_param(sig, i);
      ok = bytes_match(args[i], c, expected_bits(c, i, st->seed)) && ok;
    }
    st->ok = ok;
    ++st->calls;

    const callconv_class rc = callconv_signature_return(sig);
    if (rc != CALLCONV_CLASS_VOID)
    {
      const std::uint64_t bits = expected_bits(rc, RET_INDEX, st->seed);
      std::memcpy(ret, &bits, class_size(rc));
    }
  }
} // namespace test

#endif // !CALLCONV_TESTS_SUPPORT_HPP
