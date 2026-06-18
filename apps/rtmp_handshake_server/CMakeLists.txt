add_executable(rtmp_handshake_server
    main.cpp
    rtmp_handshake_server.cpp
)

target_include_directories(rtmp_handshake_server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rtmp_handshake_server
    PRIVATE
        muduo::static
        project_warnings
        rtmp::handshake
)
