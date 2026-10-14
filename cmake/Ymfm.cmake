# Unmodified BSD-3-Clause cores. Pin source and archive digest for all platforms.
set(YMFM_REVISION 81aec25ccbb98f4873a255f7551ac4dadac59b4a)
set(YMFM_ARCHIVE "https://codeload.github.com/aaronsgiles/ymfm/tar.gz/${YMFM_REVISION}")
if(EXISTS "${PROJECT_SOURCE_DIR}/.runtime/sources/ymfm-${YMFM_REVISION}.tar.gz")
  set(YMFM_ARCHIVE "${PROJECT_SOURCE_DIR}/.runtime/sources/ymfm-${YMFM_REVISION}.tar.gz")
endif()
FetchContent_Declare(ymfm URL "${YMFM_ARCHIVE}"
  URL_HASH SHA256=9bcf001b30f53754c49cf2fb864792a64702b8aeebb105baa908ad5dd33f443f
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(ymfm)
add_library(bitmusic_ymfm STATIC
  "${ymfm_SOURCE_DIR}/src/ymfm_opn.cpp" "${ymfm_SOURCE_DIR}/src/ymfm_opm.cpp"
  "${ymfm_SOURCE_DIR}/src/ymfm_opl.cpp" "${ymfm_SOURCE_DIR}/src/ymfm_ssg.cpp"
  "${ymfm_SOURCE_DIR}/src/ymfm_adpcm.cpp" "${ymfm_SOURCE_DIR}/src/ymfm_pcm.cpp")
target_include_directories(bitmusic_ymfm SYSTEM PUBLIC "${ymfm_SOURCE_DIR}/src")
target_compile_features(bitmusic_ymfm PUBLIC cxx_std_17)
