# OpenVegas Effects — qmake project for Qt Creator
# Open this file in Qt Creator (Qt 6.8+ Widgets kit).
# Alternative build: CMakeLists.txt (KEEP SOURCES/HEADERS in sync).

# Qt 6.8+ UI APIs; media playback/recording use libVLC.
!versionAtLeast(QT_VERSION, 6.8.0):error("OpenVegasEffects requires Qt 6.8 or newer")

QT       += core gui widgets opengl openglwidgets sql svg xml

# No Qt WebEngine: the Learn sidebar is a native widget (see CMakeLists.txt).
CONFIG   += c++17 warn_on lrelease embed_translations
# Qt Creator's MSVC build step uses Makefile.Debug/Makefile.Release. Generate
# both instead of leaving obsolete sub-makefiles after a qmake refresh.
win32:CONFIG += debug_and_release
CONFIG   -= debug_and_release_target
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = OpenVegasEffects
VERSION  = 0.1.0

# Headers live under src/ and include each other relative to that root.
INCLUDEPATH += $$PWD/src
UI_DIR       = $$OUT_PWD/ui_headers
MOC_DIR      = $$OUT_PWD/moc
OBJECTS_DIR  = $$OUT_PWD/obj
RCC_DIR      = $$OUT_PWD/rcc

# Each MSVC build pass needs its own generated resource, especially the .qrc
# embedding translations. A shared RCC_DIR lets the release pass overwrite the
# debug resource with dependencies on release/*.qm.
win32:debug_and_release {
    CONFIG(debug, debug|release): BUILD_VARIANT = debug
    else: BUILD_VARIANT = release
    UI_DIR      = $$OUT_PWD/ui_headers/$$BUILD_VARIANT
    MOC_DIR     = $$OUT_PWD/moc/$$BUILD_VARIANT
    OBJECTS_DIR = $$OUT_PWD/obj/$$BUILD_VARIANT
    RCC_DIR     = $$OUT_PWD/rcc/$$BUILD_VARIANT
    # qmake appends debug/release to LRELEASE_DIR itself.
    LRELEASE_DIR = .qm
    DESTDIR     = $$OUT_PWD/$$BUILD_VARIANT
}
INCLUDEPATH += $$UI_DIR

