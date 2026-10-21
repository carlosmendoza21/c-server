cmake_minimum_required(VERSION 3.10)
project(c-server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_C_FLAGS_DEBUG "-g -Wall -Wextra -fsanitize=address")
set(CMAKE_C_FLAGS_RELEASE "-O2 -Wall -Wextra")

find_package(Threads REQUIRED)

add_executable(c-server src/main.c src/setup.c src/requests.c)

target_include_directories(c-server PRIVATE include)
target_link_libraries(c-server PRIVATE Threads::Threads)

# ---- Tests: ctest --test-dir build --output-on-failure ----
enable_testing()

add_executable(test_paths tests/test_paths.c)
target_include_directories(test_paths PRIVATE include)
# test_paths.c includes requests.c, so the helpers it doesn't test would warn as unused
target_compile_options(test_paths PRIVATE -Wno-unused-function)
add_test(NAME unit_paths COMMAND test_paths)

add_test(NAME integration
         COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration.sh $<TARGET_FILE:c-server>
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

