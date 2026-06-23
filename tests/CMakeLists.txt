add_executable(handshake_session_test
    rtmp/handshake_session_test.cpp
)

target_link_libraries(handshake_session_test
    PRIVATE
        project_warnings
        rtmp::handshake
)

add_test(
    NAME handshake_session_test
    COMMAND handshake_session_test
)

add_executable(rtmp_protocol_test
    rtmp/protocol_test.cpp
)

target_link_libraries(rtmp_protocol_test
    PRIVATE
        project_warnings
        rtmp::core
)

add_test(
    NAME rtmp_protocol_test
    COMMAND rtmp_protocol_test
)

add_executable(timestamp_test
    base/timestamp_test.cpp
    ${PROJECT_SOURCE_DIR}/src/base/Timestamp.cpp
)

target_include_directories(timestamp_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(timestamp_test
    PRIVATE
        project_warnings
)

add_test(
    NAME timestamp_test
    COMMAND timestamp_test
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(timer_queue_test
        net/timer_queue_test.cpp
    )

    target_link_libraries(timer_queue_test
        PRIVATE
            project_warnings
            muduo::static
    )

    add_test(
        NAME timer_queue_test
        COMMAND timer_queue_test
    )
endif()
