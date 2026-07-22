cmake_minimum_required(VERSION 3.20)
project(tts_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LWS REQUIRED IMPORTED_TARGET libwebsockets)

pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)

pkg_check_modules(NLOHMANN_JSON REQUIRED IMPORTED_TARGET nlohmann_json)
pkg_check_modules(SPDLOG REQUIRED IMPORTED_TARGET spdlog)

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    find_path(GTEST_INCLUDE_DIR gtest/gtest.h REQUIRED)
    find_library(GTEST_LIBRARY NAMES gtest REQUIRED)
    find_library(GTEST_MAIN_LIBRARY NAMES gtest_main REQUIRED)
    add_library(GTest::gtest UNKNOWN IMPORTED)
    set_target_properties(GTest::gtest PROPERTIES
        IMPORTED_LOCATION "${GTEST_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}")
    add_library(GTest::gtest_main UNKNOWN IMPORTED)
    set_target_properties(GTest::gtest_main PROPERTIES
        IMPORTED_LOCATION "${GTEST_MAIN_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}")
endif()

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/tts.proto)
set(PROTO_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/pb2)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

set(PROTO_SRCS
    ${PROTO_GEN_DIR}/tts.pb.cc
    ${PROTO_GEN_DIR}/tts.grpc.pb.cc
)
set(PROTO_HDRS
    ${PROTO_GEN_DIR}/tts.pb.h
    ${PROTO_GEN_DIR}/tts.grpc.pb.h
)

find_program(PROTOC_BIN protoc)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)

