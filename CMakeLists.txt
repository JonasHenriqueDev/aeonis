cmake_minimum_required(VERSION 3.25)

project(Aeonis VERSION 1.2.1 LANGUAGES CXX)
# A versão vem sempre do project() (não do cache: um valor antigo em cache gerou um app que se dizia
# 1.0.0 dentro do pacote 1.1.0 e se atualizava em loop). Só o sufixo de pré-release é opcional.
set(AEONIS_VERSION_SUFFIX "" CACHE STRING "Sufixo de pré-release (ex.: -beta1)")
set(AEONIS_RELEASE_VERSION "${PROJECT_VERSION}${AEONIS_VERSION_SUFFIX}")
configure_file(assets/aeonis.rc.in "${CMAKE_CURRENT_BINARY_DIR}/aeonis.rc" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(AEONIS_BUILD_TESTS "Compila os testes" ON)

if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive- /Zc:__cplusplus /MP)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Quick QuickControls2 Multimedia)
qt_standard_project_setup(REQUIRES 6.8)

# ---------------------------------------------------------------- FFmpeg (estático, vcpkg)
# libavformat/libavcodec do vcpkg (com NVENC, AMF, QSV e x264), linkadas dentro do executável.
set(_vcpkg_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
add_library(aeonis_ffmpeg INTERFACE)
target_include_directories(aeonis_ffmpeg INTERFACE "${_vcpkg_root}/include")
foreach(lib avformat avcodec swscale swresample avutil x264 vpl)
    set(_dbg ${lib})
    if(lib STREQUAL "vpl")
        set(_dbg vpld)
    endif()
    if(EXISTS "${_vcpkg_root}/lib/${lib}.lib")
        target_link_libraries(aeonis_ffmpeg INTERFACE
            "$<IF:$<CONFIG:Debug>,${_vcpkg_root}/debug/lib/${_dbg}.lib,${_vcpkg_root}/lib/${lib}.lib>")
    elseif(EXISTS "${_vcpkg_root}/lib/lib${lib}.lib")
        target_link_libraries(aeonis_ffmpeg INTERFACE
            "$<IF:$<CONFIG:Debug>,${_vcpkg_root}/debug/lib/lib${_dbg}.lib,${_vcpkg_root}/lib/lib${lib}.lib>")
    else()
        message(FATAL_ERROR "FFmpeg: ${lib}.lib não encontrada em ${_vcpkg_root}/lib (rode scripts\\build.ps1)")
    endif()
endforeach()
target_link_libraries(aeonis_ffmpeg INTERFACE mfuuid mfplat ole32 strmiids user32 advapi32 bcrypt ws2_32 secur32 ncrypt crypt32 shlwapi)

# ---------------------------------------------------------------- motor
# Tudo menos a interface: usado pelo app e pelos testes.
add_library(aeonis_engine STATIC
    src/core/Paths.cpp src/core/Paths.h
    src/core/Log.cpp src/core/Log.h
    src/core/Settings.cpp src/core/Settings.h
    src/core/Library.cpp src/core/Library.h
    src/core/MediaCache.cpp src/core/MediaCache.h
    src/core/Media.cpp src/core/Media.h
    src/core/Async.h
    src/capture/Clips.cpp src/capture/Clips.h
    src/capture/DesktopDuplication.cpp src/capture/DesktopDuplication.h
    src/capture/Encoders.cpp src/capture/Encoders.h
    src/capture/SegmentWriter.cpp src/capture/SegmentWriter.h
    src/capture/CaptureEngine.cpp src/capture/CaptureEngine.h
    src/capture/Monitors.cpp src/capture/Monitors.h
    src/audio/AudioCapture.cpp src/audio/AudioCapture.h
    src/audio/AudioMixer.cpp src/audio/AudioMixer.h
    src/games/GameCatalog.cpp src/games/GameCatalog.h
    src/games/GameWatcher.cpp src/games/GameWatcher.h
    src/games/Highlighter.cpp src/games/Highlighter.h
    src/games/EventSources.cpp src/games/EventSources.h
    src/app/Hotkeys.cpp src/app/Hotkeys.h
    src/ui/LibraryModel.cpp src/ui/LibraryModel.h
    src/share/YouTube.cpp src/share/YouTube.h
)
target_include_directories(aeonis_engine PUBLIC src)
target_compile_definitions(aeonis_engine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE
    AEONIS_VERSION="${AEONIS_RELEASE_VERSION}")
# Cliente OAuth do Google ("App para computador") para o envio ao YouTube. Fica fora do repositório:
# vem das variáveis de ambiente AEONIS_YOUTUBE_CLIENT_ID/SECRET na configuração do CMake.
foreach(_var AEONIS_YOUTUBE_CLIENT_ID AEONIS_YOUTUBE_CLIENT_SECRET)
    if(DEFINED ENV{${_var}})
        set(${_var} "$ENV{${_var}}" CACHE STRING "" FORCE)
    else()
        set(${_var} "" CACHE STRING "")
    endif()
endforeach()
set_property(SOURCE src/share/YouTube.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    AEONIS_YOUTUBE_CLIENT_ID="${AEONIS_YOUTUBE_CLIENT_ID}" AEONIS_YOUTUBE_CLIENT_SECRET="${AEONIS_YOUTUBE_CLIENT_SECRET}")
if(NOT AEONIS_YOUTUBE_CLIENT_ID)
    message(STATUS "AEONIS_YOUTUBE_CLIENT_ID vazio: o envio para o YouTube fica desativado nesta compilação")
endif()

target_link_libraries(aeonis_engine PUBLIC
    Qt6::Core Qt6::Gui Qt6::Network
    aeonis_ffmpeg
    d3d11 dxgi ole32 mmdevapi avrt dwmapi gdi32 user32 shell32 version winmm)

# ---------------------------------------------------------------- app
qt_add_executable(Aeonis WIN32
    src/main.cpp
    src/app/App.cpp src/app/App.h
    src/app/TrayIcon.cpp src/app/TrayIcon.h
    src/app/AppIcon.cpp src/app/AppIcon.h
    src/app/AutoUpdater.cpp src/app/AutoUpdater.h
    src/app/WindowHost.cpp src/app/WindowHost.h
    src/app/UpdateSplash.cpp src/app/UpdateSplash.h
    src/ui/Backend.cpp src/ui/Backend.h
    src/ui/SettingsSchema.cpp src/ui/SettingsSchema.h
    "${CMAKE_CURRENT_BINARY_DIR}/aeonis.rc"
)
set_target_properties(Aeonis PROPERTIES OUTPUT_NAME "Aeonis")
qt_add_resources(Aeonis app_icons PREFIX "/" BASE assets FILES
    assets/icons/aeonis-32.png assets/icons/aeonis-64.png assets/icons/aeonis-256.png
    assets/icons/aeonis-recording-32.png assets/icons/aeonis-recording-64.png)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(Aeonis
    URI Aeonis
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Icon.qml
        qml/Toasts.qml
        qml/Sidebar.qml
        qml/GameAvatar.qml
        qml/ClipTile.qml
        qml/MatchesView.qml
        qml/ClipsView.qml
        qml/EmptyState.qml
        qml/PageHeader.qml
        qml/Player.qml
        qml/SettingsView.qml
        qml/AboutView.qml
        qml/SmoothScroll.qml
        qml/TitleBar.qml
        qml/UpdateSplash.qml
        qml/SettingRow.qml
        qml/AButton.qml
        qml/YouTubeDialog.qml
        qml/Format.js
)

target_link_libraries(Aeonis PRIVATE aeonis_engine Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)

# ---------------------------------------------------------------- testes
if(AEONIS_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(tst_core tests/tst_core.cpp)
    target_link_libraries(tst_core PRIVATE aeonis_engine Qt6::Test)
    add_test(NAME tst_core COMMAND tst_core)
    set_tests_properties(tst_core PROPERTIES TIMEOUT 120)
endif()
