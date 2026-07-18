# Folia (C++) — native self-rendered WYSIWYG Markdown editor.
#
# Build design:
#   - C++23 with C++ modules (CMake FILE_SET CXX_MODULES) + `import std;`.
#   - Core library `folia_core` is PURE C++: no Windows / DirectX / WinUI deps.
#   - Platform library `folia_platform` is the ONLY layer touching Win32 /
#     Direct2D / DirectWrite / DXGI / TSF / Clipboard (Windows headers stay
#     in that translation unit's global module fragment so no macros leak).
#   - App `Folia` is a WinUI 3 shell hosting a self-drawn EditorView surface.
#
# `import std;` is experimental in CMake 4.3; the UUID below is the official
# activation token for the CxxImportStd experimental feature in CMake 4.3.
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

cmake_minimum_required(VERSION 4.0)

project(folia
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Folia: native self-rendered WYSIWYG Markdown editor (C++)")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Require Ninja (needed for CMake's C++ module dependency scanning with MSVC).
if(NOT CMAKE_GENERATOR MATCHES "Ninja")
    message(FATAL_ERROR "Folia requires the Ninja generator for C++ module support. Reconfigure with -G Ninja.")
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

option(FOLIA_BUILD_WINUI "Build the WinUI 3 C++/WinRT application shell" OFF)

if(FOLIA_BUILD_WINUI)
    message(FATAL_ERROR "The WinUI 3 shell is built with MSBuild at src/app-winui/Folia.vcxproj so XAML/IDL/C++WinRT generation runs through the Windows App SDK targets. Keep CMake for folia_core, folia_platform, and tests.")
endif()

# ---------------------------------------------------------------------------
# Core library — pure C++, portable. Acceptance gate: no Windows dependency.
# ---------------------------------------------------------------------------
add_library(folia_tree_sitter_html STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter/runtime/lib.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter/grammars/html_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter/grammars/html_scanner.c)
target_include_directories(folia_tree_sitter_html PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter/runtime)
set_target_properties(folia_tree_sitter_html PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(folia_tree_sitter_html PRIVATE /wd4244 /wd4996)
endif()

add_library(folia_core STATIC)
target_compile_features(folia_core PUBLIC cxx_std_23)
set_target_properties(folia_core PROPERTIES CXX_MODULE_STD ON)
target_include_directories(folia_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tree-sitter/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/srell)
target_link_libraries(folia_core PRIVATE folia_tree_sitter_html)

file(GLOB_RECURSE FOLIA_CORE_MODULE CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.ixx)
file(GLOB_RECURSE FOLIA_CORE_SOURCE CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp)
target_sources(folia_core PUBLIC
    FILE_SET CXX_MODULES FILES ${FOLIA_CORE_MODULE})
target_sources(folia_core PRIVATE ${FOLIA_CORE_SOURCE})

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
        set(FOLIA_WINSDK_INC "${_win_sdk_inc}" CACHE PATH "Windows SDK Include dir")
    endif()

    file(GLOB_RECURSE FOLIA_PLATFORM_MODULE CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.ixx)
    file(GLOB_RECURSE FOLIA_PLATFORM_SRC CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp)

    if(FOLIA_PLATFORM_MODULE OR FOLIA_PLATFORM_SRC)
        add_library(folia_platform STATIC)
        target_compile_features(folia_platform PUBLIC cxx_std_23)
        set_target_properties(folia_platform PROPERTIES CXX_MODULE_STD ON)
        target_sources(folia_platform PUBLIC
            FILE_SET CXX_MODULES FILES ${FOLIA_PLATFORM_MODULE})
        target_sources(folia_platform PRIVATE ${FOLIA_PLATFORM_SRC})
        target_link_libraries(folia_platform PUBLIC folia_core)
        target_include_directories(folia_platform PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform)
        if(FOLIA_WINSDK_INC)
            target_include_directories(folia_platform PUBLIC
                ${FOLIA_WINSDK_INC}/um
                ${FOLIA_WINSDK_INC}/shared
                ${FOLIA_WINSDK_INC}/ucrt
                ${FOLIA_WINSDK_INC}/cppwinrt)
        endif()
        target_link_libraries(folia_platform PUBLIC
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
add_executable(FoliaTests)
target_compile_features(FoliaTests PRIVATE cxx_std_23)
set_target_properties(FoliaTests PROPERTIES
    CXX_MODULE_STD ON
    CXX_SCAN_FOR_MODULES ON)
file(GLOB_RECURSE FOLIA_TEST_SRC CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
list(FILTER FOLIA_TEST_SRC EXCLUDE REGEX "/tests/platform/")
target_sources(FoliaTests PRIVATE
    ${FOLIA_TEST_SRC})
target_include_directories(FoliaTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(FoliaTests PRIVATE folia_core Boost::ut_module)
# Boost.UT parses positional test-name patterns and CLI options. For example,
# `FoliaTests.exe "*name*"` filters by name; `-t` lists tags.
add_test(NAME FoliaTests COMMAND FoliaTests)

if(TARGET folia_platform)
    add_executable(FoliaWindowsEditorTests)
    target_compile_features(FoliaWindowsEditorTests PRIVATE cxx_std_23)
    set_target_properties(FoliaWindowsEditorTests PROPERTIES
        CXX_MODULE_STD ON
        CXX_SCAN_FOR_MODULES ON)
    file(GLOB_RECURSE FOLIA_PLATFORM_TEST_SRC CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/*.cpp)
    target_sources(FoliaWindowsEditorTests PRIVATE
        ${FOLIA_PLATFORM_TEST_SRC})
    target_include_directories(FoliaWindowsEditorTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform)
    target_link_libraries(FoliaWindowsEditorTests PRIVATE
        folia_platform Boost::ut_module)
    add_test(NAME FoliaWindowsEditorTests COMMAND FoliaWindowsEditorTests)
endif()
