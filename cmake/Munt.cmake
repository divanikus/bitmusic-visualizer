# Static, headless Munt. Qt owns devices and the player owns SMF scheduling.
function(bitmusic_add_munt)
  set(archive "https://codeload.github.com/munt/munt/tar.gz/refs/tags/libmt32emu_2_8_3")
  if(EXISTS "${PROJECT_SOURCE_DIR}/.runtime/sources/munt-libmt32emu-2.8.3.tar.gz")
    set(archive "${PROJECT_SOURCE_DIR}/.runtime/sources/munt-libmt32emu-2.8.3.tar.gz")
  endif()
  FetchContent_Declare(munt URL "${archive}"
    URL_HASH SHA256=81f8c462f46bc8901618762ae34cf9de93894ff81f41db73c79472fa3baef875
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR bitmusic-no-project)
  FetchContent_MakeAvailable(munt)
  include(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/PatchMunt.cmake)
  set(libmt32emu_SHARED OFF CACHE BOOL "" FORCE)
  set(libmt32emu_C_INTERFACE OFF CACHE BOOL "" FORCE)
  set(libmt32emu_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(libmt32emu_REQUIRE_ANSI OFF CACHE BOOL "" FORCE)
  add_subdirectory("${munt_SOURCE_DIR}/mt32emu" "${munt_BINARY_DIR}" EXCLUDE_FROM_ALL)
endfunction()
bitmusic_add_munt()
