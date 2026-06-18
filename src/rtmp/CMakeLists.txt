# 纯 RTMP 握手协议状态机。
# 该目标不依赖 muduo，便于独立测试并保持协议层低耦合。
add_library(rtmp_handshake STATIC
    handshake_session.cpp
)

target_include_directories(rtmp_handshake
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(rtmp_handshake
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# 网络适配层依赖 muduo，而 muduo 当前仅支持 Linux epoll。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(rtmp_handshake_server
        main.cpp
        rtmp_handshake_server.cpp
    )

    target_include_directories(rtmp_handshake_server
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(rtmp_handshake_server
        PRIVATE
            rtmp_handshake
            muduo_static
            pthread
    )

    target_compile_options(rtmp_handshake_server
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()
