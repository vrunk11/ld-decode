# FindLIBAV.cmake - Locate libavcodec, libavformat, libavutil on *nix & Windows
# SPDX-License-Identifier: BSD-3-Clause

include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)

# Attempt pkg-config first
if(PKG_CONFIG_FOUND)
  pkg_check_modules(_AVCODEC QUIET libavcodec)
  pkg_check_modules(_AVFORMAT QUIET libavformat)
  pkg_check_modules(_AVUTIL  QUIET libavutil)
endif()

# Define possible library basenames (unversioned + common Windows suffixes)
set(_AVCODEC_NAMES avcodec avcodec-61 avcodec-60)
set(_AVFORMAT_NAMES avformat avformat-61 avformat-60)
set(_AVUTIL_NAMES  avutil  avutil-59  avutil-58)

# Locate headers (take include dirs from pkg-config if present)
find_path(LIBAVCODEC_INCLUDE_DIR
  NAMES libavcodec/avcodec.h
  HINTS ${_AVCODEC_INCLUDE_DIRS}
        /usr/include /usr/local/include
        /opt/local/include /sw/include
        $ENV{MINGW_PREFIX}/include      # MSYS2/MingW
  PATH_SUFFIXES libav ffmpeg
)

# Locate import-libs / .a files
find_library(LIBAVCODEC_LIBRARY
  NAMES ${_AVCODEC_NAMES}
  HINTS ${_AVCODEC_LIBRARY_DIRS}
        /usr/lib /usr/local/lib
        /opt/local/lib /sw/lib
        $ENV{MINGW_PREFIX}/lib          # MSYS2/MingW :contentReference[oaicite:1]{index=1}
)

find_library(LIBAVFORMAT_LIBRARY
  NAMES ${_AVFORMAT_NAMES}
  HINTS ${_AVFORMAT_LIBRARY_DIRS}
        /usr/lib /usr/local/lib
        /opt/local/lib /sw/lib
        $ENV{MINGW_PREFIX}/lib
)

find_library(LIBAVUTIL_LIBRARY
  NAMES ${_AVUTIL_NAMES}
  HINTS ${_AVUTIL_LIBRARY_DIRS}
        /usr/lib /usr/local/lib
        /opt/local/lib /sw/lib
        $ENV{MINGW_PREFIX}/lib
)

# Aggregate results
set(LIBAV_LIBRARIES
  ${LIBAVCODEC_LIBRARY}
  ${LIBAVFORMAT_LIBRARY}
  ${LIBAVUTIL_LIBRARY}
)
set(LIBAV_INCLUDE_DIR ${LIBAVCODEC_INCLUDE_DIR})

# Tell CMake these vars are advanced by default
mark_as_advanced(
  LIBAVCODEC_INCLUDE_DIR
  LIBAVCODEC_LIBRARY
  LIBAVFORMAT_LIBRARY
  LIBAVUTIL_LIBRARY
  LIBAV_INCLUDE_DIR
  LIBAV_LIBRARIES
)

# Final check: require all three libraries and the include dir
find_package_handle_standard_args(LIBAV
  REQUIRED_VARS
    LIBAVCODEC_LIBRARY
    LIBAVCODEC_INCLUDE_DIR
    LIBAVFORMAT_LIBRARY
    LIBAVUTIL_LIBRARY
)

# Provide imported targets if you like:
if(LIBAV_FOUND)
  add_library(LIBAV::avcodec UNKNOWN IMPORTED)
  set_target_properties(LIBAV::avcodec PROPERTIES
    IMPORTED_LOCATION "${LIBAVCODEC_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBAV_INCLUDE_DIR}"
  )
  add_library(LIBAV::avformat UNKNOWN IMPORTED)
  set_target_properties(LIBAV::avformat PROPERTIES
    IMPORTED_LOCATION "${LIBAVFORMAT_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBAV_INCLUDE_DIR}"
  )
  add_library(LIBAV::avutil UNKNOWN IMPORTED)
  set_target_properties(LIBAV::avutil PROPERTIES
    IMPORTED_LOCATION "${LIBAVUTIL_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBAV_INCLUDE_DIR}"
  )
endif()