HEADERS += \
    src/app/AppMain.h \
    src/app/Settings.h \
    src/app/ProjectDefaults.h \
    src/app/Translations.h \
    src/app/UserDataPaths.h \
    src/cache/CacheDB.h \
    src/composition/Composition.h \
    src/composition/CompositionState.h \
    src/composition/EditorSequence.h \
    src/composition/Effect.h \
    src/composition/KeyFrame.h \
    src/composition/Layer.h \
    src/composition/MotionTracker.h \
    src/composition/TextStyle.h \
    src/composition/Transition.h \
    src/core/Identifier.h \
    src/core/Log.h \
    src/core/Result.h \
    src/core/Version.h \
    src/license/LicenseManager.h \
    src/license/LicenseTypes.h \
    src/license/OpenLicenseManager.h \
    src/media/MediaAsset.h \
    src/media/MediaStreamInfo.h \
    src/media/AudioCapture.h \
    src/media/AudioPlayer.h \
    src/media/AudioWaveform.h \
    src/media/ProxyMedia.h \
    src/media/PcmWave.h \
    src/media/ExrImage.h \
    src/media/VideoProbe.h \
    src/media/VlcBackend.h \
    src/media/MediaManager.h \
    src/plugin/EffectRender.h \
    src/plugin/EffectSpec.h \
    src/plugin/NativeEffectRender.h \
    src/plugin/NativePlugin.h \
    src/plugin/Plugin.h \
    src/plugin/PluginId.h \
    src/plugin/PluginManager.h \
    src/project/VegfxSerializer.h \
    src/project/VegfxMerge.h \
    src/model3d/Mesh.h \
    src/model3d/ModelImportSettings.h \
    src/model3d/ModelLoader.h \
    src/model3d/AlembicReader.h \
    src/model3d/Renderer3D.h \
    src/render/AudioExport.h \
    src/render/FrameDiskCache.h \
    src/render/VideoEncoder.h \
    src/render/RenderManager.h \
    src/render/TextGeometry.h \
    src/render/TextRender.h \
    src/ui/AboutDialog.h \
    src/ui/AudioMetersPanel.h \
    src/ui/DockTitleBar.h \
    src/ui/SplashScreen.h \
    src/ui/EffectInspector.h \
    src/ui/EffectsPanel.h \
    src/ui/HistoryPanel.h \
    src/ui/LayerPanel.h \
    src/ui/LayoutPanel.h \
    src/ui/LayoutTransformCommand.h \
    src/ui/LearnSidebar.h \
    src/ui/StartPanel.h \
    src/ui/LibraryPanel.h \
    src/ui/MainWindow.h \
    src/ui/Model3DSettingsDialog.h \
    src/ui/OptionsDialog.h \
    src/ui/EffectPlacement.h \
    src/ui/PromptMessage.h \
    src/ui/CameraRule.h \
    src/ui/AutoSave.h \
    src/ui/ProjectSettingsDialog.h \
    src/ui/RecoveredProjectsDialog.h \
    src/ui/ImportCompositionDialog.h \
    src/ui/CompositionSettingsDialog.h \
    src/app/AVTemplates.h \
    src/render/ExportJob.h \
    src/ui/ExportQueue.h \
    src/ui/ExportQueueView.h \
    src/ui/TextureWarning.h \
    src/ui/MediaPanel.h \
    src/ui/Preview360VideoPanel.h \
    src/ui/TextPanel.h \
    src/ui/ScreenColorPicker.h \
    src/ui/TimelineParameterEditor.h \
    src/ui/TimelineRowDelegate.h \
    src/ui/TextPropertyWidgets.h \
    src/ui/TextEditCommand.h \
    src/ui/TextSettingsDialog.h \
    src/ui/VoiceoverDialog.h \
    src/ui/TrackPanel.h \
    src/ui/TrimmerPanel.h \
    src/ui/ExportPanel.h \
    src/ui/Theme.h \
    src/ui/TimelineValueGraphView.h \
    src/ui/TimelineWidget.h \
    src/ui/ViewerWidget.h \
    src/ui/ViewerOverlay.h \
    src/ui/Viewer360View.h \
    src/ui/TextTransformOverlay.h \
    src/ui/NativeCustomUiOverlay.h     src/ui/NativeInstanceHost.h \
    src/ui/MediaSettingsDialog.h \
    src/ui/ViewerPanel.h \
    src/ui/ViewerTransportBar.h \
    src/ui/ViewScaleButton.h

SOURCES += \
    src/main.cpp \
    src/app/AppMain.cpp \
    src/app/Settings.cpp \
    src/app/Translations.cpp \
    src/app/UserDataPaths.cpp \
    src/cache/CacheDB.cpp \
    src/composition/Composition.cpp \
    src/composition/KeyFrame.cpp \
    src/composition/MotionTracker.cpp \
    src/composition/TextStyle.cpp \
    src/core/Log.cpp \
    src/license/OpenLicenseManager.cpp \
    src/media/MediaAsset.cpp \
    src/media/AudioCapture.cpp \
    src/media/AudioPlayer.cpp \
    src/media/AudioWaveform.cpp \
    src/media/ProxyMedia.cpp \
    src/media/ExrImage.cpp \
    src/media/VideoProbe.cpp \
    src/media/Vlc4Adapter.cpp \
    src/media/VlcBackend.cpp \
    src/media/MediaManager.cpp \
    src/plugin/EffectRender.cpp \
    src/plugin/NativeEffectRender.cpp \
    src/plugin/NativePlugin.cpp \
    src/plugin/PluginManager.cpp \
    src/project/VegfxSerializer.cpp \
    src/project/VegfxMerge.cpp \
    src/model3d/AlembicReader.cpp \
    src/model3d/Mesh.cpp \
    src/model3d/ModelImportSettings.cpp \
    src/model3d/ModelLoader.cpp \
    src/model3d/Renderer3D.cpp \
    src/render/AudioExport.cpp \
    src/render/FrameDiskCache.cpp \
    src/render/VideoEncoder.cpp \
    src/render/RenderManager.cpp \
    src/render/TextGeometry.cpp \
    src/render/TextRender.cpp \
    src/ui/AboutDialog.cpp \
    src/ui/AudioMetersPanel.cpp \
    src/ui/DockTitleBar.cpp \
    src/ui/SplashScreen.cpp \
    src/ui/EffectInspector.cpp \
    src/ui/EffectsPanel.cpp \
    src/ui/HistoryPanel.cpp \
    src/ui/LayerPanel.cpp \
    src/ui/LayoutPanel.cpp \
    src/ui/LearnSidebar.cpp \
    src/ui/StartPanel.cpp \
    src/ui/LibraryPanel.cpp \
    src/ui/MainWindow.cpp \
    src/ui/Model3DSettingsDialog.cpp \
    src/ui/OptionsDialog.cpp \
    src/ui/PromptMessage.cpp \
    src/ui/CameraRule.cpp \
    src/ui/AutoSave.cpp \
    src/ui/ProjectSettingsDialog.cpp \
    src/ui/RecoveredProjectsDialog.cpp \
    src/ui/ImportCompositionDialog.cpp \
    src/ui/CompositionSettingsDialog.cpp \
    src/app/AVTemplates.cpp \
    src/render/ExportJob.cpp \
    src/ui/ExportQueue.cpp \
    src/ui/ExportQueueView.cpp \
    src/ui/MediaPanel.cpp \
    src/ui/Preview360VideoPanel.cpp \
    src/ui/TextPanel.cpp \
    src/ui/TextSettingsDialog.cpp \
    src/ui/VoiceoverDialog.cpp \
    src/ui/TrackPanel.cpp \
    src/ui/TrimmerPanel.cpp \
    src/ui/ExportPanel.cpp \
    src/ui/Theme.cpp \
    src/ui/TimelineValueGraphView.cpp \
    src/ui/TimelineEditing.cpp \
    src/ui/TimelineTreeRows.cpp \
    src/ui/TimelineWidget.cpp \
    src/ui/ViewerWidget.cpp \
    src/ui/Viewer360View.cpp \
    src/ui/TextTransformOverlay.cpp \
    src/ui/NativeCustomUiOverlay.cpp     src/ui/NativeInstanceHost.cpp \
    src/ui/MediaSettingsDialog.cpp \
    src/ui/ViewerPanel.cpp \
    src/ui/ViewerTransportBar.cpp \
    src/ui/ViewScaleButton.cpp

