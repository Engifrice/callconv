/*****************************************************************/ /**
 * @file   test_signature.cpp
 * @brief  Signature construction, validation, accessors and helpers.
 *********************************************************************/
#include <doctest/doctest.h>

#include <callconv/callconv.hpp>

#include <cstdint>
#include <cstring>
#include <string>

// compile-time sizing
static_assert(CALLCONV_SIGNATURE_SIZE(0) == 16);
static_assert(CALLCONV_SIGNATURE_SIZE(3) == 40);
static_assert(callconv::signature_size(7) == CALLCONV_SIGNATURE_SIZE(7));
static_assert(sizeof(callconv::signature_storage<5>) >= CALLCONV_SIGNATURE_SIZE(5));
static_assert(alignof(callconv::signature_storage<5>) == CALLCONV_SIGNATURE_ALIGN);
static_assert(sizeof(callconv_retval) == 8);

// class deduction
namespace
{
  enum class small_enum : std::uint8_t
  {
    a
  };
  enum big_enum : std::int64_t
  {
    b
  };
} // namespace
static_assert(callconv::class_of<void>() == CALLCONV_CLASS_VOID);
static_assert(callconv::class_of<bool>() == CALLCONV_CLASS_BOOL);
static_assert(callconv::class_of<std::int8_t>() == CALLCONV_CLASS_I8);
static_assert(callconv::class_of<std::uint8_t>() == CALLCONV_CLASS_U8);
static_assert(callconv::class_of<std::int16_t>() == CALLCONV_CLASS_I16);
static_assert(callconv::class_of<std::uint16_t>() == CALLCONV_CLASS_U16);
static_assert(callconv::class_of<std::int32_t>() == CALLCONV_CLASS_I32);
static_assert(callconv::class_of<std::uint32_t>() == CALLCONV_CLASS_U32);
static_assert(callconv::class_of<std::int64_t>() == CALLCONV_CLASS_I64);
static_assert(callconv::class_of<std::uint64_t>() == CALLCONV_CLASS_U64);
static_assert(callconv::class_of<long long>() == CALLCONV_CLASS_I64);
static_assert(callconv::class_of<unsigned long long>() == CALLCONV_CLASS_U64);
static_assert(callconv::class_of<const float>() == CALLCONV_CLASS_F32);
static_assert(callconv::class_of<double>() == CALLCONV_CLASS_F64);
static_assert(callconv::class_of<void*>() == CALLCONV_CLASS_PTR);
static_assert(callconv::class_of<const char*>() == CALLCONV_CLASS_PTR);
static_assert(callconv::class_of<void (*)(int)>() == CALLCONV_CLASS_PTR);
static_assert(callconv::class_of<small_enum>() == CALLCONV_CLASS_U8);
static_assert(callconv::class_of<big_enum>() == CALLCONV_CLASS_I64);

static_assert(callconv::function_traits<float(void*, int, double)>::param_count == 3);
static_assert(callconv::function_traits<float(void*, int, double)>::ret == CALLCONV_CLASS_F32);
static_assert(callconv::function_traits<void (*)() noexcept>::param_count == 0);

TEST_CASE("signature: build and read back")
{
  const callconv_class params[] = {
      CALLCONV_CLASS_PTR, CALLCONV_CLASS_I32, CALLCONV_CLASS_F32, CALLCONV_CLASS_BOOL};
  callconv::signature_storage<4> storage;
  callconv_result r = 0xFF;
  const callconv_signature* sig =
      callconv_signature_init(storage.bytes, params, 4, CALLCONV_CLASS_F64, &r);

  REQUIRE(sig != nullptr);
  CHECK(r == CALLCONV_OK);
  CHECK(static_cast<const void*>(sig) == static_cast<const void*>(storage.bytes));
  CHECK(callconv_signature_param_count(sig) == 4);
  for (std::uint32_t i = 0; i < 4; ++i)
    CHECK(callconv_signature_param(sig, i) == params[i]);
  CHECK(callconv_signature_param(sig, 4) == CALLCONV_CLASS_VOID);
  CHECK(callconv_signature_return(sig) == CALLCONV_CLASS_F64);
}

