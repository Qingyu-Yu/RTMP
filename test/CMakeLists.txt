# test/CMakeLists.txt

add_executable(muduo_test
    test.cpp
)

target_link_libraries(muduo_test
    PRIVATE
        muduo_static
)
