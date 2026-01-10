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

add_executable(_SAFC_
	_SAFC_.cpp)
