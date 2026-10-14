# Headless static synth: Qt owns audio devices; C++ OS abstraction avoids GLib.
function(bitmusic_add_fluidsynth)
  set(gcem_archive "https://codeload.github.com/kthohr/gcem/zip/012ae73c6d0a2cb09ffe86475f5c6fba3926e200")
  if(EXISTS "${PROJECT_SOURCE_DIR}/.runtime/sources/gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200.zip")
    set(gcem_archive "${PROJECT_SOURCE_DIR}/.runtime/sources/gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200.zip")
  endif()
  FetchContent_Declare(gcem URL "${gcem_archive}"
    URL_HASH SHA256=28159274c54e9640354852e172d10d88eb159f4e7f2fea42edbcd20105ed3526
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR bitmusic-no-project)
  FetchContent_MakeAvailable(gcem)
  set(GCEM_INCLUDE_DIR "${gcem_SOURCE_DIR}/include" CACHE PATH "" FORCE)
  set(archive "https://codeload.github.com/FluidSynth/fluidsynth/tar.gz/refs/tags/v2.5.4")
  if(EXISTS "${PROJECT_SOURCE_DIR}/.runtime/sources/fluidsynth-2.5.4.tar.gz")
    set(archive "${PROJECT_SOURCE_DIR}/.runtime/sources/fluidsynth-2.5.4.tar.gz")
  endif()
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(osal cpp11 CACHE STRING "" FORCE)
  foreach(feature alsa aufile dbus jack ladspa libinstpatch libsndfile midishare
      network oss dsound wasapi waveout winmidi sdl3 pulseaudio pipewire readline
      threads openmp systemd coreaudio coremidi framework native-dls)
    set(enable-${feature} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(enable-unicode ON CACHE BOOL "" FORCE)
  FetchContent_Declare(fluidsynth URL "${archive}"
    URL_HASH SHA256=72f5720328fe44e2e5c67813885f0a6b4b004d048bd2eeeb0c0064074ebff530
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(fluidsynth)
  set_target_properties(fluidsynth PROPERTIES EXCLUDE_FROM_ALL TRUE)
  # The static library needs no Windows version resource. MinGW windres cannot
  # handle upstream's escaped include list when the checkout path has spaces.
  if(MINGW)
    get_target_property(fluid_sources libfluidsynth-OBJ SOURCES)
    list(FILTER fluid_sources EXCLUDE REGEX "\\.rc$")
    set_property(TARGET libfluidsynth-OBJ PROPERTY SOURCES "${fluid_sources}")
  endif()
endfunction()
bitmusic_add_fluidsynth()
