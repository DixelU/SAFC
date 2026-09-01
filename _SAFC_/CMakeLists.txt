cmake_minimum_required(VERSION 4.1)
project(_SAFC_)

set(CMAKE_CXX_STANDARD 23)

add_compile_definitions(-DUNICODE)

include_directories(.)
include_directories(btree)
include_directories(JSON)
include_directories(SAFC_InnerModules)
include_directories(SAFCGUIF_Local)
include_directories(SAFGUIF)
include_directories(../utility)
include_directories(../utility/include)

add_executable(_SAFC_
	SAFC_InnerModules/compressed_midi_event_source.cpp
	_SAFC_.cpp)
