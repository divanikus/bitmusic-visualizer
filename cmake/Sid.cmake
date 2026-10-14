# CMake adapter for the complete 2.16.1 release (includes generated 6502 drivers).
# Only the software reSIDfp builder; no USB/hardware drivers or STIL library.
set(SID_ARCHIVE "https://github.com/libsidplayfp/libsidplayfp/releases/download/v2.16.1/libsidplayfp-2.16.1.tar.gz")
if(EXISTS "${PROJECT_SOURCE_DIR}/.runtime/sources/libsidplayfp-2.16.1.tar.gz")
  set(SID_ARCHIVE "${PROJECT_SOURCE_DIR}/.runtime/sources/libsidplayfp-2.16.1.tar.gz")
endif()
FetchContent_Declare(sid URL "${SID_ARCHIVE}" URL_HASH SHA256=ace0f73c2ef8645ab069ce1b298b10e31e36af7b5996109983b2b67ad60ff3ca DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sid)
include(${CMAKE_CURRENT_LIST_DIR}/PatchSid.cmake)
set(PACKAGE_VERSION 2.16.1)
set(LIB_MAJOR 2)
set(LIB_MINOR 16)
set(LIB_LEVEL 1)
set(RESID_BRANCH_HINTS 0)
set(HAVE_BUILTIN_EXPECT 0)
set(RESID_INLINING 1)
set(RESID_INLINE inline)
configure_file("${sid_SOURCE_DIR}/src/sidplayfp/sidversion.h.in" "${sid_BINARY_DIR}/sidplayfp/sidversion.h" @ONLY)
configure_file("${sid_SOURCE_DIR}/src/builders/residfp-builder/residfp/siddefs-fp.h.in" "${sid_BINARY_DIR}/residfp/siddefs-fp.h" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/sid-config.h.in" "${sid_BINARY_DIR}/config.h" @ONLY)
set(sid_sources
  src/EventScheduler.cpp
  src/player.cpp
  src/psiddrv.cpp
  src/mixer.cpp
  src/reloc65.cpp
  src/simpleMixer.cpp
  src/sidemu.cpp
  src/c64/c64.cpp
  src/c64/mmu.cpp
  src/c64/VIC_II/mos656x.cpp
  src/c64/CPU/mos6510.cpp
  src/c64/CPU/mos6510debug.cpp
  src/c64/CIA/interrupt.cpp
  src/c64/CIA/mos652x.cpp
  src/c64/CIA/SerialPort.cpp
  src/c64/CIA/timer.cpp
  src/c64/CIA/tod.cpp
  src/sidplayfp/sidplayfp.cpp
  src/sidplayfp/sidbuilder.cpp
  src/sidplayfp/SidConfig.cpp
  src/sidplayfp/SidInfo.cpp
  src/sidplayfp/SidTune.cpp
  src/sidplayfp/SidTuneInfo.cpp
  src/sidtune/MUS.cpp
  src/sidtune/p00.cpp
  src/sidtune/prg.cpp
  src/sidtune/PSID.cpp
  src/sidtune/SidTuneBase.cpp
  src/sidtune/SidTuneTools.cpp
  src/utils/iniParser.cpp
  src/utils/md5Factory.cpp
  src/utils/SidDatabase.cpp
  src/builders/residfp-builder/residfp/Dac.cpp
  src/builders/residfp-builder/residfp/EnvelopeGenerator.cpp
  src/builders/residfp-builder/residfp/ExternalFilter.cpp
  src/builders/residfp-builder/residfp/Filter.cpp
  src/builders/residfp-builder/residfp/Filter6581.cpp
  src/builders/residfp-builder/residfp/Filter8580.cpp
  src/builders/residfp-builder/residfp/FilterModelConfig.cpp
  src/builders/residfp-builder/residfp/FilterModelConfig6581.cpp
  src/builders/residfp-builder/residfp/FilterModelConfig8580.cpp
  src/builders/residfp-builder/residfp/Integrator6581.cpp
  src/builders/residfp-builder/residfp/Integrator8580.cpp
  src/builders/residfp-builder/residfp/OpAmp.cpp
  src/builders/residfp-builder/residfp/SID.cpp
  src/builders/residfp-builder/residfp/Spline.cpp
  src/builders/residfp-builder/residfp/WaveformCalculator.cpp
  src/builders/residfp-builder/residfp/WaveformGenerator.cpp
  src/builders/residfp-builder/residfp/resample/SincResampler.cpp
  src/builders/residfp-builder/residfp/version.cc
  src/builders/residfp-builder/residfp-builder.cpp
  src/builders/residfp-builder/residfp-emu.cpp
  src/utils/MD5/MD5.cpp
)
list(TRANSFORM sid_sources PREPEND "${sid_SOURCE_DIR}/")
add_library(bitmusic_sid STATIC ${sid_sources} "${PROJECT_SOURCE_DIR}/native/sid-taps/taps.cpp")
target_compile_definitions(bitmusic_sid PRIVATE HAVE_CONFIG_H)
target_include_directories(bitmusic_sid PUBLIC "${sid_SOURCE_DIR}/src" "${sid_BINARY_DIR}"
  "${sid_SOURCE_DIR}/src/builders/residfp-builder"
  PRIVATE "${sid_BINARY_DIR}/residfp" "${sid_SOURCE_DIR}/src/sidtune" "${PROJECT_SOURCE_DIR}/native/sid-taps")
find_package(Threads REQUIRED)
target_link_libraries(bitmusic_sid PRIVATE Threads::Threads)
