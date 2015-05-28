cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_FLAGS "-std=c++0x")
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/)

include(ExternalProject)

find_package(Folly REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(OpenSSL REQUIRED)

include_directories(
  ${CMAKE_SOURCE_DIR}
  ${FOLLY_INCLUDE_DIR}
)

set(WANGLE_HEADERS
  wangle/acceptor/Acceptor.h
  wangle/acceptor/ConnectionCounter.h
  wangle/acceptor/ConnectionManager.h
  wangle/acceptor/DomainNameMisc.h
  wangle/acceptor/LoadShedConfiguration.h
  wangle/acceptor/ManagedConnection.h
  wangle/acceptor/NetworkAddress.h
  wangle/acceptor/ServerSocketConfig.h
  wangle/acceptor/SocketOptions.h
  wangle/acceptor/TransportInfo.h
  wangle/bootstrap/ClientBootstrap.h
  wangle/bootstrap/ServerBootstrap-inl.h
  wangle/bootstrap/ServerBootstrap.h
  wangle/bootstrap/ServerSocketFactory.h
  wangle/channel/AsyncSocketHandler.h
  wangle/channel/EventBaseHandler.h
  wangle/channel/Handler.h
  wangle/channel/HandlerContext-inl.h
  wangle/channel/HandlerContext.h
  wangle/channel/OutputBufferingHandler.h
  wangle/channel/Pipeline-inl.h
  wangle/channel/Pipeline.h
  wangle/channel/StaticPipeline.h
  wangle/channel/test/MockHandler.h
  wangle/codec/ByteToMessageCodec.h
  wangle/codec/FixedLengthFrameDecoder.h
  wangle/codec/LengthFieldBasedFrameDecoder.h
  wangle/codec/LengthFieldPrepender.h
  wangle/codec/LineBasedFrameDecoder.h
  wangle/codec/StringCodec.h
  wangle/concurrent/BlockingQueue.h
  wangle/concurrent/CPUThreadPoolExecutor.h
  wangle/concurrent/Codel.h
  wangle/concurrent/FiberIOExecutor.h
  wangle/concurrent/FutureExecutor.h
  wangle/concurrent/GlobalExecutor.h
  wangle/concurrent/IOExecutor.h
  wangle/concurrent/IOThreadPoolExecutor.h
  wangle/concurrent/LifoSemMPMCQueue.h
  wangle/concurrent/NamedThreadFactory.h
  wangle/concurrent/PriorityLifoSemMPMCQueue.h
  wangle/concurrent/ThreadFactory.h
  wangle/concurrent/ThreadPoolExecutor.h
  wangle/rx/Observable.h
  wangle/rx/Observer.h
  wangle/rx/Subject.h
  wangle/rx/Subscription.h
  wangle/rx/types.h
  wangle/service/ClientDispatcher.h
  wangle/service/ServerDispatcher.h
  wangle/service/Service.h
  wangle/ssl/ClientHelloExtStats.h
  wangle/ssl/DHParam.h
  wangle/ssl/PasswordInFile.h
  wangle/ssl/SSLCacheOptions.h
  wangle/ssl/SSLCacheProvider.h
  wangle/ssl/SSLContextConfig.h
  wangle/ssl/SSLContextManager.h
  wangle/ssl/SSLSessionCacheManager.h
  wangle/ssl/SSLStats.h
  wangle/ssl/SSLUtil.h
  wangle/ssl/TLSTicketKeyManager.h
  wangle/ssl/TLSTicketKeySeeds.h
)

set(WANGLE_SOURCES
  wangle/acceptor/Acceptor.cpp
  wangle/acceptor/ConnectionManager.cpp
  wangle/acceptor/LoadShedConfiguration.cpp
  wangle/acceptor/ManagedConnection.cpp
  wangle/acceptor/SocketOptions.cpp
  wangle/acceptor/TransportInfo.cpp
  wangle/bootstrap/ServerBootstrap.cpp
  wangle/codec/ByteToMessageCodec.cpp
  wangle/codec/LengthFieldBasedFrameDecoder.cpp
  wangle/codec/LengthFieldPrepender.cpp
  wangle/codec/LineBasedFrameDecoder.cpp
  wangle/concurrent/CPUThreadPoolExecutor.cpp
  wangle/concurrent/Codel.cpp
  wangle/concurrent/GlobalExecutor.cpp
  wangle/concurrent/IOThreadPoolExecutor.cpp
  wangle/concurrent/ThreadPoolExecutor.cpp
  wangle/rx/Dummy.cpp
  wangle/ssl/PasswordInFile.cpp
  wangle/ssl/SSLContextManager.cpp
  wangle/ssl/SSLSessionCacheManager.cpp
  wangle/ssl/SSLUtil.cpp
  wangle/ssl/TLSTicketKeyManager.cpp
)

add_library(wangle STATIC
  ${WANGLE_HEADERS}
  ${WANGLE_SOURCES}
)

target_link_libraries(wangle
  ${FOLLY_LIBRARIES}
  ${Boost_LIBRARIES}
  ${OPENSSL_LIBRARIES}
  -lglog
  -lgflags)

install(TARGETS wangle DESTINATION lib)
install(DIRECTORY wangle/ DESTINATION include/wangle
        FILES_MATCHING PATTERN "*.h")

enable_testing()

# Download and install GoogleMock
ExternalProject_Add(
    gmock
    URL https://googlemock.googlecode.com/files/gmock-1.7.0.zip
    PREFIX ${CMAKE_SOURCE_DIR}/gmock
    # Disable install step
    INSTALL_COMMAND ""
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
)

# Create a libgmock target to be used as a dependency by test programs
add_library(libgmock IMPORTED STATIC GLOBAL)
add_dependencies(libgmock gmock)
add_library(libgmock_main IMPORTED STATIC GLOBAL)
add_dependencies(libgmock_main gmock)

# Set gmock properties
ExternalProject_Get_Property(gmock source_dir binary_dir)
set_target_properties(libgmock PROPERTIES
    "IMPORTED_LOCATION" "${binary_dir}/libgmock.a"
    "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
)
set_target_properties(libgmock_main PROPERTIES
    "IMPORTED_LOCATION" "${binary_dir}/libgmock_main.a"
    "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
)

include_directories("${source_dir}/include")
include_directories("${source_dir}/gtest/include")

macro(add_gtest test_source test_name)
add_executable(${test_name} ${test_source})
target_link_libraries(${test_name} wangle libgmock libgmock_main)
add_test(${test_name} ${test_name})
endmacro(add_gtest)

# this test segfaults
add_gtest(wangle/bootstrap/BootstrapTest.cpp BootstrapTest)
add_gtest(wangle/channel/test/OutputBufferingHandlerTest.cpp OutputBufferingHandlerTest)
add_gtest(wangle/channel/test/PipelineTest.cpp PipelineTest)
add_gtest(wangle/codec/CodecTest.cpp CodecTest)
add_gtest(wangle/concurrent/test/CodelTest.cpp CodelTest)
add_gtest(wangle/concurrent/test/GlobalExecutorTest.cpp GlobalExecutorTest)
add_gtest(wangle/concurrent/test/ThreadPoolExecutorTest ThreadPoolExecutorTest)
add_gtest(wangle/rx/test/RxTest.cpp RxTest)
# this test fails with an exception
add_gtest(wangle/service/ServiceTest.cpp ServiceTest)
# this test requires arguments?
add_gtest(wangle/ssl/test/SSLCacheTest.cpp SSLCacheTest)
add_gtest(wangle/ssl/test/SSLContextManagerTest.cpp SSLContextManagerTest)

option(BUILD_EXAMPLES "BUILD_EXAMPLES" OFF)

if(BUILD_EXAMPLES)
  add_executable(TelnetClient wangle/example/telnet/TelnetClient.cpp)
  target_link_libraries(TelnetClient wangle)
  add_executable(TelnetServer wangle/example/telnet/TelnetServer.cpp)
  target_link_libraries(TelnetServer wangle)
  add_executable(ProxyServer wangle/example/proxy/Proxy.cpp)
  target_link_libraries(ProxyServer wangle)
endif()
