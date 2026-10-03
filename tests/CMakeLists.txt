cmake_minimum_required(VERSION 3.21)
project(OpenVegasTextPanelTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Test Svg Xml OpenGL)
add_executable(textpanel_regression textpanel_regression.cpp
    ../ui/panels/Text.ui
    ../resources/icons.qrc ../src/ui/TextPanel.cpp ../src/ui/TextPanel.h ../src/ui/Theme.cpp
    ../src/ui/TextSettingsDialog.cpp ../src/ui/TextSettingsDialog.h
    ../src/composition/TextStyle.cpp ../src/render/TextRender.cpp
    ../src/composition/Composition.cpp ../src/composition/KeyFrame.cpp
    ../src/project/VegfxSerializer.cpp ../src/project/VegfxMerge.cpp ../src/media/MediaManager.cpp ../src/media/MediaAsset.cpp
    ../src/media/VideoProbe.cpp ../src/media/Vlc4Adapter.cpp ../src/media/VlcBackend.cpp ../src/core/Log.cpp
    ../src/model3d/ModelLoader.cpp ../src/model3d/ModelImportSettings.cpp
    ../src/model3d/AlembicReader.cpp ../src/model3d/Mesh.cpp)
target_include_directories(textpanel_regression PRIVATE ../src)
target_link_libraries(textpanel_regression PRIVATE Qt6::Widgets Qt6::Test Qt6::Svg Qt6::Xml)
set_target_properties(textpanel_regression PROPERTIES AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../ui/panels")
enable_testing()
add_test(NAME textpanel_regression COMMAND textpanel_regression)
set_tests_properties(textpanel_regression PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

get_target_property(TIMELINE_TEST_SOURCES textpanel_regression SOURCES)
list(REMOVE_ITEM TIMELINE_TEST_SOURCES textpanel_regression.cpp)
add_executable(timeline_regression timeline_regression.cpp ${TIMELINE_TEST_SOURCES}
    ../ui/panels/AudioMeters.ui
    ../ui/panels/Controls.ui
    ../ui/panels/History.ui
    ../ui/panels/Layer.ui
    ../ui/panels/Layout.ui
    ../ui/panels/Library.ui
    ../ui/panels/Media.ui
    ../ui/panels/Preview360.ui
    ../ui/panels/Start.ui
    ../ui/panels/Timeline.ui
    ../ui/panels/Track.ui
    ../ui/panels/Trimmer.ui
    ../src/ui/TimelineWidget.cpp ../src/ui/TimelineEditing.cpp ../src/ui/TimelineTreeRows.cpp
    ../src/ui/TimelineValueGraphView.cpp ../src/ui/EffectInspector.cpp ../src/plugin/EffectRender.cpp
    ../src/plugin/NativeEffectRender.cpp ../src/plugin/NativeEffectRender.h
    ../src/plugin/NativePlugin.cpp ../src/plugin/NativePlugin.h
    ../src/ui/LayerPanel.cpp ../src/ui/TrackPanel.cpp
    ../src/ui/Preview360VideoPanel.cpp
    ../src/ui/TrimmerPanel.cpp ../src/ui/TrimmerPanel.h
    ../src/ui/HistoryPanel.cpp ../src/ui/HistoryPanel.h
    ../src/ui/StartPanel.cpp ../src/ui/StartPanel.h
    ../src/ui/LibraryPanel.cpp ../src/ui/LibraryPanel.h
    ../src/ui/MediaPanel.cpp ../src/ui/MediaPanel.h
    ../src/ui/AudioMetersPanel.cpp ../src/ui/AudioMetersPanel.h
    ../src/ui/ViewerWidget.cpp ../src/ui/ViewerWidget.h ../src/ui/ViewerOverlay.h
    ../src/ui/Viewer360View.cpp ../src/ui/Viewer360View.h
    ../src/ui/TextTransformOverlay.cpp ../src/ui/TextTransformOverlay.h
    ../src/ui/NativeCustomUiOverlay.cpp ../src/ui/NativeCustomUiOverlay.h
    ../src/ui/NativeInstanceHost.cpp ../src/ui/NativeInstanceHost.h
    ../src/ui/MediaSettingsDialog.cpp ../src/ui/MediaSettingsDialog.h
    ../src/ui/ViewScaleButton.cpp ../src/ui/ViewScaleButton.h
    ../src/ui/ViewerTransportBar.cpp ../src/ui/ViewerTransportBar.h
    ../src/ui/DockTitleBar.cpp ../src/ui/DockTitleBar.h
    ../src/ui/LayoutPanel.cpp ../src/ui/LayoutPanel.h
    ../src/app/Settings.cpp ../src/app/Settings.h
    ../src/render/RenderManager.cpp ../src/render/AudioExport.cpp ../src/model3d/Renderer3D.cpp
    ../src/render/TextGeometry.cpp ../src/render/TextGeometry.h
    ../src/render/FrameDiskCache.cpp ../src/render/FrameDiskCache.h
    ../src/render/VideoEncoder.cpp ../src/render/VideoEncoder.h
    ../src/media/AudioWaveform.cpp ../src/media/AudioWaveform.h
    ../src/media/ProxyMedia.cpp ../src/media/ProxyMedia.h
    ../src/ui/PromptMessage.cpp ../src/ui/PromptMessage.h
    ../src/ui/CameraRule.cpp ../src/ui/CameraRule.h
    ../src/ui/AutoSave.cpp ../src/ui/AutoSave.h
    ../src/ui/LearnSidebar.cpp ../src/ui/LearnSidebar.h
    ../src/ui/ProjectSettingsDialog.cpp ../src/ui/ProjectSettingsDialog.h
    ../src/ui/RecoveredProjectsDialog.cpp ../src/ui/RecoveredProjectsDialog.h
    ../src/ui/ImportCompositionDialog.cpp ../src/ui/ImportCompositionDialog.h
    ../src/ui/CompositionSettingsDialog.cpp ../src/ui/CompositionSettingsDialog.h
    ../src/app/AVTemplates.cpp ../src/app/AVTemplates.h
    ../src/render/ExportJob.cpp ../src/render/ExportJob.h ../src/media/ExrImage.cpp ../src/media/ExrImage.h
    ../src/ui/ExportQueue.cpp ../src/ui/ExportQueue.h ../src/ui/ExportQueueView.cpp ../src/ui/ExportQueueView.h
    ../src/composition/MotionTracker.cpp ../src/composition/MotionTracker.h)
target_include_directories(timeline_regression PRIVATE ../src)
target_link_libraries(timeline_regression PRIVATE Qt6::Widgets Qt6::Test Qt6::Svg Qt6::Xml Qt6::OpenGL)
set_target_properties(timeline_regression PROPERTIES AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../ui/panels")
target_compile_definitions(timeline_regression PRIVATE
    OPENVEGAS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")
if(WIN32)
    target_compile_definitions(timeline_regression PRIVATE NOMINMAX UNICODE _UNICODE)
endif()
# Force the text logger onto stdout on Windows. Without an explicit logger,
# QtTest can return a failure to CTest while writing no captured diagnostics.
add_test(NAME timeline_regression COMMAND timeline_regression -o -,txt)
set_tests_properties(timeline_regression PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(options_regression options_regression.cpp
    ../src/ui/OptionsDialog.cpp ../src/ui/OptionsDialog.h ../ui/OptionsDialog.ui
    ../src/ui/Theme.cpp ../src/app/Settings.cpp ../src/app/Translations.cpp
    ../src/ui/PromptMessage.cpp ../src/ui/PromptMessage.h
    ../src/ui/AutoSave.cpp ../src/ui/AutoSave.h
    ../src/app/AVTemplates.cpp ../src/app/AVTemplates.h)
target_include_directories(options_regression PRIVATE ../src)
target_link_libraries(options_regression PRIVATE Qt6::Widgets Qt6::Test Qt6::Xml)
set_target_properties(options_regression PROPERTIES AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../ui")
add_test(NAME options_regression COMMAND options_regression)
set_tests_properties(options_regression PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(ui_stubs_regression ui_stubs_regression.cpp
    ../src/ui/VoiceoverDialog.cpp ../src/ui/VoiceoverDialog.h ../src/ui/Theme.cpp
    ../src/ui/ExportPanel.cpp ../src/ui/ExportPanel.h ../ui/ExportPanel.ui ../src/app/Settings.cpp)
target_include_directories(ui_stubs_regression PRIVATE ../src)
target_link_libraries(ui_stubs_regression PRIVATE Qt6::Widgets Qt6::Test)
set_target_properties(ui_stubs_regression PROPERTIES AUTOUIC ON
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../ui")
add_test(NAME ui_stubs_regression COMMAND ui_stubs_regression -o -,txt)
set_tests_properties(ui_stubs_regression PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_executable(audio_regression audio_regression.cpp
    ../src/media/AudioPlayer.cpp ../src/media/AudioPlayer.h ../src/media/Vlc4Adapter.cpp ../src/media/VlcBackend.cpp ../src/core/Log.cpp
    ../src/plugin/NativeEffectRender.cpp ../src/plugin/NativePlugin.cpp
    ../src/composition/Composition.cpp ../src/composition/KeyFrame.cpp)
target_include_directories(audio_regression PRIVATE ../src)
target_link_libraries(audio_regression PRIVATE Qt6::Test Qt6::Gui Qt6::OpenGL)
if(WIN32)
    target_compile_definitions(audio_regression PRIVATE NOMINMAX UNICODE _UNICODE)
endif()
add_test(NAME audio_regression COMMAND audio_regression -o -,txt)

find_package(Qt6 6.8 REQUIRED COMPONENTS LinguistTools)
set(TRANSLATION_TEST_QMS)
foreach(locale ru ja zh_CN)
    set(qm "${CMAKE_CURRENT_BINARY_DIR}/translation-catalogs/${locale}/openvegaseffects_${locale}.qm")
    add_custom_command(OUTPUT "${qm}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/translation-catalogs/${locale}"
        COMMAND Qt6::lrelease "${CMAKE_CURRENT_SOURCE_DIR}/../translations/openvegaseffects_${locale}.ts" -qm "${qm}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../translations/openvegaseffects_${locale}.ts"
        VERBATIM)
    list(APPEND TRANSLATION_TEST_QMS "${qm}")
endforeach()
add_executable(translation_regression translation_regression.cpp
    ../src/app/Translations.cpp ${TRANSLATION_TEST_QMS})
target_include_directories(translation_regression PRIVATE ../src)
target_link_libraries(translation_regression PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(translation_regression PRIVATE
    TRANSLATION_TEST_ROOT="${CMAKE_CURRENT_BINARY_DIR}/translation-catalogs")
if(MSVC)
    target_compile_options(translation_regression PRIVATE /utf-8)
endif()
add_test(NAME translation_regression COMMAND translation_regression -o -,txt)

foreach(audio_target options_regression ui_stubs_regression)
    target_sources(${audio_target} PRIVATE ../src/media/AudioCapture.cpp ../src/media/Vlc4Adapter.cpp ../src/media/VlcBackend.cpp ../src/core/Log.cpp)
    if(WIN32)
        target_compile_definitions(${audio_target} PRIVATE NOMINMAX UNICODE _UNICODE)
        target_link_libraries(${audio_target} PRIVATE ole32 oleaut32 strmiids)
    elseif(APPLE)
        target_link_libraries(${audio_target} PRIVATE "-framework CoreAudio" "-framework CoreFoundation")
    endif()
endforeach()

include_directories(../thirdparty/vlc/include)
