cmake_minimum_required(VERSION 3.25.0)

# VSTGUI's bundled tiny-js declares an ancient cmake_minimum_required, which
# is a hard error under CMake 4.x. This is the documented escape hatch and
# only affects that third-party subdirectory.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# We are building one plug-in, not the whole SDK. Turning the SDK's own
# examples off cuts the configure and build time down a lot.
option(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES "Enable VST 3 Plug-in Examples" OFF)
# Turn this ON if you want the `validator` target built alongside the plug-in.
option(SMTG_ENABLE_VST3_HOSTING_EXAMPLES "Enable VST 3 Hosting Examples" OFF)
# The macOS 27 editor is drawn with VSTGUI, so this one stays ON.
option(SMTG_ENABLE_VSTGUI_SUPPORT "Enable VSTGUI Support" ON)

# The SDK defaults this ON, and it symlinks the built bundle into the user's
# VST3 folder as a post-build step. Creating a symlink on Windows needs either
# an elevated shell or Developer Mode, so with the default the build fails at
# the very last step on an ordinary machine -- and on CI. Install with
# scripts/build.ps1 -Install, or with the installer, instead. Turn it back ON
# if you want the plug-in relinked on every build and can create symlinks.
option(SMTG_CREATE_PLUGIN_LINK "Symlink the built plug-in into the user VST3 folder" OFF)

#------------------------------------------------------------------------------
# Locating the VST 3 SDK
#
# The SDK is not vendored in this repository -- it is a large tree with its own
# submodules and its own release cadence. Point the build at a checkout of
# https://github.com/steinbergmedia/vst3sdk (cloned with --recursive), which
# must contain pluginterfaces/, public.sdk/, base/ and vstgui4/.
#
# Searched in order:
#   1. -Dvst3sdk_SOURCE_DIR=<path>   explicit, wins over everything
#   2. extern/vst3sdk               the documented in-tree location
#   3. $ENV{VST3_SDK_DIR}           machine-wide default
#   4. -DGLASS76_FETCH_SDK=ON       clone it into the build tree for you
#
# See docs/BUILDING.md.
#------------------------------------------------------------------------------
set(GLASS76_SDK_TAG "v3.8.1_build_84" CACHE STRING
    "VST 3 SDK git tag used when GLASS76_FETCH_SDK is ON")
option(GLASS76_FETCH_SDK "Download the VST 3 SDK into the build tree if it is not found" OFF)

set(vst3sdk_SOURCE_DIR "" CACHE PATH "Path to a VST 3 SDK checkout")

function(_glass76_is_sdk path out)
    if(path AND EXISTS "${path}/pluginterfaces" AND EXISTS "${path}/public.sdk"
       AND EXISTS "${path}/base" AND EXISTS "${path}/vstgui4")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

_glass76_is_sdk("${vst3sdk_SOURCE_DIR}" _sdk_ok)

if(NOT _sdk_ok)
    foreach(_candidate
            "${CMAKE_CURRENT_SOURCE_DIR}/extern/vst3sdk"
            "$ENV{VST3_SDK_DIR}")
        _glass76_is_sdk("${_candidate}" _sdk_ok)
        if(_sdk_ok)
            set(vst3sdk_SOURCE_DIR "${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(NOT _sdk_ok AND GLASS76_FETCH_SDK)
    # A plain git clone rather than FetchContent: the SDK is a superproject of
    # seven submodules, and FetchContent_Populate()'s "populate but do not add"
    # form is deprecated from CMake 3.30 on. This does the one thing needed and
    # keeps working.
    set(_sdk_fetch_dir "${CMAKE_CURRENT_SOURCE_DIR}/extern/vst3sdk")
    find_package(Git REQUIRED)
    message(STATUS "Glass76: cloning VST 3 SDK ${GLASS76_SDK_TAG} into ${_sdk_fetch_dir}")
    message(STATUS "Glass76: this pulls ~1 GB and takes several minutes the first time")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" clone --depth 1 --recurse-submodules --shallow-submodules
                --branch "${GLASS76_SDK_TAG}"
                https://github.com/steinbergmedia/vst3sdk.git "${_sdk_fetch_dir}"
        RESULT_VARIABLE _sdk_clone_result
    )
    if(NOT _sdk_clone_result EQUAL 0)
        message(FATAL_ERROR "Glass76: cloning the VST 3 SDK failed (git exit ${_sdk_clone_result}). "
                            "Delete ${_sdk_fetch_dir} before retrying.")
    endif()
    set(vst3sdk_SOURCE_DIR "${_sdk_fetch_dir}")
    _glass76_is_sdk("${vst3sdk_SOURCE_DIR}" _sdk_ok)
endif()

if(NOT _sdk_ok)
    message(FATAL_ERROR
        "The VST 3 SDK was not found.\n"
        "Do one of:\n"
        "  git clone --recursive --branch ${GLASS76_SDK_TAG} https://github.com/steinbergmedia/vst3sdk extern/vst3sdk\n"
        "  cmake -S . -B build -Dvst3sdk_SOURCE_DIR=<path to your SDK checkout>\n"
        "  cmake -S . -B build -DGLASS76_FETCH_SDK=ON\n"
        "See docs/BUILDING.md.")
endif()
message(STATUS "Glass76: VST 3 SDK at ${vst3sdk_SOURCE_DIR}")