option(TTS_SHERPA_LOCAL "Enable sherpa-onnx local TTS engine" ON)
if(TTS_SHERPA_LOCAL)
    set(SHERPA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/sherpa-onnx-v1.13.4")
    if(EXISTS "${SHERPA_DIR}/lib/libsherpa-onnx-cxx-api.so" OR
       EXISTS "${SHERPA_DIR}/lib/sherpa-onnx-core.lib" OR
       EXISTS "${SHERPA_DIR}/lib/libsherpa-onnx-cxx-api.dylib")
        if(EXISTS "${SHERPA_DIR}/lib/libsherpa-onnx-cxx-api.so")
            add_library(sherpa-onnx SHARED IMPORTED)
            set_target_properties(sherpa-onnx PROPERTIES
                IMPORTED_LOCATION "${SHERPA_DIR}/lib/libsherpa-onnx-cxx-api.so"
                INTERFACE_INCLUDE_DIRECTORIES "${SHERPA_DIR}/include")
            add_library(sherpa-onnx::sherpa-onnx ALIAS sherpa-onnx)
        elseif(EXISTS "${SHERPA_DIR}/lib/sherpa-onnx-core.lib")
            add_library(sherpa-onnx STATIC IMPORTED)
            set_target_properties(sherpa-onnx PROPERTIES
                IMPORTED_LOCATION "${SHERPA_DIR}/lib/sherpa-onnx-core.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${SHERPA_DIR}/include")
            add_library(sherpa-onnx::sherpa-onnx ALIAS sherpa-onnx)
        elseif(EXISTS "${SHERPA_DIR}/lib/libsherpa-onnx-cxx-api.dylib")
            add_library(sherpa-onnx SHARED IMPORTED)
            set_target_properties(sherpa-onnx PROPERTIES
                IMPORTED_LOCATION "${SHERPA_DIR}/lib/libsherpa-onnx-cxx-api.dylib"
                INTERFACE_INCLUDE_DIRECTORIES "${SHERPA_DIR}/include")
            add_library(sherpa-onnx::sherpa-onnx ALIAS sherpa-onnx)
        endif()
        message(STATUS "sherpa-onnx found at ${SHERPA_DIR}")
    else()
        message(WARNING "sherpa-onnx not found at ${SHERPA_DIR}; local TTS will use Mock fallback. Run scripts/install_sherpa_onnx.sh to install.")
        set(TTS_SHERPA_LOCAL OFF)
    endif()
endif()

add_custom_command(
    OUTPUT ${PROTO_SRCS}
    COMMAND ${PROTOC_BIN}
        --cpp_out=${PROTO_GEN_DIR}
        --grpc_out=${PROTO_GEN_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating protobuf+gRPC stubs for tts.proto"
    VERBATIM
)

add_library(tts_proto_gen STATIC ${PROTO_SRCS})
target_include_directories(tts_proto_gen PUBLIC
    ${PROTO_GEN_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${GRPC_INCLUDE_DIRS}
)
target_link_libraries(tts_proto_gen PUBLIC
    PkgConfig::PROTOBUF
    PkgConfig::GRPCPP
)

add_library(tts_framework STATIC
    src/framework/event_bus.cpp
    src/framework/task.cpp
)
target_include_directories(tts_framework PUBLIC src)
target_link_libraries(tts_framework PUBLIC
    Threads::Threads
    PkgConfig::SPDLOG
)

add_library(tts_databoard INTERFACE)
target_include_directories(tts_databoard INTERFACE src)
target_link_libraries(tts_databoard INTERFACE tts_framework)

add_library(tts_upstream STATIC
    src/upstream/ws_frame_codec.cpp
    src/upstream/lws_transport.cpp
)
target_include_directories(tts_upstream PUBLIC src)
target_link_libraries(tts_upstream
    PUBLIC tts_framework Threads::Threads PkgConfig::SPDLOG PkgConfig::NLOHMANN_JSON
    PRIVATE PkgConfig::LWS
)

add_library(tts_heartbeat INTERFACE)
target_include_directories(tts_heartbeat INTERFACE src)
target_link_libraries(tts_heartbeat INTERFACE tts_framework tts_databoard tts_upstream tts_local)

add_library(tts_local STATIC
    src/local/audio_resampler.cpp
    src/local/mock_local_tts.cpp
    src/local/sherpa_local_tts.cpp
    src/local/local_tts_factory.cpp
)
target_include_directories(tts_local PUBLIC src)
target_link_libraries(tts_local
    PUBLIC tts_framework
    PRIVATE PkgConfig::SPDLOG
)
if(TTS_SHERPA_LOCAL)
    target_compile_definitions(tts_local PRIVATE TTS_HAS_SHERPA_ONNX=1)
endif()
if(TARGET sherpa-onnx::sherpa-onnx)
    target_link_libraries(tts_local PRIVATE sherpa-onnx::sherpa-onnx)
endif()

add_library(tts_entry STATIC
    src/entry/request_parser.cpp
    src/entry/grpc_response_writer.cpp
    src/entry/jwt_auth_interceptor.cpp
)
target_include_directories(tts_entry PUBLIC src)
target_link_libraries(tts_entry
    PUBLIC tts_framework tts_databoard tts_upstream tts_heartbeat tts_proto_gen
    PRIVATE OpenSSL::SSL OpenSSL::Crypto
)

add_executable(tts_server
    src/main.cpp
    src/config.cpp
)
target_include_directories(tts_server PRIVATE src)
target_link_libraries(tts_server
    PRIVATE tts_framework tts_databoard tts_upstream tts_heartbeat tts_entry tts_proto_gen
    Threads::Threads
    PkgConfig::SPDLOG
    OpenSSL::SSL OpenSSL::Crypto
    PkgConfig::GRPCPP
    PkgConfig::GRPC
    grpc++_reflection
)
if(TTS_SHERPA_LOCAL)
    target_link_directories(tts_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/deps/sherpa-onnx-v1.13.4/lib)
    target_link_libraries(tts_server PRIVATE
        -Wl,--start-group
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/sherpa-onnx-v1.13.4/lib/libonnxruntime.so
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/sherpa-onnx-v1.13.4/lib/libsherpa-onnx-c-api.so
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/sherpa-onnx-v1.13.4/lib/libsherpa-onnx-cxx-api.so
        -Wl,--end-group
        -Wl,--no-as-needed
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/sherpa-onnx-v1.13.4/lib/libonnxruntime.so
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/sherpa-onnx-v1.13.4/lib/libsherpa-onnx-c-api.so)
endif()

enable_testing()
add_subdirectory(tests)