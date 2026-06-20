add_library(rtmp_muduo_server STATIC
    rtmp_server.cpp
)

target_include_directories(rtmp_muduo_server
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rtmp_muduo_server
    PUBLIC
        muduo::static
    PRIVATE
        project_warnings
        rtmp::core
        rtmp::handshake
)

add_library(rtmp::server ALIAS rtmp_muduo_server)
