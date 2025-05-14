#--- Begin FindSoxr.cmake ---------------------------
#
# FindSoxr.cmake — locate the libsoxr resampling library
#
# Defines:
#   Soxr_FOUND
#   Soxr_INCLUDE_DIRS
#   Soxr_LIBRARIES
#   Imported target: Soxr::soxr
#
include(CMakeFindDependencyMacro)   # for pkg_check_modules()
find_package(PkgConfig QUIET)
pkg_check_modules(PC_Soxr QUIET soxr)

# Allow user to set a root hint:
if(NOT DEFINED Soxr_ROOT)
  set(Soxr_ROOT "")
endif()

find_path(Soxr_INCLUDE_DIRS
  NAMES soxr.h
  HINTS ${Soxr_ROOT}
        ${PC_Soxr_INCLUDEDIR}
  PATH_SUFFIXES include
)

find_library(Soxr_LIBRARIES
  NAMES soxr
  HINTS ${Soxr_ROOT}
        ${PC_Soxr_LIBDIR}
  PATH_SUFFIXES lib
)

# Handle required args, set Soxr_FOUND
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Soxr
    REQUIRED_VARS Soxr_LIBRARIES Soxr_INCLUDE_DIRS
    VERSION_VAR Soxr_VERSION_STRING
)

mark_as_advanced(
  Soxr_ROOT
  Soxr_INCLUDE_DIRS
  Soxr_LIBRARIES
)

# Create an imported target for modern CMake usage
if(Soxr_FOUND AND NOT TARGET Soxr::soxr)
  add_library(Soxr::soxr UNKNOWN IMPORTED)
  set_target_properties(Soxr::soxr PROPERTIES
    IMPORTED_LOCATION "${Soxr_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${Soxr_INCLUDE_DIRS}"
    # on systems where pkg-config lists extra cflags
    INTERFACE_COMPILE_OPTIONS "${PC_Soxr_CFLAGS_OTHER}"
  )
endif()
#--- End FindSoxr.cmake -----------------------------
