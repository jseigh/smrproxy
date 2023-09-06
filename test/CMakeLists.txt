cmake_minimum_required(VERSION 3.20)

project(smrproxy_test)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -std=gnu17 -ggdb")
else ()
    message(FATAL_ERROR "unsupported system type")
endif ()

add_executable(example example.c)
target_include_directories(example PUBLIC
    ../include
    )

target_link_libraries(example 
    ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libsmrproxy.a
    )

