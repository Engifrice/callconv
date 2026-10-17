/*****************************************************************/ /**
 * @file   test_spill.cpp
 * @brief  The spill entry called through real typed function pointers:
 *         proves the spill matches the compiler's calling convention.
 *********************************************************************/
#include <doctest/doctest.h>

#include "signatures.hpp"
#include "support.hpp"

#include <utility>

namespace
{
  template<class F>
  struct spill_test;
  template<class R, class... A>
  struct spill_test<R(void*, A...)>
  {
    using fn_t = R (*)(void*, A...);

    template<std::size_t... I>
    static void call_once(fn_t f, test::fn_state& st, std::index_sequence<I...>)
    {
      // parameter 0 is the closure, so the others start at index 1
      if constexpr (std::is_void_v<R>)
        f(&st, test::expected<A>((std::uint32_t)(I + 1), st.seed)...);
      else
      {
        const R r = f(&st, test::expected<A>((std::uint32_t)(I + 1), st.seed)...);
        CHECK(test::same(r, test::expected<R>(test::RET_INDEX, st.seed)));
      }
    }

    static void run()
    {
      callconv::typed_signature<R(void*, A...)> sig;
      REQUIRE(sig.get() != nullptr);
      const fn_t f = callconv::spill_entry<R(void*, A...)>(sig.get());
      REQUIRE(f != nullptr);

      for (std::uint32_t seed = 1; seed <= 4; ++seed)
      {
        test::fn_state st{{sig.get(), &test::check_handler}, seed, 0, false};
        call_once(f, st, std::index_sequence_for<A...>{});
        CHECK(st.calls == 1);
        CHECK(st.ok);
      }
    }
  };
} // namespace

TEST_CASE_TEMPLATE_DEFINE("spill: typed calls", F, spill_test_id)
{
  spill_test<F>::run();
}
TEST_CASE_TEMPLATE_APPLY(spill_test_id, test::spill_signatures);

namespace
{
  // The usage pattern from the documentation: user data after the closure.
  struct my_function
  {
    callconv_spill_closure closure; // must be first
    int multiplier;
  };

  void my_handler(const callconv_spill_closure* self, void* const* args, callconv_retval* ret)
  {
    const auto* me = reinterpret_cast<const my_function*>(self);
    const auto a   = callconv::arg_get<std::int32_t>(args, 1);
    const auto b   = callconv::arg_get<float>(args, 2);
    callconv::retval_set(*ret, (float)(a * me->multiplier) * b);
  }
} // namespace

TEST_CASE("spill: user data next to the closure")
{
  using F = float(void*, std::int32_t, float);
  callconv::typed_signature<F> sig;
  REQUIRE(sig.get() != nullptr);

  my_function fn{{sig.get(), &my_handler}, 2};
  auto f = callconv::spill_entry<F>(sig.get());
  REQUIRE(f != nullptr);
  CHECK(f(&fn, 3, 1.5f) == 9.0f);
  CHECK(f(&fn, -4, 0.5f) == -4.0f);
}

TEST_CASE("spill: the same entry serves different signatures")
{
  callconv::typed_signature<std::int64_t(void*, std::int64_t)> sig_a;
  callconv::typed_signature<double(void*, double, double)> sig_b;
  CHECK(callconv_spill_entry(sig_a.get()) == callconv_spill_entry(sig_b.get()));
}
