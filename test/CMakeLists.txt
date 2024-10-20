cmake_minimum_required(VERSION 3.20)

project(smrproxy_test)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -std=gnu17 -ggdb")
else ()
    message(FATAL_ERROR "unsupported system type")
endif ()

message(STATUS "CMAKE_SOURCE_DIR : ${CMAKE_SOURCE_DIR}")
message(STATUS "PROJECT_SOURCE_DIR : ${PROJECT_SOURCE_DIR}")

add_executable(example example.c)
target_include_directories(example PUBLIC
    ${CMAKE_SOURCE_DIR}/../include
    )

target_link_libraries(example 
    ${CMAKE_SOURCE_DIR}/../lib/libsmrproxy.a
    )

add_executable(example2 example2.c)
target_include_directories(example2 PUBLIC
    ${CMAKE_SOURCE_DIR}/../include
    )

target_link_libraries(example2
    ${CMAKE_SOURCE_DIR}/../lib/libsmrproxy.a
    )


