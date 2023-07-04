cmake_minimum_required(VERSION 3.20)

project(smrproxy_test)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -std=gnu17 -ggdb")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /O2 /std:c17")
endif ()

add_executable(smrtest smrtest.c smrtestconfig.c)
target_include_directories(smrtest PUBLIC . ../include)
target_link_libraries(smrtest ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libsmrproxy.a)

add_executable(smrtest_mb smrtest.c smrtestconfig.c)
target_compile_definitions(smrtest_mb PUBLIC SMRPROXY_MB)
target_include_directories(smrtest_mb PUBLIC . ../include)
target_link_libraries(smrtest_mb ${CMAKE_CURRENT_SOURCE_DIR}/../lib/libsmrproxy.a)

