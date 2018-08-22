# - Try to find fizz
# Once done, this will define
#
# FIZZ_FOUND - system has Fizz
# FIZZ_INCLUDE_DIRS - the Fizz include directories
# FIZZ_LIBRARIES - link these to use Fizz

include(FindPackageHandleStandardArgs)

find_library(FIZZ_LIBRARY fizz PATHS ${FIZZ_LIBRARYDIR})

find_path(FIZZ_INCLUDE_DIR fizz/protocol/Protocol.h PATHS ${FIZZ_INCLUDEDIR})

find_package_handle_standard_args(fizz DEFAULT_MSG
  FIZZ_LIBRARY
  FIZZ_INCLUDE_DIR)

set(FIZZ_LIBRARIES ${FIZZ_LIBRARY})
set(FIZZ_INCLUDE_DIRS ${FIZZ_INCLUDE_DIR})
