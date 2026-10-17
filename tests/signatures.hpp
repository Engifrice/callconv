/*****************************************************************/ /**
 * @file   signatures.hpp
 * @brief  Signatures tested against the compiler's own calling convention.
 *
 * They cover: no parameters, every class as parameter and return,
 * integer and float register banks overflowing onto the stack (alone and
 * together), interleaved integers and floats (positional slots on Win64),
 * and small types on the stack (natural packing on Apple arm64).
 *********************************************************************/
#ifndef CALLCONV_TESTS_SIGNATURES_HPP
#define CALLCONV_TESTS_SIGNATURES_HPP

#include <cstdint>
#include <tuple>

namespace test
{
  using i8  = std::int8_t;
  using u8  = std::uint8_t;
  using i16 = std::int16_t;
  using u16 = std::uint16_t;
  using i32 = std::int32_t;
  using u32 = std::uint32_t;
  using i64 = std::int64_t;
  using u64 = std::uint64_t;
  using f32 = float;
  using f64 = double;
  using ptr = void*;

  /// @brief Used as a type list only (never instantiated)
  using call_signatures = std::tuple<
      void(),
      i32(),
      ptr(ptr),
      i32(i32, f32),
      f64(f64, f32, f64, f32),
      i64(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64),
      f64(f64, f64, f64, f64, f64, f64, f64, f64, f64, f64),
      i8(i8, u8, i16, u16, bool),
      u8(bool, i8),
      bool(i32, u32),
      i16(i16),
      u16(u16, i8),
      u32(u32),
      u64(u64, ptr),
      f32(i32, f32, i64, f64, u8, f32, ptr, f64, i16, f32, u64, f64, i32, f32),
      f32(i32, i32, i32, i32, i32, i32, i32, i32, i32,
          f32, f32, f32, f32, f32, f32, f32, f32, f32),
      void(i8, i8, i8, i8, i8, i8, i8, i8, i8, i8, i8, i8),
      i8(i8, i8, i8, i8, i8, i8, i8, i8, i8, f32, i16, i8, f64, u16, bool),
      f64(f32, f32, f32, f32, f32, f32, f32, f32, f32, f32, f32, f32, f64, f32),
      ptr(ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr),
      bool(bool, bool, bool, bool, bool, bool, bool, bool, bool, bool),
      i32(f32, f32, f32, f32, f32, f32, f32, f32, f32,
          i32, i32, i32, i32, i32, i32, i32, i32, i32, u8, u8, u8)>;

  /// @brief Prepends a pointer parameter (the closure) to a function type
  template<class F>
  struct prepend_ptr;
  template<class R, class... A>
  struct prepend_ptr<R(A...)>
  {
    using type = R(void*, A...);
  };

  template<class L>
  struct map_prepend_ptr;
  template<class... F>
  struct map_prepend_ptr<std::tuple<F...>>
  {
    using type = std::tuple<typename prepend_ptr<F>::type...>;
  };

  /// @brief Same shapes, with the closure pointer as first parameter
  using spill_signatures = typename map_prepend_ptr<call_signatures>::type;
} // namespace test

#endif // !CALLCONV_TESTS_SIGNATURES_HPP
