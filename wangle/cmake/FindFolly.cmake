# Copyright (c) Facebook, Inc. and its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# - Try to find folly
# This will define
# FOLLY_FOUND
# FOLLY_INCLUDE_DIR
# FOLLY_LIBRARIES

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.7 FATAL_ERROR)

INCLUDE(FindPackageHandleStandardArgs)

FIND_LIBRARY(FOLLY_LIBRARY folly PATHS ${FOLLY_LIBRARYDIR})
FIND_PATH(FOLLY_INCLUDE_DIR "folly/String.h" PATHS ${FOLLY_INCLUDEDIR})

SET(FOLLY_LIBRARIES ${FOLLY_LIBRARY})

FIND_PACKAGE_HANDLE_STANDARD_ARGS(Folly
  REQUIRED_ARGS FOLLY_INCLUDE_DIR FOLLY_LIBRARIES)
