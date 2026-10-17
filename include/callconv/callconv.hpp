/*****************************************************************/ /**
 * @file   callconv.hpp
 * @brief  C++20 helpers over the `callconv` C API.
 *
 * - `class_of<T>()`: the `callconv_class` of a C++ scalar type.
 * - `function_traits<F>`: parameter and return classes of a function type.
 * - `signature_size(n)`, `signature_storage<N>`: compile-time storage sizing.
 * - `typed_signature<F>`: a signature built from a C++ function type,
 *   owning its storage.
 * - `call`, `spill_entry`, `retval_get`, `retval_set`, `arg_get`:
 *   typed conveniences over the C functions.
 *********************************************************************/
#ifndef CALLCONV_CALLCONV_HPP
#define CALLCONV_CALLCONV_HPP

#include <callconv/callconv.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

namespace callconv
{
  namespace detail
  {
    template<class>
    inline constexpr bool always_false = false;
  } // namespace detail

  /***********************/
  // CLASSES
  /***********************/

  /// @brief Returns the class of a C++ type.
  /// Supported: void (returns only), bool, integers of 1/2/4/8 bytes,
  /// float, double, pointers (data and function) and enums (through
  /// their underlying type). Anything else is a compile-time error.
  /// @tparam T The type
  template<class T>
  consteval callconv_class class_of() noexcept
  {
    using U = std::remove_cv_t<T>;
    if constexpr (std::is_void_v<U>)
      return (callconv_class)CALLCONV_CLASS_VOID;
    else if constexpr (std::is_same_v<U, bool>)
      return (callconv_class)CALLCONV_CLASS_BOOL;
    else if constexpr (std::is_enum_v<U>)
      return class_of<std::underlying_type_t<U>>();
    else if constexpr (std::is_integral_v<U>)
    {
      constexpr bool S = std::is_signed_v<U>;
      if constexpr (sizeof(U) == 1)
        return (callconv_class)(S ? CALLCONV_CLASS_I8 : CALLCONV_CLASS_U8);
      else if constexpr (sizeof(U) == 2)
        return (callconv_class)(S ? CALLCONV_CLASS_I16 : CALLCONV_CLASS_U16);
      else if constexpr (sizeof(U) == 4)
        return (callconv_class)(S ? CALLCONV_CLASS_I32 : CALLCONV_CLASS_U32);
      else if constexpr (sizeof(U) == 8)
        return (callconv_class)(S ? CALLCONV_CLASS_I64 : CALLCONV_CLASS_U64);
      else
        static_assert(detail::always_false<U>, "callconv: unsupported integer size");
    }
    else if constexpr (std::is_same_v<U, float>)
      return (callconv_class)CALLCONV_CLASS_F32;
    else if constexpr (std::is_same_v<U, double>)
      return (callconv_class)CALLCONV_CLASS_F64;
    else if constexpr (std::is_pointer_v<U> || std::is_null_pointer_v<U>)
      return (callconv_class)CALLCONV_CLASS_PTR;
    else
      static_assert(
          detail::always_false<U>,
          "callconv: only scalars are supported, pass aggregates by pointer");
  }

  /// @brief Shorthand for `class_of<T>()`.
  template<class T>
  inline constexpr callconv_class class_of_v = class_of<T>();

  /***********************/
  // FUNCTION TRAITS
  /***********************/

  /// @brief Parameter and return classes of a function type.
  template<class F>
  struct function_traits;

  template<class R, class... A>
  struct function_traits<R(A...)>
  {
    /// @brief The C++ return type
    using return_type = R;
    /// @brief The C++ function pointer type
    using pointer_type = R (*)(A...);
    /// @brief Number of parameters
    static constexpr std::uint32_t param_count = (std::uint32_t)sizeof...(A);
    /// @brief Parameter classes
    static constexpr std::array<callconv_class, sizeof...(A)> params{class_of<A>()...};
    /// @brief Return class
    static constexpr callconv_class ret = class_of<R>();
  };
  template<class R, class... A>
  struct function_traits<R(A...) noexcept> : function_traits<R(A...)>
  {
  };
  template<class R, class... A>
  struct function_traits<R (*)(A...)> : function_traits<R(A...)>
  {
  };
  template<class R, class... A>
  struct function_traits<R (*)(A...) noexcept> : function_traits<R(A...)>
  {
  };

  /***********************/
  // STORAGE
  /***********************/

  /// @brief Required alignment of signature storage.
  inline constexpr std::size_t signature_alignment = CALLCONV_SIGNATURE_ALIGN;

  /// @brief Size of the storage of a signature with `param_count` parameters.
  constexpr std::size_t signature_size(std::uint32_t param_count) noexcept
  {
    return CALLCONV_SIGNATURE_SIZE(param_count);
  }

