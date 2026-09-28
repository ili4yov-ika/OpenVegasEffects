cmake_minimum_required(VERSION 3.21)

project(OpenVegasEffects
    VERSION 0.1.0
    DESCRIPTION "Open reimplementation of a VEGAS Effects style compositing application"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake Tools may activate the newest Visual Studio environment even when the
# cached compiler belongs to an older installation. Bind Ninja's compile/link
# commands to that compiler's own toolchain (including INCLUDE and LIB).
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    get_filename_component(OPENVEGAS_MSVC_VC_ROOT "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(level RANGE 1 6)
        get_filename_component(OPENVEGAS_MSVC_VC_ROOT "${OPENVEGAS_MSVC_VC_ROOT}" DIRECTORY)
    endforeach()
    set(OPENVEGAS_MSVC_VCVARS "${OPENVEGAS_MSVC_VC_ROOT}/Auxiliary/Build/vcvars64.bat")
    if(EXISTS "${OPENVEGAS_MSVC_VCVARS}")
        string(REGEX REPLACE "^19\\.([0-9]+).*" "14.\\1"
            OPENVEGAS_MSVC_TOOLSET_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
        configure_file(cmake/msvc_environment.cmd.in
            "${CMAKE_BINARY_DIR}/msvc_environment.cmd" @ONLY)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_BINARY_DIR}/msvc_environment.cmd")
        set(CMAKE_CXX_LINKER_LAUNCHER "${CMAKE_BINARY_DIR}/msvc_environment.cmd")
    else()
        message(WARNING "Cannot locate vcvars64.bat for ${CMAKE_CXX_COMPILER}; build from its developer environment")
    endif()
endif()

if(NOT CMAKE_PREFIX_PATH AND NOT DEFINED ENV{Qt6_DIR})
    if(EXISTS "C:/Qt/6.9.3/llvm-mingw_64")
        set(CMAKE_PREFIX_PATH "C:/Qt/6.9.3/llvm-mingw_64")
    elseif(EXISTS "C:/Qt/6.9.3/msvc2022_64")
        set(CMAKE_PREFIX_PATH "C:/Qt/6.9.3/msvc2022_64")
    endif()
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    OpenGLWidgets
    Sql
    Svg
    Xml)

# OpenEXR gives the Export panel a working ".exr" preset (Qt ships no EXR image
# plugin). The reference bundles the OpenEXR 3.1 family and has an
# "OpenEXR Export" command.
#
# Verified working with the vendored tree: -DOPENVEGAS_WITH_OPENEXR=ON builds
# OpenEXR and Imath statically into the executable and .exr export produces a
# valid file (RGBA half, ZIP compression).
#
# Still OFF by default for two reasons, both about the checkout rather than the
# code: thirdparty/openexr has no Imath beside it, so OpenEXR fetches Imath over
# the network at configure time - a default build would need connectivity - and
# both trees currently sit on their development branches (OpenEXR 4.0.0-dev,
# Imath 3.2.0-dev) rather than on release tags. Vendor thirdparty/imath and pin
# both to releases (the reference ships the 3.1 family) to make this the
# default.
option(OPENVEGAS_WITH_OPENEXR
    "Enable .exr frame export via OpenEXR (needs OpenEXR + Imath)" OFF)

set(OPENVEGAS_OPENEXR_ENABLED OFF)
if(OPENVEGAS_WITH_OPENEXR)
    # Prefer an installed OpenEXR; fall back to the vendored source tree.
    find_package(OpenEXR 3 CONFIG QUIET)
    if(TARGET OpenEXR::OpenEXR)
        set(OPENVEGAS_OPENEXR_ENABLED ON)
        message(STATUS "OpenEXR found via find_package - .exr export enabled")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/openexr/CMakeLists.txt")
        message(STATUS "Building vendored OpenEXR from thirdparty/openexr")
        # Link the vendored copy statically: built shared, its DLLs land in the
        # subproject's own output directories and the executable fails to start
        # with STATUS_DLL_NOT_FOUND unless they are deployed by hand.
        # Configure the subproject with plain variables and CMP0077 NEW, so its
        # own option() calls do not override them. A cache FORCE is not enough:
        # OpenEXR re-forces BUILD_SHARED_LIBS, and the Imath it pulls in through
        # FetchContent then builds as a shared debug DLL that the executable
        # cannot find at startup (STATUS_DLL_NOT_FOUND).
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
        set(BUILD_SHARED_LIBS OFF)
        set(OPENEXR_INSTALL OFF)
        set(OPENEXR_BUILD_TOOLS OFF)
        set(OPENEXR_BUILD_EXAMPLES OFF)
        set(OPENEXR_INSTALL_TOOLS OFF)
        set(OPENEXR_INSTALL_PKG_CONFIG OFF)
        # IMATH_INSTALL has to stay on: Imath only generates ImathTargets.cmake
        # as part of its export set, and OpenEXR's find_package(Imath) includes
        # that file. Turning it off makes the configure fail outright.
        set(IMATH_INSTALL_PKG_CONFIG OFF)
        set(BUILD_TESTING OFF)
        add_subdirectory(thirdparty/openexr EXCLUDE_FROM_ALL)
        if(TARGET OpenEXR::OpenEXR)
            set(OPENVEGAS_OPENEXR_ENABLED ON)
        endif()
    else()
        message(WARNING
            "OPENVEGAS_WITH_OPENEXR=ON but neither an installed OpenEXR nor "
            "thirdparty/openexr was found - .exr export stays disabled")
    endif()
endif()


# Alembic (.abc) import for 3D models. The reference offers it in both of its
# import filters - "Alembic (*.abc)" in the 3D model dialog (1412d0000) and
# again in the animation one (1412cff50) - and its own sample model is an .abc,
# so it is a first-class format there.
#
# The library is vendored in thirdparty/alembic. It is OFF by default for one
# reason only: Alembic requires Imath, which is not vendored beside it, and
# there is no Imath in this checkout. Turn this on once an Imath is available
# (an install, or thirdparty/imath vendored for OPENVEGAS_WITH_OPENEXR above)
# and .abc is read for real; until then loadModel() reports .abc as unsupported
# by name rather than failing blankly.
option(OPENVEGAS_WITH_ALEMBIC
    "Enable Alembic (.abc) 3D model import (needs Alembic + Imath)" OFF)

set(OPENVEGAS_ALEMBIC_ENABLED OFF)
if(OPENVEGAS_WITH_ALEMBIC)
    # Prefer an installed Alembic; fall back to the vendored source tree.
    find_package(Alembic CONFIG QUIET)
    if(TARGET Alembic::Alembic)
        set(OPENVEGAS_ALEMBIC_ENABLED ON)
        message(STATUS "Alembic found via find_package - .abc import enabled")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/alembic/CMakeLists.txt")
        find_package(Imath CONFIG QUIET)
        if(TARGET Imath::Imath)
            message(STATUS "Building vendored Alembic from thirdparty/alembic")
            set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
            set(BUILD_SHARED_LIBS OFF)
            set(USE_HDF5 OFF)          # Ogawa is the modern .abc container
            set(USE_TESTS OFF)
            set(ALEMBIC_SHARED_LIBS OFF)
            set(ALEMBIC_BUILD_LIBS ON)
            set(USE_BINARIES OFF)
            set(USE_EXAMPLES OFF)
            add_subdirectory(thirdparty/alembic EXCLUDE_FROM_ALL)
            if(TARGET Alembic)
                set(OPENVEGAS_ALEMBIC_ENABLED ON)
            endif()
        else()
            message(WARNING
                "OPENVEGAS_WITH_ALEMBIC=ON but no Imath was found - Alembic needs it, "
                "so .abc import stays disabled")
        endif()
    else()
        message(WARNING
            "OPENVEGAS_WITH_ALEMBIC=ON but neither an installed Alembic nor "
            "thirdparty/alembic was found - .abc import stays disabled")
    endif()
endif()

# libVLC owns video/audio decode, PCM master output and microphone capture.
# Runtime deployment is configured below; no Qt Multimedia dependency.
set(_ov_default_vlc_runtime "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vlc/runtime/${CMAKE_SYSTEM_NAME}")
set(OPENVEGAS_VLC_RUNTIME_DIR "${_ov_default_vlc_runtime}"
    CACHE PATH "Prepared libVLC runtime prefix (libraries and plugins); not the VLC source tree")
if(OPENVEGAS_VLC_RUNTIME_DIR STREQUAL _ov_default_vlc_runtime AND NOT IS_DIRECTORY "${OPENVEGAS_VLC_RUNTIME_DIR}" AND WIN32)
    find_path(_ov_installed_vlc libvlc.dll PATHS "$ENV{ProgramW6432}/VideoLAN/VLC" "$ENV{ProgramFiles}/VideoLAN/VLC" NO_DEFAULT_PATH)
    if(_ov_installed_vlc)
        set(OPENVEGAS_VLC_RUNTIME_DIR "${_ov_installed_vlc}" CACHE PATH "Prepared libVLC runtime prefix" FORCE)
    endif()
endif()
if(IS_DIRECTORY "${OPENVEGAS_VLC_RUNTIME_DIR}")
    file(GLOB _ov_vlc_libraries "${OPENVEGAS_VLC_RUNTIME_DIR}/libvlc.dll"
        "${OPENVEGAS_VLC_RUNTIME_DIR}/libvlc.so*" "${OPENVEGAS_VLC_RUNTIME_DIR}/libvlc.dylib"
        "${OPENVEGAS_VLC_RUNTIME_DIR}/lib/libvlc.so*" "${OPENVEGAS_VLC_RUNTIME_DIR}/lib/libvlc.dylib")
    if(NOT _ov_vlc_libraries)
        message(FATAL_ERROR "OPENVEGAS_VLC_RUNTIME_DIR must contain compiled libVLC libraries; the thirdparty/vlc source checkout is not a runtime")
    endif()
elseif(OPENVEGAS_VLC_RUNTIME_DIR AND NOT OPENVEGAS_VLC_RUNTIME_DIR STREQUAL _ov_default_vlc_runtime)
    message(FATAL_ERROR "OPENVEGAS_VLC_RUNTIME_DIR does not exist: ${OPENVEGAS_VLC_RUNTIME_DIR}")
endif()
# Qt WebEngine backs the Learn sidebar, mirroring the reference (which ships
# QtWebEngineProcess.exe and imports Qt5WebEngineWidgets + Qt5WebChannel).
# It is optional: on Windows Qt only builds WebEngine for the MSVC kits, so a
# MinGW build simply drops the sidebar instead of failing to configure.
option(OPENVEGAS_WITH_WEBENGINE
    "Build the Learn sidebar on Qt WebEngine (requires an MSVC kit on Windows)" ON)

set(OPENVEGAS_WEBENGINE_ENABLED OFF)
if(OPENVEGAS_WITH_WEBENGINE)
    find_package(Qt6 6.8 QUIET COMPONENTS WebEngineWidgets WebChannel Network)
    if(Qt6WebEngineWidgets_FOUND AND Qt6WebChannel_FOUND AND Qt6Network_FOUND)
        set(OPENVEGAS_WEBENGINE_ENABLED ON)
        message(STATUS "Qt WebEngine found - Learn sidebar enabled")
    else()
        message(STATUS
            "Qt WebEngine not found in this kit - Learn sidebar disabled. "
            "Install the Qt WebEngine component and use an MSVC kit to enable it.")
    endif()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# Designer forms live in ui/ at the project root. AUTOUIC only finds a .ui next
# to the source that includes its ui_*.h unless it is told where else to look.
set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/panels)

