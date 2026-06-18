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
