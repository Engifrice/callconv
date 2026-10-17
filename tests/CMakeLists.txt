if(NOT TARGET doctest::doctest)
  include(FetchContent)
  # doctest declares its options with an old policy level: only cache
  # variables are honored.
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "Build doctest tests")
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "Skip doctest install rules")
  # doctest 2.4.11 declares cmake_minimum_required(VERSION 3.0), which
  # CMake 4 rejects unless a minimum policy version is provided.
  if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  endif()
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(doctest)
endif()

add_executable(callconv_tests
  main.cpp
  support.hpp
  signatures.hpp
  test_signature.cpp
  test_call.cpp
  test_spill.cpp
  test_roundtrip.cpp
)
target_link_libraries(callconv_tests PRIVATE callconv::callconv doctest::doctest)
target_compile_features(callconv_tests PRIVATE cxx_std_20)
set_target_properties(callconv_tests PROPERTIES CXX_EXTENSIONS OFF)