# qmake's RC_ICONS embeds logo.ico automatically. CMake needs the equivalent
# Windows resource source explicitly, otherwise VS Code/CMake builds get the
# generic executable and taskbar icon.
set(OPENVEGAS_PLATFORM_SOURCES)
if(WIN32)
    list(APPEND OPENVEGAS_PLATFORM_SOURCES resources/OpenVegasEffects.rc)
endif()

add_executable(OpenVegasEffects WIN32
    ${OPENVEGAS_PLATFORM_SOURCES}
    src/main.cpp
    src/core/Identifier.h
    src/core/Result.h
    src/core/Version.h
    src/core/Log.h
    src/core/Log.cpp
    src/app/UserDataPaths.h
    src/app/UserDataPaths.cpp
    src/app/Settings.h
    src/app/ProjectDefaults.h
    src/app/Settings.cpp
    src/app/Translations.h
    src/app/Translations.cpp
    src/app/AppMain.h
    src/app/AppMain.cpp
    src/ui/SplashScreen.h
    src/ui/SplashScreen.cpp
    src/ui/DockTitleBar.h
    src/ui/DockTitleBar.cpp
    src/license/LicenseTypes.h
    src/license/LicenseManager.h
    src/license/OpenLicenseManager.h
    src/license/OpenLicenseManager.cpp
    src/plugin/PluginId.h
    src/plugin/EffectSpec.h
    src/plugin/Plugin.h
    src/plugin/PluginManager.h
    src/plugin/PluginManager.cpp
    src/plugin/NativePlugin.h
    src/plugin/NativePlugin.cpp
    src/plugin/NativeEffectRender.h
    src/plugin/NativeEffectRender.cpp
    src/plugin/EffectRender.h
    src/plugin/EffectRender.cpp
    src/media/MediaAsset.h
    src/media/MediaAsset.cpp
    src/media/AudioCapture.h
    src/media/AudioPlayer.h
    src/media/MediaManager.h
    src/media/MediaManager.cpp
    src/media/ExrImage.h
    src/media/AudioCapture.cpp
    src/media/AudioPlayer.cpp
    src/media/ExrImage.cpp
    src/media/VideoProbe.h
    src/media/VideoProbe.cpp
    src/media/VlcBackend.h
    src/media/Vlc4Adapter.cpp
    src/media/VlcBackend.cpp
    src/composition/Effect.h
    src/composition/KeyFrame.h
    src/composition/KeyFrame.cpp
    src/composition/TextStyle.h
    src/composition/TextStyle.cpp
    src/composition/Layer.h
    src/composition/CompositionState.h
    src/composition/Composition.h
    src/composition/Composition.cpp
    src/composition/MotionTracker.h
    src/composition/MotionTracker.cpp
    src/cache/CacheDB.h
    src/cache/CacheDB.cpp
    src/project/VegfxSerializer.h
    src/project/VegfxSerializer.cpp
    src/model3d/Mesh.h
    src/model3d/ModelImportSettings.h
    src/model3d/ModelLoader.h
    src/model3d/Renderer3D.h
    src/render/RenderManager.h
    src/render/TextRender.h
    src/model3d/AlembicReader.h
    src/model3d/AlembicReader.cpp
    src/model3d/Mesh.cpp
    src/model3d/ModelImportSettings.cpp
    src/model3d/ModelLoader.cpp
    src/model3d/Renderer3D.cpp
    src/render/RenderManager.cpp
    src/render/TextRender.cpp
src/ui/MainWindow.h
src/ui/MainWindow.cpp
src/ui/Model3DSettingsDialog.h
src/ui/Model3DSettingsDialog.cpp
src/ui/OptionsDialog.h
src/ui/OptionsDialog.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/ui/EffectsPanel.h
    src/ui/EffectsPanel.cpp
    src/ui/MediaPanel.h
    src/ui/MediaPanel.cpp
    src/ui/ScreenColorPicker.h
    src/ui/TimelineParameterEditor.h
    src/ui/TimelineRowDelegate.h
    src/ui/TextPropertyWidgets.h
    src/ui/TextEditCommand.h
    src/ui/TextSettingsDialog.h
    src/ui/TextSettingsDialog.cpp
    src/ui/VoiceoverDialog.h
    src/ui/VoiceoverDialog.cpp
    src/media/PcmWave.h
    src/ui/TextPanel.h
    src/ui/TextPanel.cpp
    src/ui/AboutDialog.h
    src/ui/AboutDialog.cpp
    ui/AboutDialog.ui
    ui/ExportPanel.ui
    ui/MainWindow.ui
    ui/OptionsDialog.ui
    ui/panels/AudioMeters.ui
    ui/panels/Controls.ui
    ui/panels/Effects.ui
    ui/panels/History.ui
    ui/panels/Layer.ui
    ui/panels/Layout.ui
    ui/panels/Library.ui
    ui/panels/Media.ui
    ui/panels/Preview360.ui
    ui/panels/Start.ui
    ui/panels/Text.ui
    ui/panels/Timeline.ui
    ui/panels/Track.ui
    ui/panels/Trimmer.ui
    ui/panels/Viewer.ui
    src/ui/AudioMetersPanel.h
    src/ui/AudioMetersPanel.cpp
    src/ui/HistoryPanel.h
    src/ui/HistoryPanel.cpp
    src/ui/LayerPanel.h
    src/ui/LayerPanel.cpp
    src/ui/TrackPanel.h
    src/ui/TrackPanel.cpp
    src/ui/LayoutPanel.h
    src/ui/LayoutPanel.cpp
    src/ui/LearnSidebar.h
    src/ui/LearnSidebar.cpp
    resources/learn.qrc
    resources/icons.qrc
    src/ui/StartPanel.h
    src/ui/StartPanel.cpp
    src/ui/LibraryPanel.h
    src/ui/LibraryPanel.cpp
    src/ui/TrimmerPanel.h
    src/ui/TrimmerPanel.cpp
    src/ui/ExportPanel.h
    src/ui/ExportPanel.cpp
    src/ui/EffectInspector.h
    src/ui/EffectInspector.cpp
    src/ui/ViewerWidget.h
    src/ui/ViewerWidget.cpp
    src/ui/ViewerPanel.h
    src/ui/ViewerPanel.cpp
    src/ui/Preview360VideoPanel.h
    src/ui/Preview360VideoPanel.cpp
    src/ui/ViewerTransportBar.h
    src/ui/ViewerTransportBar.cpp
    src/ui/ViewScaleButton.h
    src/ui/ViewScaleButton.cpp
    src/ui/TimelineValueGraphView.h
    src/ui/TimelineValueGraphView.cpp
    src/ui/TimelineWidget.h
    src/ui/TimelineEditing.cpp
    src/ui/TimelineTreeRows.cpp
    src/ui/TimelineWidget.cpp)


