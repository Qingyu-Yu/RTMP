add_library(rtmp_handshake STATIC
    handshake_session.cpp
)

target_include_directories(rtmp_handshake
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rtmp_handshake
    PRIVATE
        project_warnings
)

add_library(rtmp::handshake ALIAS rtmp_handshake)
