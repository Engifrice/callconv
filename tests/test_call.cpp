/*****************************************************************/ /**
 * @file   test_call.cpp
 * @brief  callconv_call against functions compiled with real signatures:
 *         proves the dynamic call matches the compiler's calling convention.
 *********************************************************************/
#include <doctest/doctest.h>

#include "signatures.hpp"
#include "support.hpp"

#include <tuple>
#include <utility>

namespace
{
  std::uint32_t g_seed = 0;
  int g_calls          = 0;
  bool g_ok            = false;

  /// @brief A compiled function of signature F that checks its arguments
  template<class F>
  struct target;
  template<class R, class... A>
  struct target<R(A...)>
  {
    static R fn(A... a)
    {
      bool ok         = true;
      std::uint32_t i = 0;
      ((ok = test::same(a, test::expected<A>(i, g_seed)) && ok, ++i), ...);
      (void)i;
      g_ok = ok;
      ++g_calls;
      if constexpr (!std::is_void_v<R>)
        return test::expected<R>(test::RET_INDEX, g_seed);
    }
  };

  template<class F>
  struct call_test;
  template<class R, class... A>
  struct call_test<R(A...)>
  {
    template<std::size_t... I>
    static void call_once(
        const callconv_signature* sig, std::uint32_t seed, std::index_sequence<I...>)
    {
      std::tuple<A...> values{test::expected<A>((std::uint32_t)I, seed)...};
      void* ptrs[sizeof...(A) + 1] = {static_cast<void*>(&std::get<I>(values))..., nullptr};
      (void)values;

      g_seed  = seed;
      g_calls = 0;
      g_ok    = false;

      callconv_retval r;
      r.u64 = 0xA5A5A5A5A5A5A5A5ull;
      callconv_call(sig, callconv::to_fn(&target<R(A...)>::fn), ptrs, &r);

      CHECK(g_calls == 1);
      CHECK(g_ok);
      if constexpr (std::is_void_v<R>)
        CHECK(r.u64 == 0);
      else
        // fully extended, whatever the return class
        CHECK(r.u64 == test::expected_bits(callconv::class_of<R>(), test::RET_INDEX, seed));
    }

    static void run()
    {
      callconv::typed_signature<R(A...)> sig;
      REQUIRE(sig.get() != nullptr);
      for (std::uint32_t seed = 1; seed <= 4; ++seed)
        call_once(sig.get(), seed, std::index_sequence_for<A...>{});
    }
  };
} // namespace

TEST_CASE_TEMPLATE_DEFINE("callconv_call: compiled functions", F, call_test_id)
{
  call_test<F>::run();
}
TEST_CASE_TEMPLATE_APPLY(call_test_id, test::call_signatures);

namespace
{
  double scale(void* self, std::int32_t a, float b)
  {
    return (self == nullptr ? 1.0 : 2.0) * (double)a * (double)b;
  }
} // namespace

TEST_CASE("callconv_call: C++ helper")
{
  callconv::typed_signature<double(void*, std::int32_t, float)> sig;
  REQUIRE(sig.get() != nullptr);
  CHECK(callconv::call(sig.get(), &scale, nullptr, 3, 1.5f) == 4.5);

  int dummy = 0;
  CHECK(callconv::call(sig.get(), &scale, &dummy, -3, 1.5f) == -9.0);
}

TEST_CASE("callconv_call: null ret discards the result")
{
  callconv::typed_signature<double(void*, std::int32_t, float)> sig;
  REQUIRE(sig.get() != nullptr);
  void* p          = nullptr;
  std::int32_t a   = 1;
  float b          = 2.0f;
  void* args[]     = {&p, &a, &b};
  callconv_call(sig.get(), callconv::to_fn(&scale), args, nullptr);
}