# UI translations. qt_add_translations comes from Qt's LinguistTools package;
# it runs lrelease on the .ts catalogues, embeds the .qm under ":/i18n/" and
# provides the update_translations / release_translations maintenance targets.
# The component is optional so a Qt install without the Linguist tools still
# configures - the application then simply runs untranslated.
find_package(Qt6 6.8 QUIET COMPONENTS LinguistTools)
if(TARGET Qt6::lrelease)
    qt_add_translations(OpenVegasEffects
        TS_FILES
            translations/openvegaseffects_ru.ts
            translations/openvegaseffects_ja.ts
            translations/openvegaseffects_zh_CN.ts
        RESOURCE_PREFIX "/i18n")
    message(STATUS "Translations enabled (ru, ja, zh_CN)")
else()
    message(STATUS "Qt LinguistTools not found - UI translations disabled")
endif()

target_include_directories(OpenVegasEffects PRIVATE src)

target_include_directories(OpenVegasEffects PRIVATE thirdparty/vlc/include)
target_compile_definitions(OpenVegasEffects PRIVATE OPENVEGAS_VLC_RUNTIME_DIR="${OPENVEGAS_VLC_RUNTIME_DIR}")
target_link_libraries(OpenVegasEffects PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::OpenGL
    Qt6::Sql
    Qt6::Svg
    Qt6::Xml)


