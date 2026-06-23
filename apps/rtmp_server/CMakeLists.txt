add_executable(rtmp_server
    main.cpp
)

target_link_libraries(rtmp_server
    PRIVATE
        project_warnings
        rtmp::server
)
