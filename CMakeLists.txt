cmake_minimum_required(VERSION 3.20)

project(smrproxy)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(platform "linux")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -std=gnu17 -ggdb")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(platform "windows")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /O2 /std:c17")
else ()
    set(platform "other")
endif ()

#set(ADDITIONAL_CLEAN_FILES include lib)

add_library(smrproxy STATIC
    src/smrproxy.c
    src/smrqueue.c
    src/smrref.c
    src/platform/${platform}/membarrier.c
    src/platform/${platform}/smr_util.c
)

target_include_directories(smrproxy PUBLIC
    src
)

set_property(TARGET smrproxy
    APPEND
    PROPERTY ADDITIONAL_CLEAN_FILES include lib
)

#${CMAKE_INSTALL_INCLUDEDIR}
install(FILES src/smrproxy.h DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/include)
#${CMAKE_INSTALL_LIBDIR}
install(TARGETS smrproxy DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/lib)

#target_compile_options(smrproxy PUBLIC ()