# Installation. tools/build_windows_installer.bat runs `cmake --install` and
# then points windeployqt and NSIS at <prefix>/bin, so the executable has to
# land there. Without this rule the install step silently produced nothing and
# the installer build failed at the windeployqt stage.
include(GNUInstallDirs)
install(TARGETS OpenVegasEffects
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .)

# Incremental linking is off for MSVC.
#
# A debug build here started crashing before main with an access violation and
# no output at all: the stack was __scrt_initialize_type_info calling a null
# pointer from the CRT's own start-up, which reads as a broken runtime but is
# what a stale incremental link produces. Recompiling everything fixed it, and
# the same state came back after enough partial rebuilds.
#
# /INCREMENTAL buys very little here - the link is a couple of seconds - and it
# costs a failure mode that looks like a corrupted toolchain. Turning it off
# makes every link write a complete image.
if(MSVC)
    foreach(_ov_cfg DEBUG RELWITHDEBINFO)
        # CMake puts /INCREMENTAL in these by default; drop it rather than
        # appending the opposite, so the link line says one thing only.
        string(REPLACE "/INCREMENTAL" "" CMAKE_EXE_LINKER_FLAGS_${_ov_cfg}
               "${CMAKE_EXE_LINKER_FLAGS_${_ov_cfg}}")
        string(APPEND CMAKE_EXE_LINKER_FLAGS_${_ov_cfg} " /INCREMENTAL:NO")
    endforeach()
