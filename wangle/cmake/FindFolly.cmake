#  Copyright (c) 2014, Facebook, Inc.
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree. An additional grant
#  of patent rights can be found in the PATENTS file in the same directory.
#
# - Try to find folly
# This will define
# FOLLY_FOUND
# FOLLY_INCLUDE_DIR
# FOLLY_LIBRARIES

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.7 FATAL_ERROR)

INCLUDE(FindPackageHandleStandardArgs)

FIND_LIBRARY(FOLLY_LIBRARY folly)
FIND_PATH(FOLLY_INCLUDE_DIR "folly/String.h")

SET(FOLLY_LIBRARIES ${FOLLY_LIBRARY})

FIND_PACKAGE_HANDLE_STANDARD_ARGS(Folly
  REQUIRED_ARGS FOLLY_INCLUDE_DIR FOLLY_LIBRARIES)
