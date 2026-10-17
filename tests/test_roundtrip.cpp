/*****************************************************************/ /**
 * @file   test_roundtrip.cpp
 * @brief  callconv_call into the spill entry with random signatures built
 *         at runtime. Complements the typed tests (which check agreement
 *         with the compiler) by covering many more signature shapes.
 *********************************************************************/
#include <doctest/doctest.h>

#include "support.hpp"

#include <cstdint>
#include <random>
#include <vector>

TEST_CASE("round trip: random runtime signatures")
{
  std::mt19937 rng(0xC0FFEEu);

  for (std::uint32_t iter = 0; iter < 2000; ++iter)
  {
    const std::uint32_t n = 1 + (std::uint32_t)(rng() % 32);

    std::vector<callconv_class> params(n);
    params[0] = CALLCONV_CLASS_PTR; // the closure
    for (std::uint32_t i = 1; i < n; ++i)
      params[i] = (callconv_class)(CALLCONV_CLASS_BOOL + rng() % (CALLCONV_CLASS_COUNT - 1));
    const auto ret = (callconv_class)(rng() % CALLCONV_CLASS_COUNT);

    // std::uint64_t elements give the required 8-byte alignment
    std::vector<std::uint64_t> storage((CALLCONV_SIGNATURE_SIZE(n) + 7) / 8);
    callconv_result result = 0xFF;
    const callconv_signature* sig =
        callconv_signature_init(storage.data(), params.data(), n, ret, &result);
    REQUIRE(result == CALLCONV_OK);
    REQUIRE(sig != nullptr);

    test::fn_state st{{sig, &test::check_handler}, iter, 0, false};

    // values in 8-byte cells: args[i] points to the low bytes (little-endian)
    std::vector<std::uint64_t> values(n);
    std::vector<void*> args(n);
    void* self = &st;
    std::memcpy(&values[0], &self, sizeof(void*));
    for (std::uint32_t i = 1; i < n; ++i)
      values[i] = test::expected_bits(params[i], i, iter);
    for (std::uint32_t i = 0; i < n; ++i)
      args[i] = &values[i];

    callconv_retval r;
    r.u64 = 0xA5A5A5A5A5A5A5A5ull;
    callconv_call(sig, callconv_spill_entry(sig), args.data(), &r);

    INFO("iteration ", iter, ", parameters ", n, ", return class ", (int)ret);
    REQUIRE(st.calls == 1);
    REQUIRE(st.ok);
    if (ret == CALLCONV_CLASS_VOID)
      REQUIRE(r.u64 == 0);
    else
      REQUIRE(r.u64 == test::expected_bits(ret, test::RET_INDEX, iter));
  }
}
