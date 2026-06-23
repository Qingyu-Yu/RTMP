add_library(rtmp_handshake STATIC
    handshake/handshake_session.cpp
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

add_library(rtmp_core STATIC
    amf/amf0.cpp
    chunk/chunk_codec.cpp
    session/rtmp_session.cpp
    stream/stream_registry.cpp
)

target_include_directories(rtmp_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(rtmp_core
    PRIVATE
        project_warnings
)

add_library(rtmp::core ALIAS rtmp_core)
