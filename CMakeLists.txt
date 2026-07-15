# el-md (C++) — native self-rendered WYSIWYG Markdown editor.
#
# Build design:
#   - C++23 with C++ modules (CMake FILE_SET CXX_MODULES) + `import std;`.
#   - Core library `elmd_core` is PURE C++: no Windows / DirectX / WinUI deps.
#   - Platform library `elmd_platform` is the ONLY layer touching Win32 /
#     Direct2D / DirectWrite / DXGI / TSF / Clipboard (Windows headers stay
#     in that translation unit's global module fragment so no macros leak).
#   - App `el-md` is a WinUI 3 shell hosting a self-drawn EditorView surface.
#
# `import std;` is experimental in CMake 4.3; the UUID below is the official
# activation token for the CxxImportStd experimental feature in CMake 4.3.
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

cmake_minimum_required(VERSION 4.0)

project(el_md_cpp
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "el-md: native self-rendered WYSIWYG Markdown editor (C++)")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Require Ninja (needed for CMake's C++ module dependency scanning with MSVC).
if(NOT CMAKE_GENERATOR MATCHES "Ninja")
    message(FATAL_ERROR "el-md requires the Ninja generator for C++ module support. Reconfigure with -G Ninja.")
endif()

if(MSVC)
    # /utf-8: source and execution charset UTF-8 (we embed box-drawing chars).
    # /permissive-: strict standards conformance.
    add_compile_options(/utf-8 /permissive- /W3 /Zc:preprocessor /Zc:__cplusplus)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNDEFINED _UNICODE)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_compile_options(/O2)
    endif()
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

option(ELMD_BUILD_WINUI "Build the WinUI 3 C++/WinRT application shell" OFF)

if(ELMD_BUILD_WINUI)
    message(FATAL_ERROR "The WinUI 3 shell is built with MSBuild at src/app-winui/el-md.vcxproj so XAML/IDL/C++WinRT generation runs through the Windows App SDK targets. Keep CMake for elmd_core, elmd_platform, and tests.")
endif()

# ---------------------------------------------------------------------------
# Core library — pure C++, portable. Acceptance gate: no Windows dependency.
# ---------------------------------------------------------------------------
add_library(elmd_core STATIC)
target_compile_features(elmd_core PUBLIC cxx_std_23)
set_target_properties(elmd_core PROPERTIES CXX_MODULE_STD ON)

file(GLOB_RECURSE ELMD_CORE_MODULE CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.ixx)
target_sources(elmd_core PUBLIC
    FILE_SET CXX_MODULES FILES ${ELMD_CORE_MODULE})

# ---------------------------------------------------------------------------
# Platform library — the ONLY Windows-touching layer.
# ---------------------------------------------------------------------------
if(WIN32)
    # Locate the Windows 10/11 SDK so platform .ixx files can #include
    # <dwrite.h>/<d2d1.h>/... without relying on a globally-injected env.
    set(_win_sdk_root "C:/Program Files (x86)/Windows Kits/10")
    file(GLOB _win_sdk_inc_dirs LIST_DIRECTORIES true "${_win_sdk_root}/Include/10.0.*")
    if(_win_sdk_inc_dirs)
        list(SORT _win_sdk_inc_dirs ORDER DESCENDING)
        list(GET _win_sdk_inc_dirs 0 _win_sdk_inc)
        set(ELMD_WINSDK_INC "${_win_sdk_inc}" CACHE PATH "Windows SDK Include dir")
    endif()

    file(GLOB_RECURSE ELMD_PLATFORM_MODULE CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.ixx)
    file(GLOB_RECURSE ELMD_PLATFORM_SRC CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp)

    if(ELMD_PLATFORM_MODULE OR ELMD_PLATFORM_SRC)
        add_library(elmd_platform STATIC)
        target_compile_features(elmd_platform PUBLIC cxx_std_23)
        set_target_properties(elmd_platform PROPERTIES CXX_MODULE_STD ON)
        target_sources(elmd_platform PUBLIC
            FILE_SET CXX_MODULES FILES ${ELMD_PLATFORM_MODULE})
        target_sources(elmd_platform PRIVATE ${ELMD_PLATFORM_SRC})
        target_link_libraries(elmd_platform PUBLIC elmd_core)
        if(ELMD_WINSDK_INC)
            target_include_directories(elmd_platform PRIVATE
                ${ELMD_WINSDK_INC}/um
                ${ELMD_WINSDK_INC}/shared
                ${ELMD_WINSDK_INC}/ucrt
                ${ELMD_WINSDK_INC}/cppwinrt)
        endif()
        target_link_libraries(elmd_platform PUBLIC
            dwrite d2d1 d3d11 dxgi windowscodecs dwmapi uxtheme shlwapi
            ole32 oleaut32 uuid user32 gdi32 comctl32
            shell32 advapi32 imm32 propsys)

    endif()
endif()

# ---------------------------------------------------------------------------
# Tests — pure core tests, no Windows dependency, portable.
#
# Boost.UT is consumed through its C++ module. The vendored fork fixes MSVC
# module linkage and command-line initialization; main passes argc/argv
# explicitly so no test translation unit depends on MSVC's private CRT globals.
# ---------------------------------------------------------------------------
set(BOOST_UT_DISABLE_MODULE OFF CACHE BOOL "Build Boost.UT's C++ module" FORCE)
set(BOOST_UT_BUILD_EXAMPLES OFF CACHE BOOL "Do not build Boost.UT examples" FORCE)
set(BOOST_UT_BUILD_TESTS OFF CACHE BOOL "Do not build Boost.UT self-tests" FORCE)
set(BOOST_UT_ENABLE_INSTALL OFF CACHE BOOL "Do not install vendored Boost.UT" FORCE)
add_subdirectory(third_party/ut EXCLUDE_FROM_ALL)

enable_testing()
add_executable(elmd_tests)
target_compile_features(elmd_tests PRIVATE cxx_std_23)
set_target_properties(elmd_tests PROPERTIES
    CXX_MODULE_STD ON
    CXX_SCAN_FOR_MODULES ON)
file(GLOB ELMD_TEST_SRC CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
target_sources(elmd_tests PRIVATE
    ${ELMD_TEST_SRC})
target_link_libraries(elmd_tests PRIVATE elmd_core Boost::ut_module)
# Boost.UT parses positional test-name patterns and CLI options. For example,
# `elmd_tests.exe "*name*"` filters by name; `-t` lists tags.
add_test(NAME elmd_tests COMMAND elmd_tests)
