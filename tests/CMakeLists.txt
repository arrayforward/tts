find_package(GTest REQUIRED)

add_executable(test_event_bus test_event_bus.cpp)
target_link_libraries(test_event_bus PRIVATE tts_framework GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME event_bus COMMAND test_event_bus)

add_executable(test_session_pool_lru test_session_pool_lru.cpp)
target_link_libraries(test_session_pool_lru PRIVATE tts_databoard GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME session_pool_lru COMMAND test_session_pool_lru)

add_executable(test_protocol_codec test_protocol_codec.cpp)
target_link_libraries(test_protocol_codec PRIVATE tts_upstream GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME protocol_codec COMMAND test_protocol_codec)

add_executable(test_jwt_verifier test_jwt_verifier.cpp)
target_link_libraries(test_jwt_verifier
    PRIVATE tts_entry OpenSSL::SSL OpenSSL::Crypto GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME jwt_verifier COMMAND test_jwt_verifier)

add_executable(test_request_parser test_request_parser.cpp)
target_link_libraries(test_request_parser PRIVATE tts_entry GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME request_parser COMMAND test_request_parser)

add_executable(test_heartbeat_e2e test_heartbeat_e2e.cpp)
target_link_libraries(test_heartbeat_e2e
    PRIVATE tts_framework tts_databoard tts_upstream tts_heartbeat GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME heartbeat_e2e COMMAND test_heartbeat_e2e)

add_executable(test_evolution_single_layer test_evolution_single_layer.cpp)
target_link_libraries(test_evolution_single_layer PRIVATE tts_heartbeat GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME evolution_single_layer COMMAND test_evolution_single_layer)

add_executable(test_copy_channel test_copy_channel.cpp)
target_link_libraries(test_copy_channel PRIVATE tts_framework GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME copy_channel COMMAND test_copy_channel)

add_executable(test_audio_resampler test_audio_resampler.cpp)
target_link_libraries(test_audio_resampler PRIVATE tts_local GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME audio_resampler COMMAND test_audio_resampler)

add_executable(test_local_tts_mock test_local_tts_mock.cpp)
target_link_libraries(test_local_tts_mock PRIVATE tts_local GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME local_tts_mock COMMAND test_local_tts_mock)

add_executable(test_heartbeat_local test_heartbeat_local.cpp)
target_link_libraries(test_heartbeat_local
    PRIVATE tts_framework tts_databoard tts_upstream tts_heartbeat tts_local GTest::gtest GTest::gtest_main Threads::Threads)
add_test(NAME heartbeat_local COMMAND test_heartbeat_local)

add_executable(grpc_smoke_test test_grpc_synthesize.cpp)
target_include_directories(grpc_smoke_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/pb2)
target_link_libraries(grpc_smoke_test
    PRIVATE tts_proto_gen
    OpenSSL::SSL OpenSSL::Crypto
    PkgConfig::GRPCPP PkgConfig::GRPC)

add_executable(grpc_ping_test test_grpc_ping.cpp)
target_include_directories(grpc_ping_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/pb2)
target_link_libraries(grpc_ping_test
    PRIVATE tts_proto_gen
    PkgConfig::GRPCPP PkgConfig::GRPC)