cmake_minimum_required(VERSION 3.20)
project(SAFC)

set(CMAKE_CXX_STANDARD 23)

include_directories(_SAFC_)
include_directories(_SAFC_/JSON)

set(ADDITIONAL_INCLUDES "" CACHE STRING "Include paths")

include_directories(${ADDITIONAL_INCLUDES})

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
endif()

add_executable(SAFC
    _SAFC_/_SAFC_.cpp)

if(NOT WIN32)
    target_compile_definitions(SAFC PRIVATE SAFC_CLI_ONLY)
    find_package(Threads REQUIRED)
    target_link_libraries(SAFC PRIVATE Threads::Threads)
endif()