TEST_CASE("signature: no parameters, optional result pointer")
{
  callconv::signature_storage<0> storage;
  const callconv_signature* sig =
      callconv_signature_init(storage.bytes, nullptr, 0, CALLCONV_CLASS_VOID, nullptr);
  REQUIRE(sig != nullptr);
  CHECK(callconv_signature_param_count(sig) == 0);
  CHECK(callconv_signature_return(sig) == CALLCONV_CLASS_VOID);
}

TEST_CASE("signature: errors")
{
  callconv::signature_storage<4> storage;
  const callconv_class ok_params[] = {CALLCONV_CLASS_PTR, CALLCONV_CLASS_I32};
  callconv_result r                = CALLCONV_OK;

  SUBCASE("null storage")
  {
    CHECK(callconv_signature_init(nullptr, ok_params, 2, CALLCONV_CLASS_VOID, &r) == nullptr);
    CHECK(r == CALLCONV_ERR_NULL);
  }
  SUBCASE("null params with a non-zero count")
  {
    CHECK(callconv_signature_init(storage.bytes, nullptr, 2, CALLCONV_CLASS_VOID, &r) == nullptr);
    CHECK(r == CALLCONV_ERR_NULL);
  }
  SUBCASE("misaligned storage")
  {
    CHECK(callconv_signature_init(storage.bytes + 1, ok_params, 2, CALLCONV_CLASS_VOID, &r) == nullptr);
    CHECK(r == CALLCONV_ERR_MISALIGNED);
  }
  SUBCASE("invalid parameter class")
  {
    const callconv_class bad[] = {CALLCONV_CLASS_PTR, (callconv_class)CALLCONV_CLASS_COUNT};
    CHECK(callconv_signature_init(storage.bytes, bad, 2, CALLCONV_CLASS_VOID, &r) == nullptr);
    CHECK(r == CALLCONV_ERR_INVALID_CLASS);
  }
  SUBCASE("invalid return class")
  {
    CHECK(callconv_signature_init(storage.bytes, ok_params, 2, 200, &r) == nullptr);
    CHECK(r == CALLCONV_ERR_INVALID_CLASS);
  }
  SUBCASE("void parameter")
  {
    const callconv_class bad[] = {CALLCONV_CLASS_PTR, CALLCONV_CLASS_VOID};
    CHECK(callconv_signature_init(storage.bytes, bad, 2, CALLCONV_CLASS_VOID, &r) == nullptr);
    CHECK(r == CALLCONV_ERR_VOID_PARAM);
  }
  SUBCASE("errors without a result pointer")
  {
    CHECK(callconv_signature_init(nullptr, ok_params, 2, CALLCONV_CLASS_VOID, nullptr) == nullptr);
  }
}

TEST_CASE("signature: null accessors")
{
  CHECK(callconv_signature_param_count(nullptr) == 0);
  CHECK(callconv_signature_param(nullptr, 0) == CALLCONV_CLASS_VOID);
  CHECK(callconv_signature_return(nullptr) == CALLCONV_CLASS_VOID);
}

TEST_CASE("spill entry: requires a leading pointer parameter")
{
  CHECK(callconv_spill_entry(nullptr) == nullptr);

  callconv::typed_signature<void()> none;
  CHECK(callconv_spill_entry(none.get()) == nullptr);

  callconv::typed_signature<void(std::int64_t)> not_ptr;
  CHECK(callconv_spill_entry(not_ptr.get()) == nullptr);

  callconv::typed_signature<void(void*)> ok;
  CHECK(callconv_spill_entry(ok.get()) != nullptr);
}

TEST_CASE("information")
{
  CHECK(callconv_version() == CALLCONV_VERSION);
  const std::string abi = callconv_abi_name();
  CHECK(!abi.empty());
  MESSAGE("calling convention: ", abi);
}
