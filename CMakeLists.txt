cmake_minimum_required(VERSION 3.30)
project(SAFC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SAFC_ENABLE_SYNCORE "Enable the embedded SAFSYNCore output" ON)

include_directories(_SAFC_)
include_directories(_SAFC_/JSON)
include_directories(utility)
include_directories(utility/include)

set(ADDITIONAL_INCLUDES "" CACHE STRING "Include paths")

include_directories(${ADDITIONAL_INCLUDES})

add_definitions(-DUNICODE -D_UNICODE)

find_package(Boost REQUIRED)
find_package(GLUT REQUIRED)
find_package(LibArchive REQUIRED)

add_executable(SAFC
	_SAFC_/SAFC_InnerModules/compressed_midi_event_source.cpp
	_SAFC_/SAFC_InnerModules/midi_editor_tools.cpp
	_SAFC_/SAFC_InnerModules/syncore_output.cpp
	_SAFC_/SAFCGUIF_Local/midi_editor_tools_ui.cpp
	_SAFC_/_SAFC_.cpp)
target_link_libraries(SAFC PRIVATE Boost::headers GLUT::GLUT LibArchive::LibArchive)
if(MSVC)
	# Match the maintained Visual Studio project; several legacy constexpr call
	# sites still depend on permissive MSVC lookup behavior.
	target_compile_options(SAFC PRIVATE /permissive)
endif()

if(SAFC_ENABLE_SYNCORE)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/SYNCore/CMakeLists.txt")
		set(SAFSYN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
		set(SAFSYN_BUILD_WINMM OFF CACHE BOOL "" FORCE)
		set(SAFSYN_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
		add_subdirectory(SYNCore EXCLUDE_FROM_ALL)
		target_link_libraries(SAFC PRIVATE safsyn-windows)
		target_compile_definitions(SAFC PRIVATE SAFC_WITH_SYNCORE=1)
	else()
		message(WARNING "SAFC_ENABLE_SYNCORE is ON, but SYNCore is unavailable; run git submodule update --init --recursive")
	endif()
endif()
