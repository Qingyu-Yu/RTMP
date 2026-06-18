add_executable(echo_server
    main.cpp
)

target_link_libraries(echo_server
    PRIVATE
        muduo::static
        project_warnings
)