endif()

if(OPENVEGAS_ALEMBIC_ENABLED)
    if(TARGET Alembic::Alembic)
        target_link_libraries(OpenVegasEffects PRIVATE Alembic::Alembic)
    else()
        target_link_libraries(OpenVegasEffects PRIVATE Alembic)
    endif()
    target_compile_definitions(OpenVegasEffects PRIVATE OPENVEGAS_HAVE_ALEMBIC)
endif()

if(OPENVEGAS_OPENEXR_ENABLED)
    target_link_libraries(OpenVegasEffects PRIVATE OpenEXR::OpenEXR)
    target_compile_definitions(OpenVegasEffects PRIVATE OPENVEGAS_HAVE_OPENEXR)
endif()

if(OPENVEGAS_WEBENGINE_ENABLED)
    target_link_libraries(OpenVegasEffects PRIVATE
        Qt6::WebEngineWidgets
        Qt6::WebChannel
        Qt6::Network)
    target_compile_definitions(OpenVegasEffects PRIVATE OPENVEGAS_HAVE_WEBENGINE)
endif()

if(WIN32)
    target_link_libraries(OpenVegasEffects PRIVATE
        user32
        shell32)
    target_compile_definitions(OpenVegasEffects PRIVATE
        NOMINMAX
        UNICODE
        _UNICODE)

    # Diagnostic target for the recovered native-plugin ABI. Kept out of the
    # default build because it executes every module passed to it; CI and the
    # application build do not need the shipped VEGAS Effects plugin tree.
    add_executable(hfpl_runtime_probe EXCLUDE_FROM_ALL
        tools/hfpl_runtime_probe.cpp
        src/plugin/NativePlugin.h
        src/plugin/NativePlugin.cpp
        src/plugin/NativeEffectRender.h
        src/plugin/NativeEffectRender.cpp)
    target_include_directories(hfpl_runtime_probe PRIVATE src)
    target_link_libraries(hfpl_runtime_probe PRIVATE Qt6::Core Qt6::Gui Qt6::OpenGL)
    target_compile_definitions(hfpl_runtime_probe PRIVATE
        NOMINMAX
        UNICODE
        _UNICODE)
