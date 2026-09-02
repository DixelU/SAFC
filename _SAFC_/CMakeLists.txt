cmake_minimum_required(VERSION 4.1)
project(_SAFC_ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SAFC_ENABLE_SYNCORE "Enable the embedded SAFSYNCore output" ON)

add_compile_definitions(-DUNICODE)

include_directories(.)
include_directories(btree)
include_directories(JSON)
include_directories(SAFC_InnerModules)
include_directories(SAFCGUIF_Local)
include_directories(SAFGUIF)
include_directories(../utility)
include_directories(../utility/include)

find_package(Boost REQUIRED)
find_package(GLUT REQUIRED)
find_package(LibArchive REQUIRED)

add_executable(_SAFC_
	SAFC_InnerModules/compressed_midi_event_source.cpp
	SAFC_InnerModules/midi_editor_tools.cpp
	SAFC_InnerModules/syncore_output.cpp
	SAFCGUIF_Local/midi_editor_tools_ui.cpp
	_SAFC_.cpp)
target_link_libraries(_SAFC_ PRIVATE Boost::headers GLUT::GLUT LibArchive::LibArchive)
if(MSVC)
	# Match the maintained Visual Studio project; several legacy constexpr call
	# sites still depend on permissive MSVC lookup behavior.
	target_compile_options(_SAFC_ PRIVATE /permissive)
endif()

if(SAFC_ENABLE_SYNCORE)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../SYNCore/CMakeLists.txt")
		set(SAFSYN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
		set(SAFSYN_BUILD_WINMM OFF CACHE BOOL "" FORCE)
		set(SAFSYN_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../SYNCore"
			"${CMAKE_CURRENT_BINARY_DIR}/SYNCore" EXCLUDE_FROM_ALL)
		target_link_libraries(_SAFC_ PRIVATE safsyn-windows)
		target_compile_definitions(_SAFC_ PRIVATE SAFC_WITH_SYNCORE=1)
	else()
		message(WARNING "SAFC_ENABLE_SYNCORE is ON, but SYNCore is unavailable; run git submodule update --init --recursive")
	endif()
endif()