project(Glass76
    # Your plug-in version. Change it here only; source/version.h picks it up
    # through the generated projectversion.h, and installer/Glass76.nsi reads
    # it back out of the built binary.
    VERSION 1.1.2.0
    DESCRIPTION "Glass76 VST 3 Plug-in"
)

set(SMTG_VSTGUI_ROOT "${vst3sdk_SOURCE_DIR}")

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

smtg_add_vst3plugin(Glass76
    source/version.h
    source/cids.h
    source/params.h
    source/prefs.h
    source/prefs.cpp
    source/processor.h
    source/processor.cpp
    source/controller.h
    source/controller.cpp
    source/entry.cpp
    source/ui/theme.h
    source/ui/macdraw.h
    source/ui/macdraw.cpp
    source/ui/editor.h
    source/ui/editor.cpp
)

# vstgui_support is the SDK's own target: it bundles VSTGUI, the
# uidescription library, and the VST3Editor / VSTGUIEditor plug-in
# bindings, and propagates the include root PUBLIC.
target_link_libraries(Glass76
    PRIVATE
        sdk
        vstgui_support
)

# The editor is a single custom view substituted into this template. Without
# this the plug-in builds and then fails to open its editor at runtime,
# because the description file is not inside the bundle.
smtg_target_add_plugin_resources(Glass76
    RESOURCES
        resource/glass76.uidesc
)

# Any font files dropped into resource/Fonts are copied into
# Contents/Resources/Fonts, which VSTGUI scans at start-up and adds to its
# private DirectWrite font collection. That is how Inter can ship with the
# plug-in without being installed system-wide. See ui/macdraw.cpp.
# The OFL requires the licence to travel with the font, so it ships in the
# same folder. VSTGUI skips files DirectWrite cannot open as a font file.
file(GLOB GLASS76_FONTS "${CMAKE_CURRENT_SOURCE_DIR}/resource/Fonts/*.ttf"
                        "${CMAKE_CURRENT_SOURCE_DIR}/resource/Fonts/*.otf"
                        "${CMAKE_CURRENT_SOURCE_DIR}/resource/Fonts/*.txt")
if(GLASS76_FONTS)
    smtg_target_add_plugin_resources(Glass76
        RESOURCES
            ${GLASS76_FONTS}
        OUTPUT_SUBDIRECTORY
            Fonts
    )
endif()

smtg_target_configure_version_file(Glass76)

# Offline test host. Loads the built bundle and checks the things the SDK
# validator cannot -- gain-stage calibration, that the compressor actually
# compresses, mix/trim/attenuator behaviour, and the meter round trip.
option(GLASS76_BUILD_TESTS "Build the offline DSP test host" OFF)
if(GLASS76_BUILD_TESTS)
    # The platform module loader and plugprovider.cpp are not in sdk_hosting;
    # the SDK's own hosting samples (see validator/CMakeLists.txt) compile
    # them straight into the executable, so we do the same.
    if(SMTG_MAC)
        set(GLASS76_TEST_MODULE_SRC
            "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm")
    elseif(SMTG_LINUX)
        set(GLASS76_TEST_MODULE_SRC
            "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_linux.cpp")
    else()
        set(GLASS76_TEST_MODULE_SRC
            "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp")
    endif()

    add_executable(glass76_test
        tools/offline_test.cpp
        source/prefs.h
        source/prefs.cpp
        ${GLASS76_TEST_MODULE_SRC}
        "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp"
    )
    if(SMTG_MAC)
        set_source_files_properties(${GLASS76_TEST_MODULE_SRC} PROPERTIES
            COMPILE_FLAGS "-fobjc-arc"
        )
        target_link_libraries(glass76_test PRIVATE "-framework Cocoa" "-framework CoreFoundation")
    endif()
    target_link_libraries(glass76_test PRIVATE sdk_hosting sdk base)
    target_include_directories(glass76_test PRIVATE "${vst3sdk_SOURCE_DIR}")
    set_target_properties(glass76_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_dependencies(glass76_test Glass76)
endif()

# Throwaway headless renderer for eyeballing RootView changes without a host
# -- see tools/ui_snapshot.cpp. Not part of the shipped plug-in.
option(GLASS76_BUILD_UI_SNAPSHOT "Build the offline UI snapshot tool" OFF)
if(GLASS76_BUILD_UI_SNAPSHOT)
    add_executable(glass76_ui_snapshot
        tools/ui_snapshot.cpp
        source/cids.h
        source/params.h
        source/prefs.h
        source/prefs.cpp
        source/controller.h
        source/controller.cpp
        source/ui/theme.h
        source/ui/macdraw.h
        source/ui/macdraw.cpp
        source/ui/editor.h
        source/ui/editor.cpp
    )
    target_link_libraries(glass76_ui_snapshot PRIVATE sdk vstgui_support)
    # Points the tool at the same resource/Fonts CMake copies into
    # Contents/Resources for the real bundle -- see tools/ui_snapshot.cpp.
    target_compile_definitions(glass76_ui_snapshot PRIVATE
        GLASS76_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resource"
    )
    set_target_properties(glass76_ui_snapshot PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

if(SMTG_MAC)
    smtg_target_set_bundle(Glass76
        BUNDLE_IDENTIFIER "com.jaxson.vst3.glass76"
        COMPANY_NAME "jxxnmade"
    )
elseif(SMTG_WIN)
    target_sources(Glass76 PRIVATE
        resource/win32resource.rc
    )
    if(MSVC)
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                     PROPERTY VS_STARTUP_PROJECT Glass76)
    endif()
endif(SMTG_MAC)