endif()

# Deploy Qt runtime next to the executable after every build.
#
# The Debug build links the Qt *debug* DLLs (Qt6Widgetsd.dll, ...) and the MSVC
# debug CRT (MSVCP140D.dll, VCRUNTIME140D.dll, ucrtbased.dll). Nothing puts them
# beside the exe, so a direct launch - double-click, or a run/debug task that
# sets PATH to only the Qt bin - dies with STATUS_DLL_NOT_FOUND (0xC0000135).
# VS Code happens to work because its launch/task config prepends the Qt bin to
# the existing PATH, which also carries the dev-prompt CRT; Zed's MSVC run task
# *replaces* PATH with just the Qt bin, which drops that CRT. Rather than lean
# on every editor's PATH wiring, run windeployqt as a post-build step so the
# build directory is self-contained (Qt DLLs + plugins + WebEngine + CRT).
if(WIN32 AND TARGET OpenVegasEffects)
    if(TARGET Qt6::windeployqt)
        set(_ov_windeployqt "$<TARGET_FILE:Qt6::windeployqt>")
    elseif(EXISTS "${qt6_install_prefix}/bin/windeployqt.exe")
        set(_ov_windeployqt "${qt6_install_prefix}/bin/windeployqt.exe")
    endif()
    if(_ov_windeployqt)
        if(CMAKE_CONFIGURATION_TYPES)
            # TARGET POST_BUILD does not accept CONFIGURATIONS. Select the
            # deployment mode at build time for Visual Studio/Xcode instead.
            set(_ov_deploy_flag
                "$<IF:$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,--debug,--release>")
            add_custom_command(TARGET OpenVegasEffects POST_BUILD
                COMMAND "${_ov_windeployqt}"
                        "${_ov_deploy_flag}" --compiler-runtime --no-translations
                        "--dir=$<TARGET_FILE_DIR:OpenVegasEffects>"
                        "$<TARGET_FILE:OpenVegasEffects>"
                COMMENT "Deploying Qt runtime (windeployqt) next to OpenVegasEffects.exe"
                VERBATIM)
        else()
            # Single-config generators (Ninja, Makefiles): a build type is set
            # at configure time, so map it to the matching flag here. The debug
            # build additionally needs the debug UCRT, ucrtbased.dll, which
            # windeployqt does not ship: copy it from the Windows SDK.
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                set(_ov_deploy_flag "--debug")
                # ucrtbased.dll ships in the Windows SDK at
                # <SDK>/bin/<version>/<arch>/ucrt/ucrtbased.dll. The install
                # folder (and version) varies, so glob the Windows Kits bin and
                # take the first x64 copy. Best-effort: if none is found the
                # build still succeeds and only the debug UCRT is missing.
                file(GLOB _ov_ucrtbased_candidates
                    "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/ucrt/ucrtbased.dll")
                list(SORT _ov_ucrtbased_candidates)
                if(_ov_ucrtbased_candidates)
                    list(GET _ov_ucrtbased_candidates -1 _ov_ucrtbased_src)
                endif()
            else()
                set(_ov_deploy_flag "--release")
            endif()
            add_custom_command(TARGET OpenVegasEffects POST_BUILD
                COMMAND "${_ov_windeployqt}"
                        ${_ov_deploy_flag} --compiler-runtime --no-translations
                        "--dir=$<TARGET_FILE_DIR:OpenVegasEffects>"
                        "$<TARGET_FILE:OpenVegasEffects>"
                COMMENT "Deploying Qt runtime (windeployqt) next to OpenVegasEffects.exe"
                VERBATIM)
            if(_ov_ucrtbased_src)
                add_custom_command(TARGET OpenVegasEffects POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                            "${_ov_ucrtbased_src}"
                            "$<TARGET_FILE_DIR:OpenVegasEffects>/ucrtbased.dll"
                    COMMENT "Copying debug UCRT (ucrtbased.dll) for the debug build"
                    VERBATIM)
            endif()
        endif()
    endif()
