cmake_minimum_required(VERSION 3.20)

project(JamTabaVST3Wrapper
    VERSION 1.0.0
    DESCRIPTION "64-bit Windows VST3 wrapper for JamtabaVST2.dll"
    LANGUAGES CXX C
)

if(NOT WIN32)
    message(WARNING "JamTabaVST3Wrapper is designed for 64-bit Windows (wraps 64-bit JamtabaVST2.dll).")
endif()

# Disable SDK examples, VSTGUI, validator, and admin symlink creation for fast, clean builds.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_BUNDLE_FOR_WINDOWS ON CACHE BOOL "" FORCE)
set(SMTG_USE_STATIC_CRT ON CACHE BOOL "" FORCE)

# Allow passing an already-cloned local SDK via -DVST3_SDK_ROOT=C:/path/to/vst3sdk
set(VST3_SDK_ROOT "" CACHE PATH "Optional local path to Steinberg vst3sdk")

if(VST3_SDK_ROOT AND EXISTS "${VST3_SDK_ROOT}/CMakeLists.txt")
    set(vst3sdk_SOURCE_DIR "${VST3_SDK_ROOT}")
    add_subdirectory("${vst3sdk_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/vst3sdk")
else()
    include(FetchContent)
    FetchContent_Declare(
        vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES "cmake;base;pluginterfaces;public.sdk"
    )
    FetchContent_MakeAvailable(vst3sdk)
endif()

smtg_enable_vst3_sdk()

set(WRAPPER_SOURCES
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp"
    source/vst2_abi.h
    source/version.h
    source/plugids.h
    source/vst2_editor_view.h
    source/vst2_editor_view.cpp
    source/jamtaba_wrapper.h
    source/jamtaba_wrapper.cpp
    source/factory.cpp
)

smtg_add_vst3plugin(JamTabaVST3Wrapper ${WRAPPER_SOURCES})
smtg_target_configure_version_file(JamTabaVST3Wrapper)

target_compile_features(JamTabaVST3Wrapper PRIVATE cxx_std_17)

target_link_libraries(JamTabaVST3Wrapper
    PRIVATE
        sdk
)

if(WIN32)
    target_sources(JamTabaVST3Wrapper PRIVATE resource/win32resource.rc)
    target_compile_definitions(JamTabaVST3Wrapper PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(JamTabaVST3Wrapper PRIVATE user32 gdi32)
    if(MSVC)
        set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT JamTabaVST3Wrapper)
        target_compile_options(JamTabaVST3Wrapper PRIVATE /W4 /utf-8)
    endif()

    option(JTW_BUILD_SMOKE_TEST "Build Windows VST3 smoke test executable" OFF)
    if(JTW_BUILD_SMOKE_TEST)
        add_executable(vst3_smoke_test tests/vst3_smoke_test.cpp)
        target_compile_features(vst3_smoke_test PRIVATE cxx_std_17)
        target_include_directories(vst3_smoke_test PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/source"
            "${vst3sdk_SOURCE_DIR}"
        )
        target_link_libraries(vst3_smoke_test PRIVATE sdk_common pluginterfaces base)
    endif()
endif()
