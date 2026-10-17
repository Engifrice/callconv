# callconv
Low-level calling convention building blocks for C and C++.

- **Dynamic call** (`callconv_call`): call an ordinary compiled function whose signature is only known at runtime.
- **Spill** (`callconv_spill_entry`): get machine code that can be called through a typed function pointer of *any* signature, and forwards the call to a generic handler. No code is generated at runtime.

Both are driven by a `callconv_signature`, which precomputes where every parameter lives for the target calling convention.

## Rules

- Scalars only: `bool`, 8/16/32/64-bit integers, `float`, `double`, pointers. Pass aggregates by pointer. No variadic functions.
- Signatures are immutable once built.
- A function called through the spill entry must receive a pointer to a `callconv_spill_closure` as its **first** argument. Put the closure first in your own struct to attach data.
- Functions called through `callconv_call` and spill handlers must **not** throw.
- 64-bit little-endian targets only (for now at least).

## Supported targets

| Calling convention | Platforms | Toolchains | Assembly |
|---|---|---|---|
| System V x86_64 | Linux | GCC, Clang | `x86_64_sysv_elf_gas.S` |
| Microsoft x64 | Windows, Xbox | MSVC | `x86_64_ms_pe_masm.asm` |
| Microsoft x64 | Windows, Xbox | clang-cl | `x86_64_ms_pe_gas.S` |
| AAPCS64 | Linux arm64, Android | GCC, Clang | `arm64_aapcs_elf_macho_gas.S` |
| AAPCS64 (Apple) | macOS, iOS | Apple Clang | `arm64_aapcs_elf_macho_gas.S` |
| Windows arm64 | Windows arm64 | MSVC | `arm64_aapcs_pe_armasm.asm` |
| Windows arm64 | Windows arm64 | clang-cl | `arm64_aapcs_pe_gas.S` |

## Usage

```c
#include <callconv/callconv.h>

/* float f(void* self, int32_t a, float b) */
static const callconv_class PARAMS[] = { CALLCONV_CLASS_PTR, CALLCONV_CLASS_I32, CALLCONV_CLASS_F32 };

void example(void)
{
    /* storage: CALLCONV_SIGNATURE_SIZE(3) bytes, aligned to CALLCONV_SIGNATURE_ALIGN */
    static uint64_t storage[CALLCONV_SIGNATURE_SIZE(3) / 8];
    const callconv_signature* sig =
        callconv_signature_init(storage, PARAMS, 3, CALLCONV_CLASS_F32, NULL);

    /* dynamic call */
    void* self = NULL; int32_t a = 3; float b = 1.5f;
    void* const args[] = { &self, &a, &b };
    callconv_retval ret;
    callconv_call(sig, (callconv_fn)&some_function, args, &ret);   /* ret.f32 */
}
```

```cpp
#include <callconv/callconv.hpp>

struct my_function {
    callconv_spill_closure closure;   // must be first
    int multiplier;
};

void my_handler(const callconv_spill_closure* self, void* const* args, callconv_retval* ret)
{
    auto* me = reinterpret_cast<const my_function*>(self);
    auto a = callconv::arg_get<int32_t>(args, 1);
    auto b = callconv::arg_get<float>(args, 2);
    callconv::retval_set(*ret, float(a * me->multiplier) * b);
}

using F = float(void*, int32_t, float);
callconv::typed_signature<F> sig;
my_function fn{{sig.get(), &my_handler}, 2};
auto f = callconv::spill_entry<F>(sig.get());
float r = f(&fn, 3, 1.5f);   // 9.0f
```

## Building

```sh
cmake -S . -B build
cmake --build build
./build/tests/callconv_tests
```

Options: `CALLCONV_BUILD_TESTS` and `CALLCONV_INSTALL` (both default to ON when callconv is the top-level project).
Consumers use `find_package(callconv)` or `add_subdirectory`/`FetchContent`, then link `callconv::callconv`.

Tests use [doctest](https://github.com/doctest/doctest).

## Notes
- Universal (multi-architecture) Apple builds are not supported.

## License

MIT, see [LICENSE](LICENSE).