  /// @brief Correctly sized and aligned storage for a signature of N parameters.
  template<std::uint32_t N>
  struct signature_storage
  {
    alignas(CALLCONV_SIGNATURE_ALIGN) unsigned char bytes[CALLCONV_SIGNATURE_SIZE(N)];
  };

  /// @brief A signature built from a C++ function type, owning its storage.
  /// Not copyable nor movable: the signature points into the object.
  /// @tparam F A function type, such as `float(void*, int, float)`
  template<class F>
  class typed_signature
  {
    using traits = function_traits<F>;

    signature_storage<traits::param_count> _storage{};
    const callconv_signature* _sig = nullptr;

  public:
    typed_signature() noexcept
        : _sig(callconv_signature_init(
              _storage.bytes, traits::params.data(), traits::param_count,
              traits::ret, nullptr))
    {
    }
    typed_signature(const typed_signature&)            = delete;
    typed_signature& operator=(const typed_signature&) = delete;

    /// @brief Returns the signature (never null for supported types)
    const callconv_signature* get() const noexcept { return _sig; }
    operator const callconv_signature*() const noexcept { return _sig; }
  };

  /***********************/
  // VALUES
  /***********************/

  /// @brief Reads a value of type T from a `callconv_retval`.
  template<class T>
  T retval_get(const callconv_retval& r) noexcept
  {
    using U = std::remove_cv_t<T>;
    if constexpr (std::is_void_v<U>)
      return;
    else if constexpr (std::is_same_v<U, bool>)
      return r.boolean != 0;
    else if constexpr (std::is_null_pointer_v<U>)
      return nullptr;
    else
    {
      static_assert(sizeof(U) <= sizeof(callconv_retval));
      U v;
      std::memcpy(&v, &r, sizeof(U));
      return v;
    }
  }

  /// @brief Writes a value of type T into a `callconv_retval`.
  template<class T>
  void retval_set(callconv_retval& r, T v) noexcept
  {
    using U = std::remove_cv_t<T>;
    r.u64   = 0;
    if constexpr (std::is_same_v<U, bool>)
      r.boolean = v ? 1 : 0;
    else if constexpr (std::is_null_pointer_v<U>)
      r.ptr = nullptr;
    else
    {
      static_assert(sizeof(U) <= sizeof(callconv_retval));
      std::memcpy(&r, &v, sizeof(U));
    }
  }

  /// @brief Reads argument `i` of type T inside a spill handler.
  template<class T>
  T arg_get(void* const* args, std::uint32_t i) noexcept
  {
    using U = std::remove_cv_t<T>;
    if constexpr (std::is_same_v<U, bool>)
    {
      std::uint8_t b;
      std::memcpy(&b, args[i], 1);
      return b != 0;
    }
    else
    {
      U v;
      std::memcpy(&v, args[i], sizeof(U));
      return v;
    }
  }

  /***********************/
  // CALLS
  /***********************/

  /// @brief Converts any function pointer to `callconv_fn`.
  template<class F>
    requires std::is_function_v<F>
  callconv_fn to_fn(F* f) noexcept
  {
    return reinterpret_cast<callconv_fn>(f);
  }

  namespace detail
  {
    template<class R, class... A>
    R call_impl(const callconv_signature* sig, callconv_fn fn, A&... args) noexcept
    {
      // + 1 avoids a zero-sized array
      void* ptrs[sizeof...(A) + 1] = {
          const_cast<void*>(static_cast<const void*>(std::addressof(args)))...,
          nullptr};
      callconv_retval r;
      callconv_call(sig, fn, ptrs, &r);
      if constexpr (!std::is_void_v<R>)
        return retval_get<R>(r);
    }
  } // namespace detail

  /// @brief Calls `fn` through `callconv_call`.
  /// Also accepts `noexcept` functions (function pointer conversion).
  /// @pre `sig` describes the type of `fn`
  template<class R, class... A>
  R call(
      const callconv_signature* sig, R (*fn)(A...),
      std::type_identity_t<A>... args) noexcept
  {
    return detail::call_impl<R, A...>(sig, to_fn(fn), args...);
  }

  /// @brief Returns the spill entry for `sig`, cast to the typed pointer of F.
  /// @tparam F The function type described by `sig` (first parameter must be a pointer)
  /// @return The typed entry or null (see `callconv_spill_entry`)
  template<class F>
  typename function_traits<F>::pointer_type spill_entry(const callconv_signature* sig) noexcept
  {
    return reinterpret_cast<typename function_traits<F>::pointer_type>(
        callconv_spill_entry(sig));
  }
} // namespace callconv

#endif // !CALLCONV_CALLCONV_HPP