FORMS += \
    ui/AboutDialog.ui \
    ui/ExportPanel.ui \
    ui/MainWindow.ui \
    ui/OptionsDialog.ui \
    ui/panels/AudioMeters.ui \
    ui/panels/Controls.ui \
    ui/panels/Effects.ui \
    ui/panels/History.ui \
    ui/panels/Layer.ui \
    ui/panels/Layout.ui \
    ui/panels/Library.ui \
    ui/panels/Media.ui \
    ui/panels/Preview360.ui \
    ui/panels/Start.ui \
    ui/panels/Text.ui \
    ui/panels/Timeline.ui \
    ui/panels/Track.ui \
    ui/panels/Trimmer.ui \
    ui/panels/Viewer.ui

RESOURCES += resources/icons.qrc

TRANSLATIONS += \
    translations/openvegaseffects_ru.ts \
    translations/openvegaseffects_ja.ts \
    translations/openvegaseffects_zh_CN.ts
QM_FILES_RESOURCE_PREFIX = /i18n

win32:RC_ICONS = resources/icons/logo.ico

# Warning levels. For MSVC do not append /W4 on top of Qt mkspec /W3 (D9025).
win32-msvc {
    QMAKE_CFLAGS_WARN_ON = /W4
    QMAKE_CXXFLAGS_WARN_ON = /W4
    # Qt's MSVC mkspec already enables /permissive- and UTF-8 source handling.
    # Match CMake: always write a complete executable after partial rebuilds.
    QMAKE_LFLAGS_DEBUG -= /INCREMENTAL /INCREMENTAL:YES
    QMAKE_LFLAGS_DEBUG += /INCREMENTAL:NO
} else {
    QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
}

# Optional reference-runtime staging (gitignored).
# In Creator: Projects → Build → Additional qmake arguments:
# CONFIG+=copy_vegas_effects_runtime
copy_vegas_effects_runtime {
    VEGAS_EFFECTS_SRC = $$PWD/SAMPLES/VEGAS_Effects
    VEGAS_EFFECTS_DST = $$OUT_PWD/vegas-runtime
    !exists($$VEGAS_EFFECTS_SRC/Plugins) {
        warning("CONFIG+=copy_vegas_effects_runtime set but SAMPLES/VEGAS_Effects/Plugins not found")
    } else {
        message("Build will stage reference Plugins into $$VEGAS_EFFECTS_DST (do not commit)")
        # cmd.exe builtins and xcopy require native paths; also support spaces.
        QMAKE_POST_LINK += $$QMAKE_CHK_DIR_EXISTS $$shell_quote($$shell_path($$VEGAS_EFFECTS_DST)) $$QMAKE_MKDIR $$shell_quote($$shell_path($$VEGAS_EFFECTS_DST)) $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += $$QMAKE_COPY_DIR $$shell_quote($$shell_path($$VEGAS_EFFECTS_SRC/Plugins)) $$shell_quote($$shell_path($$VEGAS_EFFECTS_DST/Plugins))
    }
}

