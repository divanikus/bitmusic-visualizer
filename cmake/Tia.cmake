# Only the Stella audio core shipped with TIATracker; no editor, SDL or device IO.
set(tia_archive "https://deb.debian.org/debian/pool/main/t/tiatracker/tiatracker_1.3.orig.tar.xz")
if(EXISTS "${PROJECT_SOURCE_DIR}/.runtime/sources/tiatracker-1.3.tar.xz")
  set(tia_archive "${PROJECT_SOURCE_DIR}/.runtime/sources/tiatracker-1.3.tar.xz")
endif()
FetchContent_Declare(tiatracker URL "${tia_archive}"
  URL_HASH SHA256=68e2a6aa2cc2d69a13ebed1a1069ba04bd902ea2d9ac3fe084647e4d8d20f110
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR bitmusic-no-project)
FetchContent_MakeAvailable(tiatracker)
add_library(bitmusic_tia STATIC "${tiatracker_SOURCE_DIR}/emulation/TIASnd.cpp"
  "${PROJECT_SOURCE_DIR}/native/tia-replay/replay.cpp")
target_include_directories(bitmusic_tia PUBLIC "${tiatracker_SOURCE_DIR}/emulation")
target_link_libraries(bitmusic_tia PRIVATE Qt6::Core)
