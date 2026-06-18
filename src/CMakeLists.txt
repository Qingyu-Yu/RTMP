add_subdirectory(rtmp)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

find_package(Threads REQUIRED)

set(MUDUO_SOURCES
    base/Logger.cpp
    base/Timestamp.cpp
    net/Acceptor.cpp
    net/Buffer.cpp
    net/Channel.cpp
    net/CurrentThread.cpp
    net/DefaultPoller.cpp
    net/EpollPoller.cpp
    net/EventLoop.cpp
    net/EventLoopThread.cpp
    net/EventLoopThreadPool.cpp
    net/InetAddress.cpp
    net/Poller.cpp
    net/TcpConnection.cpp
    net/TcpServer.cpp
)

foreach(library_type IN ITEMS STATIC SHARED)
    string(TOLOWER "${library_type}" library_suffix)
    add_library(muduo_${library_suffix} ${library_type} ${MUDUO_SOURCES})
    target_include_directories(muduo_${library_suffix}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    )
    target_link_libraries(muduo_${library_suffix}
        PUBLIC
            Threads::Threads
        PRIVATE
            project_warnings
    )
    set_target_properties(muduo_${library_suffix} PROPERTIES OUTPUT_NAME muduo)
endforeach()

add_library(muduo::static ALIAS muduo_static)
add_library(muduo::shared ALIAS muduo_shared)