DISTFILES += \
    translations/README.md \
    ui/panels/README.md \
    tools/validate_translations.py

# Desktop deployment hints
win32 {
    CONFIG += windows
    DEFINES += NOMINMAX UNICODE _UNICODE
    LIBS += -luser32 -lshell32
}

unix:!macx {
    target.path = /usr/local/bin
    INSTALLS += target
}

INCLUDEPATH += $$PWD/thirdparty/vlc/include
win32:LIBS += -lole32 -loleaut32 -lstrmiids
macx:LIBS += -framework CoreAudio -framework CoreFoundation
macx:QMAKE_INFO_PLIST = $$PWD/resources/macos/Info.plist

# Prepared runtime from the vendored source build, or an explicit SDK prefix.
# The source checkout itself has no runnable libraries.
VLC_RUNTIME_OVERRIDE = $$(OPENVEGAS_VLC_RUNTIME_DIR)
VLC_RUNTIME = $$VLC_RUNTIME_OVERRIDE
isEmpty(VLC_RUNTIME):VLC_RUNTIME = $$PWD/thirdparty/vlc/runtime/$$QMAKE_HOST.os
win32:isEmpty(VLC_RUNTIME_OVERRIDE):!exists($$VLC_RUNTIME/libvlc.dll) {
    VLC_RUNTIME = $$(ProgramW6432)/VideoLAN/VLC
}
VLC_CHECK_FILES = $$files($$VLC_RUNTIME/libvlc.dll) $$files($$VLC_RUNTIME/libvlc*.so*) $$files($$VLC_RUNTIME/libvlc.dylib) $$files($$VLC_RUNTIME/lib/libvlc*.so*) $$files($$VLC_RUNTIME/lib/libvlc.dylib)
!isEmpty(VLC_CHECK_FILES) {
    VLC_DEPLOY = $$DESTDIR/vlc
    isEmpty(DESTDIR):VLC_DEPLOY = $$OUT_PWD/vlc
    macx:VLC_DEPLOY = $$OUT_PWD/$$TARGET.app/Contents/MacOS/vlc
    QMAKE_POST_LINK += $$escape_expand(\n\t) $$QMAKE_CHK_DIR_EXISTS $$shell_quote($$shell_path($$VLC_DEPLOY)) $$QMAKE_MKDIR $$shell_quote($$shell_path($$VLC_DEPLOY)) $$escape_expand(\n\t)
    VLC_RUNTIME_FILES = $$files($$VLC_RUNTIME/*.dll) $$files($$VLC_RUNTIME/libvlc*.so*) $$files($$VLC_RUNTIME/*.dylib)
    for(vlc_dll, VLC_RUNTIME_FILES) {
        QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$vlc_dll)) $$shell_quote($$shell_path($$VLC_DEPLOY)) $$escape_expand(\n\t)
    }
    exists($$VLC_RUNTIME/plugins) {
        QMAKE_POST_LINK += $$QMAKE_COPY_DIR $$shell_quote($$shell_path($$VLC_RUNTIME/plugins)) $$shell_quote($$shell_path($$VLC_DEPLOY/plugins)) $$escape_expand(\n\t)
    }
    exists($$VLC_RUNTIME/lib) {
        QMAKE_POST_LINK += $$QMAKE_COPY_DIR $$shell_quote($$shell_path($$VLC_RUNTIME/lib)) $$shell_quote($$shell_path($$VLC_DEPLOY/lib)) $$escape_expand(\n\t)
    }
    VLC_LICENSE_FILES = COPYING COPYING.LIB
    for(vlc_license, VLC_LICENSE_FILES) {
        QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$shell_path($$PWD/thirdparty/vlc/$$vlc_license)) $$shell_quote($$shell_path($$VLC_DEPLOY)) $$escape_expand(\n\t)
    }
} else:!isEmpty(VLC_RUNTIME_OVERRIDE) {
    error("OPENVEGAS_VLC_RUNTIME_DIR must contain compiled libVLC libraries, not the VLC source checkout")
}
