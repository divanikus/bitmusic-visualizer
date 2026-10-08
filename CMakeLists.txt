cmake_minimum_required(VERSION 3.24)
project(BitMusicVisualizer VERSION 0.7.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets Multimedia QuickWidgets)
# Do not embed the build machine's checkout path in assertions/debug strings.
# Apply to dependency targets as well as the application; RC does not take these flags.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>"
                      "$<$<COMPILE_LANGUAGE:C,CXX>:-fdebug-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=./build>")
endif()
include(FetchContent)
# Cache archives locally; the same hashes are checked for online and offline builds.
set(GME_ARCHIVE "https://codeload.github.com/libgme/game-music-emu/tar.gz/refs/tags/0.6.5")
set(ZLIB_ARCHIVE "https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.3.2")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.runtime/sources/libgme-0.6.5.tar.gz")
  set(GME_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/.runtime/sources/libgme-0.6.5.tar.gz")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.runtime/sources/zlib-1.3.2.tar.gz")
  set(ZLIB_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/.runtime/sources/zlib-1.3.2.tar.gz")
endif()
option(BITMUSIC_SHARED_GME "Use a replaceable libgme DLL for portable distribution" OFF)
set(GME_BUILD_SHARED ${BITMUSIC_SHARED_GME} CACHE BOOL "" FORCE)
if(BITMUSIC_SHARED_GME)
  set(GME_BUILD_STATIC OFF CACHE BOOL "" FORCE)
else()
  set(GME_BUILD_STATIC ON CACHE BOOL "" FORCE)
endif()
set(GME_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GME_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GME_ZLIB OFF CACHE BOOL "" FORCE)
set(GME_YM2612_EMU Nuked CACHE STRING "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(gme URL "${GME_ARCHIVE}" URL_HASH SHA256=8531678502451c2cf04248cda45c8b4645e19fcfb63e6a7ec2549641c47bb392 DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(zlib URL "${ZLIB_ARCHIVE}" URL_HASH SHA256=b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1 DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(gme zlib)
include(cmake/PatchGme.cmake)
add_executable(bitmusic_visualizer native/main.cpp native/engine.cpp native/window.cpp native/playerwindow.cpp native/scopewindow.cpp native/scopegpu.cpp native/scopecolors.cpp native/scopetheme.cpp native/chrome.cpp native/checks.cpp native/tapchecks.cpp native/gbschecks.cpp native/aychecks.cpp)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/themes")
configure_file(packaging/THEMES.txt "${CMAKE_CURRENT_BINARY_DIR}/themes/README.txt" COPYONLY)
qt_add_resources(bitmusic_visualizer app_assets PREFIX "/" FILES assets/player.png)
target_sources(bitmusic_visualizer PRIVATE native/gpuchecks.cpp native/scopewave.cpp native/scopecontrols.cpp native/outputhistory.cpp native/wavechecks.cpp native/scopegrid.cpp native/scopelayout.cpp native/layoutchecks.cpp)
target_sources(bitmusic_visualizer PRIVATE native/pacingchecks.cpp native/spectrum.cpp native/scopeviews.cpp native/viewchecks.cpp)
if(WIN32)
  enable_language(RC)
  configure_file(native/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
  # Isolate windres from dependency include paths containing spaces (MinGW quoting).
  add_library(bitmusic_icon OBJECT "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
  target_sources(bitmusic_visualizer PRIVATE $<TARGET_OBJECTS:bitmusic_icon>)
endif()
set_target_properties(bitmusic_visualizer PROPERTIES WIN32_EXECUTABLE TRUE)
target_link_libraries(bitmusic_visualizer PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::QuickWidgets gme::gme ZLIB::ZLIBSTATIC)
target_include_directories(bitmusic_visualizer PRIVATE "${gme_SOURCE_DIR}/gme")
if(WIN32 AND BITMUSIC_SHARED_GME)
  add_custom_command(TARGET bitmusic_visualizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:gme_shared> $<TARGET_FILE_DIR:bitmusic_visualizer>)
endif()
if(MINGW)
  target_compile_options(bitmusic_visualizer PRIVATE -Wall -Wextra)
endif()

if(APPLE)
  set_target_properties(bitmusic_visualizer PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER org.bitmusicvisualizer.BitMusicVisualizer
    MACOSX_BUNDLE_BUNDLE_NAME "Bit Music Visualizer"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE player.icns
    INSTALL_RPATH "@executable_path/../Frameworks")
  set_source_files_properties(assets/player.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  target_sources(bitmusic_visualizer PRIVATE assets/player.icns)
elseif(UNIX)
  set_target_properties(bitmusic_visualizer PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()

# Windows uses package-native.ps1. Unix packages use this Runtime component only,
# keeping dependency headers, static archives and development tools out of bundles.
if(UNIX AND BITMUSIC_SHARED_GME)
  include(GNUInstallDirs)
  install(TARGETS bitmusic_visualizer BUNDLE DESTINATION . COMPONENT Runtime
    RUNTIME DESTINATION bin COMPONENT Runtime)
  if(APPLE)
    install(TARGETS gme_shared LIBRARY DESTINATION bitmusic_visualizer.app/Contents/Frameworks COMPONENT Runtime)
  else()
    install(TARGETS gme_shared LIBRARY DESTINATION lib COMPONENT Runtime)
    qt_generate_deploy_app_script(TARGET bitmusic_visualizer OUTPUT_SCRIPT deploy_script
      NO_TRANSLATIONS
      INCLUDE_PLUGINS qoffscreen
      EXCLUDE_PLUGIN_TYPES multimedia imageformats iconengines networkinformation tls
                           generic styles qmltooling platformthemes egldeviceintegrations
                           wayland-decoration-client wayland-graphics-integration-client
                           wayland-shell-integration
      EXCLUDE_PLUGINS qwayland qwayland-egl qvnc qeglfs qlinuxfb)
    install(SCRIPT ${deploy_script} COMPONENT Runtime)
  endif()
endif()
