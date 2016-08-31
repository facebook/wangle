#  Copyright (c) 2014, Facebook, Inc.
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree. An additional grant
#  of patent rights can be found in the PATENTS file in the same directory.

cmake_minimum_required(VERSION 2.8)

add_compile_options(-std=c++1y)
add_compile_options(-fPIC)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Folly REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread filesystem)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_library(GLOG_LIBRARY_PATH glog)
find_library(GFLAGS_LIBRARY_PATH gflags)

include_directories(
  ${CMAKE_SOURCE_DIR}/..
  ${FOLLY_INCLUDE_DIR}
  ${BOOST_INCLUDE_DIR}
  ${OPENSSL_INCLUDE_DIR}
  ${INCLUDE_DIR}
)

set(WANGLE_HEADER_DIRS
  acceptor
  bootstrap
  channel
  codec
  concurrent
  deprecated
  service
  ssl
)

foreach(dir ${WANGLE_HEADER_DIRS})
  file(GLOB_RECURSE headers ${dir}/*.h)
  set(WANGLE_HEADERS
    ${WANGLE_HEADERS}
    ${headers})
endforeach()

set(WANGLE_SOURCES
  acceptor/Acceptor.cpp
  acceptor/AcceptorHandshakeManager.cpp
  acceptor/ConnectionManager.cpp
  acceptor/LoadShedConfiguration.cpp
  acceptor/ManagedConnection.cpp
  acceptor/SecureTransportType.cpp
  acceptor/SocketOptions.cpp
  acceptor/SSLAcceptorHandshakeHelper.cpp
  acceptor/TLSPlaintextPeekingCallback.cpp
  acceptor/TransportInfo.cpp
  bootstrap/ServerBootstrap.cpp
  channel/FileRegion.cpp
  channel/Pipeline.cpp
  codec/LengthFieldBasedFrameDecoder.cpp
  codec/LengthFieldPrepender.cpp
  codec/LineBasedFrameDecoder.cpp
  concurrent/ThreadedExecutor.cpp
  concurrent/CPUThreadPoolExecutor.cpp
  concurrent/Codel.cpp
  concurrent/GlobalExecutor.cpp
  concurrent/IOThreadPoolExecutor.cpp
  concurrent/ThreadPoolExecutor.cpp
  deprecated/rx/Dummy.cpp
  ssl/PasswordInFile.cpp
  ssl/SSLContextManager.cpp
  ssl/SSLSessionCacheManager.cpp
  ssl/SSLUtil.cpp
  ssl/TLSTicketKeyManager.cpp
)

add_library(wangle STATIC
  ${WANGLE_HEADERS}
  ${WANGLE_SOURCES}
)

target_link_libraries(wangle
  ${FOLLY_LIBRARIES}
  ${Boost_LIBRARIES}
  ${OPENSSL_LIBRARIES}
  ${GLOG_LIBRARY_PATH}
  ${GFLAGS_LIBRARY_PATH}
  $<$<NOT:$<CXX_COMPILER_ID:Clang>>:-latomic>)

install(TARGETS wangle DESTINATION lib)
foreach(dir ${WANGLE_HEADER_DIRS})
  install(DIRECTORY ${dir} DESTINATION include/wangle
          FILES_MATCHING PATTERN "*.h")
endforeach()

IF(CMAKE_CROSSCOMPILING)
   option(BUILD_TESTS "BUILD_TESTS" OFF)
ELSE(CMAKE_CROSSCOMPILING)
   option(BUILD_TESTS "BUILD_TESTS" ON)
ENDIF(CMAKE_CROSSCOMPILING)

if(BUILD_TESTS)
  enable_testing()

  include(ExternalProject)

# Download and install GoogleMock
  ExternalProject_Add(
      gtest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG release-1.8.0
      PREFIX gtest
      # Disable install step
      INSTALL_COMMAND ""
      LOG_DOWNLOAD ON
      LOG_UPDATE 1
      LOG_CONFIGURE ON
      LOG_BUILD ON
      LOG_TEST 1
      LOG_INSTALL 1
  )

  # Create a libgmock target to be used as a dependency by test programs
  add_library(libgmock IMPORTED STATIC GLOBAL)
  add_dependencies(libgmock gtest)
  add_library(libgmock_main IMPORTED STATIC GLOBAL)
  add_dependencies(libgmock_main gtest)

  # Set gmock properties
  ExternalProject_Get_Property(gtest source_dir binary_dir)
  set_target_properties(libgmock PROPERTIES
      "IMPORTED_LOCATION" "${binary_dir}/googlemock/libgmock.a"
      "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
  )
  set_target_properties(libgmock_main PROPERTIES
      "IMPORTED_LOCATION" "${binary_dir}/googlemock/libgmock_main.a"
      "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
  )

  include_directories("${source_dir}/googlemock/include")
  include_directories("${source_dir}/googletest/include")

  macro(add_gtest test_source test_name)
  add_executable(${test_name} ${test_source})
  target_link_libraries(${test_name} wangle libgmock libgmock_main)
  add_test(${test_name} bin/${test_name})
  endmacro(add_gtest)

  # this test segfaults
  add_gtest(acceptor/test/AcceptorTest.cpp AcceptorTest)
  add_gtest(acceptor/test/ConnectionManagerTest.cpp ConnectionManagerTest)
  add_gtest(acceptor/test/LoadShedConfigurationTest.cpp LoadShedConfigurationTest)
  add_gtest(acceptor/test/PeekingAcceptorHandshakeHelperTest.cpp PeekingAcceptorHandshakeHelperTest)
  add_gtest(bootstrap/BootstrapTest.cpp BootstrapTest)
  add_gtest(channel/broadcast/test/BroadcastHandlerTest.cpp BroadcastHandlerTest)
  add_gtest(channel/broadcast/test/BroadcastPoolTest.cpp BroadcastPoolTest)
  add_gtest(channel/broadcast/test/ObservingHandlerTest.cpp ObservingHandlerTest)
  add_gtest(channel/test/AsyncSocketHandlerTest.cpp AsyncSocketHandlerTest)
  add_gtest(channel/test/OutputBufferingHandlerTest.cpp OutputBufferingHandlerTest)
  add_gtest(channel/test/PipelineTest.cpp PipelineTest)
  add_gtest(codec/CodecTest.cpp CodecTest)
  add_gtest(concurrent/test/AsyncTest.cpp AsyncTest)
  add_gtest(concurrent/test/ThreadedExecutorTest.cpp ThreadedExecutorTest)
  add_gtest(concurrent/test/CodelTest.cpp CodelTest)
  add_gtest(concurrent/test/GlobalExecutorTest.cpp GlobalExecutorTest)
  add_gtest(concurrent/test/ThreadPoolExecutorTest ThreadPoolExecutorTest)
  add_gtest(deprecated/rx/test/RxTest.cpp RxTest)
  # this test fails with an exception
  #  add_gtest(service/ServiceTest.cpp ServiceTest)
  # this test requires arguments?
  #  add_gtest(ssl/test/SSLCacheTest.cpp SSLCacheTest)
  add_gtest(ssl/test/SSLContextManagerTest.cpp SSLContextManagerTest)
endif()

option(BUILD_EXAMPLES "BUILD_EXAMPLES" OFF)

if(BUILD_EXAMPLES)
  add_executable(EchoClient example/echo/EchoClient.cpp)
  target_link_libraries(EchoClient wangle)
  add_executable(EchoServer example/echo/EchoServer.cpp)
  target_link_libraries(EchoServer wangle)
  add_executable(TelnetClient example/telnet/TelnetClient.cpp)
  target_link_libraries(TelnetClient wangle)
  add_executable(TelnetServer example/telnet/TelnetServer.cpp)
  target_link_libraries(TelnetServer wangle)
  add_executable(ProxyServer example/proxy/Proxy.cpp)
  target_link_libraries(ProxyServer wangle)
  add_executable(AcceptServer example/accept_steering/accept_steering_server.cpp)
  target_link_libraries(AcceptServer wangle)
  add_executable(BroadcastProxy example/broadcast/BroadcastProxy.cpp)
  target_link_libraries(BroadcastProxy wangle)
endif()