endif()

if(WIN32)
    target_link_libraries(OpenVegasEffects PRIVATE ole32 oleaut32 strmiids)
elseif(APPLE)
    target_link_libraries(OpenVegasEffects PRIVATE "-framework CoreAudio" "-framework CoreFoundation")
endif()

if(IS_DIRECTORY "${OPENVEGAS_VLC_RUNTIME_DIR}")
    add_custom_command(TARGET OpenVegasEffects POST_BUILD
        COMMAND "${CMAKE_COMMAND}" "-DROOT=${OPENVEGAS_VLC_RUNTIME_DIR}"
            "-DDEST=$<TARGET_FILE_DIR:OpenVegasEffects>/vlc"
            "-DSOURCE_LICENSE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vlc"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployVlc.cmake"
        COMMENT "Deploying libVLC libraries and decoder/capture plugins" VERBATIM)
    # Preserve the same bin/vlc layout for NSIS and other prefix installers.
    install(DIRECTORY "${OPENVEGAS_VLC_RUNTIME_DIR}/" DESTINATION "${CMAKE_INSTALL_BINDIR}/vlc"
        FILES_MATCHING PATTERN "*.dll" PATTERN "*.so*" PATTERN "*.dylib" PATTERN "plugins.dat"
        PATTERN "COPYING*" PATTERN "vlc.exe" EXCLUDE PATTERN "npvlc.dll" EXCLUDE)
    install(FILES thirdparty/vlc/COPYING thirdparty/vlc/COPYING.LIB DESTINATION "${CMAKE_INSTALL_BINDIR}/vlc")
else()
    message(STATUS "No bundled VLC runtime; playback requires system libVLC or OPENVEGAS_VLC_DIR")
endif()
