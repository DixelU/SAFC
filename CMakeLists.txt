cmake_minimum_required(VERSION 3.30)
project(SAFC)

set(CMAKE_CXX_STANDARD 23)

include_directories(_SAFC_)
include_directories(_SAFC_/JSON)
include_directories(utility)
include_directories(utility/include)

set(ADDITIONAL_INCLUDES "" CACHE STRING "Include paths")

include_directories(${ADDITIONAL_INCLUDES})

add_definitions(-DUNICODE -D_UNICODE)

add_executable(SAFC
	_SAFC_/JSON/JSON.cpp
	_SAFC_/JSON/JSONValue.cpp
	_SAFC_/SAFC_InnerModules/compressed_midi_event_source.cpp
	_SAFC_/_SAFC_.cpp)
