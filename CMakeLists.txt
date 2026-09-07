cmake_minimum_required(VERSION 3.10)
project(c-server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_C_FLAGS_DEBUG "-g -Wall -Wextra -fsanitize=address")
set(CMAKE_C_FLAGS_RELEASE "-02 -Wall -Wextra")

find_package(Threads REQUIRED)

add_executable(c-server src/main.c)

target_include_directories(c-server PRIVATE include)
target_link_libraries(c-server PRIVATE Threads::Threads)

