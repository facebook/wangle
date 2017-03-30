# - Try to find libdl
# Once done, this will define
#
# LIBDL_FOUND - system has libdl
# LIBDL_LIBRARIES - link these to use libdl

include(FindPackageHandleStandardArgs)

find_library(LIBDL_LIBRARY dl
  PATHS ${LIBDL_LIBRARYDIR})

find_package_handle_standard_args(libdl DEFAULT_MSG LIBDL_LIBRARY)

mark_as_advanced(LIBDL_LIBRARY)

set(LIBDL_LIBRARIES ${LIBDL_LIBRARY})
