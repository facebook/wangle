# - Try to find Gflags
# Once done, this will define
#
# GFLAGS_FOUND - system has Gflags
# GFLAGS_INCLUDE_DIRS - the Gflags include directories
# GFLAGS_LIBRARIES - link these to use Gflags

include(FindPackageHandleStandardArgs)

find_library(GFLAGS_LIBRARY gflags
  PATHS ${GFLAGS_LIBRARYDIR})

find_path(GFLAGS_INCLUDE_DIR gflags/gflags.h
  PATHS ${GFLAGS_INCLUDEDIR})

find_package_handle_standard_args(gflags DEFAULT_MSG
  GFLAGS_LIBRARY
  GFLAGS_INCLUDE_DIR)

mark_as_advanced(
  GFLAGS_LIBRARY
  GFLAGS_INCLUDE_DIR)

set(GFLAGS_LIBRARIES ${GFLAGS_LIBRARY})
set(GFLAGS_INCLUDE_DIRS ${GFLAGS_INCLUDE_DIR})
