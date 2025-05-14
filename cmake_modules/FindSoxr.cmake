# FindSoxr.cmake
#
# Finds the soxr library
#
# This will define the following variables
#
#    Soxr_FOUND
#    Soxr_INCLUDE_DIRS
#    Soxr_LIBRARIES
#
# and the following imported targets
#
#     Soxr::Soxr
#

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_Soxr QUIET soxr)
endif()

# Find include directory
find_path(Soxr_INCLUDE_DIR
  NAMES soxr.h
  PATHS
    ${PC_Soxr_INCLUDE_DIRS}
    /usr/include
    /usr/local/include
  PATH_SUFFIXES soxr
)

# Find library
find_library(Soxr_LIBRARY
  NAMES soxr libsoxr
  PATHS
    ${PC_Soxr_LIBRARY_DIRS}
    /usr/lib
    /usr/local/lib
    /usr/lib64
    /usr/local/lib64
)

# Set include directories and libraries
set(Soxr_INCLUDE_DIRS ${Soxr_INCLUDE_DIR})
set(Soxr_LIBRARIES ${Soxr_LIBRARY})

# Handle REQUIRED and QUIET arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Soxr
  REQUIRED_VARS Soxr_LIBRARY Soxr_INCLUDE_DIR
  VERSION_VAR PC_Soxr_VERSION
)

# Create imported target
if(Soxr_FOUND AND NOT TARGET Soxr::Soxr)
  add_library(Soxr::Soxr UNKNOWN IMPORTED)
  set_target_properties(Soxr::Soxr PROPERTIES
    IMPORTED_LOCATION "${Soxr_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${Soxr_INCLUDE_DIR}"
  )
endif()

# Mark as advanced
mark_as_advanced(Soxr_INCLUDE_DIR Soxr_LIBRARY)