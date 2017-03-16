# - Try to find double-conversion
# Once done, this will define
#
# DOUBLE_CONVERSION_FOUND - system has double-conversion
# DOUBLE_CONVERSION_INCLUDE_DIRS - the double-conversion include directories
# DOUBLE_CONVERSION_LIBRARIES - link these to use double-conversion

include(FindPackageHandleStandardArgs)

find_library(DOUBLE_CONVERSION_LIBRARY double-conversion
  PATHS ${DOUBLE_CONVERSION_LIBRARYDIR})

find_path(DOUBLE_CONVERSION_INCLUDE_DIR double-conversion/double-conversion.h
  PATHS ${DOUBLE_CONVERSION_INCLUDEDIR})

find_package_handle_standard_args(double_conversion DEFAULT_MSG
  DOUBLE_CONVERSION_LIBRARY
  DOUBLE_CONVERSION_INCLUDE_DIR)

mark_as_advanced(
  DOUBLE_CONVERSION_LIBRARY
  DOUBLE_CONVERSION_INCLUDE_DIR)

set(DOUBLE_CONVERSION_LIBRARIES ${DOUBLE_CONVERSION_LIBRARY})
set(DOUBLE_CONVERSION_INCLUDE_DIRS ${DOUBLE_CONVERSION_INCLUDE_DIR})
