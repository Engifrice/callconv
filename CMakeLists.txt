cmake_minimum_required(VERSION 3.26)

project(callconv
  VERSION 0.1.0
  DESCRIPTION "Low-level calling convention building blocks: dynamic calls and universal typed entries"
  LANGUAGES C CXX
)

option(CALLCONV_BUILD_TESTS "Build the callconv tests" ${PROJECT_IS_TOP_LEVEL})
option(CALLCONV_INSTALL "Generate the install target" ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)

#####################################################################
# Target architecture
#####################################################################

# CMAKE_SYSTEM_PROCESSOR describes the host on some generators (for
# example Visual Studio with -A ARM64), so prefer what the compiler targets.
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
  list(LENGTH CMAKE_OSX_ARCHITECTURES _callconv_n_arch)
  if(_callconv_n_arch GREATER 1)
    message(FATAL_ERROR
      "callconv: multi-architecture Apple builds are not supported "
      "(CMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}), build each architecture separately")
  endif()
  set(_callconv_arch "${CMAKE_OSX_ARCHITECTURES}")
elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
  set(_callconv_arch "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
else()
  set(_callconv_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
string(TOLOWER "${_callconv_arch}" _callconv_arch)

if(_callconv_arch MATCHES "^(x86_64|amd64|x64)$")
  set(_callconv_arch x86_64)
elseif(_callconv_arch MATCHES "^(aarch64|arm64)$")
  set(_callconv_arch arm64)
else()
  message(FATAL_ERROR "callconv: unsupported architecture '${_callconv_arch}'")
endif()

#####################################################################
# Assembly selection
#
# CALLCONV_CMAKE_ABI is checked against the compiler target in
# src/platform.h: 1 x86_64-sysv, 2 x86_64-win64, 3 arm64-aapcs,
# 4 arm64-apple, 5 arm64-win.
#####################################################################

set(_callconv_asm_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/asm")
set(_callconv_asm_sources "")

if(WIN32)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(_callconv_arch STREQUAL "x86_64")
      enable_language(ASM_MASM)
      set(_callconv_abi 2)
      set(_callconv_asm_sources "${_callconv_asm_dir}/x86_64_ms_pe_masm.asm")
    else()
      enable_language(ASM_MARMASM)
      set(_callconv_abi 5)
      # armasm64 has no preprocessor, and the unwind macros come from
      # ksarm64.h: run the file through cl first (as libffi and CoreCLR do).
      # cl finds ksarm64.h through the INCLUDE environment variable set by
      # the Visual Studio developer environment.
      set(_callconv_armasm_in "${_callconv_asm_dir}/arm64_aapcs_pe_armasm.asm")
      set(_callconv_armasm_out "${CMAKE_CURRENT_BINARY_DIR}/callconv_arm64_aapcs_pe_armasm.asm")
      add_custom_command(
        OUTPUT "${_callconv_armasm_out}"
        COMMAND "${CMAKE_C_COMPILER}" /nologo /EP /P "/Fi${_callconv_armasm_out}" "/Tc${_callconv_armasm_in}"
        DEPENDS "${_callconv_armasm_in}"
        COMMENT "callconv: preprocessing arm64_aapcs_pe_armasm.asm"
        VERBATIM
      )
      set_source_files_properties("${_callconv_armasm_out}" PROPERTIES
        GENERATED TRUE
        LANGUAGE ASM_MARMASM
      )
      set(_callconv_asm_sources "${_callconv_armasm_out}")
    endif()
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # clang-cl (or clang targeting MSVC): GNU syntax with .seh_* directives
    enable_language(ASM)
    # CMake defines no MSVC_DEBUG_INFORMATION_FORMAT flags for ASM compiled
    # by clang-cl, yet applies the default format (ProgramDatabase in Debug
    # and RelWithDebInfo) and fails at generation. Empty flags, as CMake
    # already does for MSVC_RUNTIME_LIBRARY in Windows-Clang-ASM.cmake.
    # Unwind information does not depend on these flags.
    foreach(_callconv_fmt IN ITEMS Embedded ProgramDatabase EditAndContinue)
      if(NOT DEFINED CMAKE_ASM_COMPILE_OPTIONS_MSVC_DEBUG_INFORMATION_FORMAT_${_callconv_fmt})
        set(CMAKE_ASM_COMPILE_OPTIONS_MSVC_DEBUG_INFORMATION_FORMAT_${_callconv_fmt} "")
      endif()
    endforeach()
    if(_callconv_arch STREQUAL "x86_64")
      set(_callconv_abi 2)
      set(_callconv_asm_sources "${_callconv_asm_dir}/x86_64_ms_pe_gas.S")
    else()
      set(_callconv_abi 5)
      set(_callconv_asm_sources "${_callconv_asm_dir}/arm64_aapcs_pe_gas.S")
    endif()
  else()
    message(FATAL_ERROR "callconv: on Windows, only MSVC and clang-cl are supported")
  endif()
elseif(APPLE)
  if(NOT _callconv_arch STREQUAL "arm64")
    message(FATAL_ERROR "callconv: only arm64 is supported on Apple platforms")
  endif()
  enable_language(ASM)
  set(_callconv_abi 4)
  set(_callconv_asm_sources "${_callconv_asm_dir}/arm64_aapcs_elf_macho_gas.S")
else()
  enable_language(ASM)
  if(_callconv_arch STREQUAL "x86_64")
    set(_callconv_abi 1)
    set(_callconv_asm_sources "${_callconv_asm_dir}/x86_64_sysv_elf_gas.S")
  else()
    set(_callconv_abi 3)
    set(_callconv_asm_sources "${_callconv_asm_dir}/arm64_aapcs_elf_macho_gas.S")
  endif()
endif()

message(STATUS "callconv: ${_callconv_arch}, assembly: ${_callconv_asm_sources}")

#####################################################################
# Library
#####################################################################

add_library(callconv STATIC
  src/platform.h
  src/internal.h
  src/signature.cpp
  src/call.cpp
  src/spill.cpp
  ${_callconv_asm_sources}
)
add_library(callconv::callconv ALIAS callconv)

target_include_directories(callconv
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(callconv PUBLIC cxx_std_20)
target_compile_definitions(callconv PRIVATE CALLCONV_CMAKE_ABI=${_callconv_abi})
set_target_properties(callconv PROPERTIES
  # the static library is meant to be linked into shared libraries too
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  CXX_EXTENSIONS OFF
)
if(MSVC)
  target_compile_options(callconv PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive->")
else()
  target_compile_options(callconv PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>")
endif()

#####################################################################
# Install
#####################################################################

if(CALLCONV_INSTALL)
  include(CMakePackageConfigHelpers)

  install(TARGETS callconv
    EXPORT callconvTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT callconvTargets
    NAMESPACE callconv::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/callconv
  )
  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/callconvConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/callconvConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/callconv
  )
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/callconvConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    # 0.x: minor versions may break compatibility
    COMPATIBILITY SameMinorVersion
  )
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/callconvConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/callconvConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/callconv
  )
endif()

#####################################################################
# Tests
#####################################################################

if(CALLCONV_BUILD_TESTS)
  add_subdirectory(tests)
endif()