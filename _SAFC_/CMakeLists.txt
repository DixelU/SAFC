cmake_minimum_required(VERSION 4.1)
project(_SAFC_ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SAFC_ENABLE_SYNCORE "Enable the embedded SAFSYNCore output" ON)
option(SAFC_BUILD_PROCESSOR_TESTS "Build streaming MIDI processor regression tests" OFF)
option(SAFC_BUILD_VIDEO_EXPORT_TESTS "Build the Windows MP4 exporter regression test" OFF)
# The migration preview switch remains an enabling compatibility alias. An
# explicitly supplied SAFC_BUILD_IMGUI setting takes precedence over old caches.
option(SAFC_BUILD_IMGUI_PREVIEW "Deprecated alias enabling the Dear ImGui application" OFF)
if(SAFC_BUILD_IMGUI_PREVIEW AND NOT DEFINED SAFC_BUILD_IMGUI)
	set(SAFC_BUILD_IMGUI ON CACHE BOOL "Build the Dear ImGui SAFC application")
endif()
option(SAFC_BUILD_IMGUI "Build the Dear ImGui SAFC application" ON)
if(WIN32)
	enable_language(RC)
endif()
if(SAFC_BUILD_PROCESSOR_TESTS OR SAFC_BUILD_VIDEO_EXPORT_TESTS OR SAFC_BUILD_IMGUI)
	include(CTest)
endif()

add_compile_definitions(UNICODE _UNICODE)

include_directories(.)
include_directories(btree)
include_directories(JSON)
include_directories(SAFC_InnerModules)
include_directories(../utility)
include_directories(../utility/include)

if(POLICY CMP0167)
	cmake_policy(SET CMP0167 OLD)
endif()
find_package(Boost REQUIRED)
find_package(LibArchive REQUIRED)
find_package(zstd CONFIG REQUIRED)

if(SAFC_BUILD_PROCESSOR_TESTS)
	add_subdirectory(tests)
endif()

if(SAFC_ENABLE_SYNCORE)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../SYNCore/CMakeLists.txt")
		set(SAFSYN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
		set(SAFSYN_BUILD_WINMM OFF CACHE BOOL "" FORCE)
		set(SAFSYN_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../SYNCore"
			"${CMAKE_CURRENT_BINARY_DIR}/SYNCore" EXCLUDE_FROM_ALL)
		if(WIN32 AND SAFC_BUILD_VIDEO_EXPORT_TESTS)
			add_executable(safc-video-export-tests
				tests/player_video_export_tests.cpp
				SAFC_InnerModules/compressed_midi_event_source.cpp
				SAFC_InnerModules/simple_player_video_export.cpp
				SAFC_InnerModules/syncore_output.cpp)
			target_link_libraries(safc-video-export-tests PRIVATE
				Boost::headers LibArchive::LibArchive zstd::libzstd
				safsyn-windows opengl32 winmm mfplat mfreadwrite mfuuid mf oleaut32)
			target_compile_definitions(safc-video-export-tests PRIVATE SAFC_WITH_SYNCORE=1)
			target_compile_features(safc-video-export-tests PRIVATE cxx_std_23)
			if(MSVC)
				target_compile_options(safc-video-export-tests PRIVATE /permissive /EHsc)
			endif()
			add_test(NAME safc-video-export COMMAND safc-video-export-tests
				"${CMAKE_CURRENT_BINARY_DIR}/player-video-test-data")
			set_tests_properties(safc-video-export PROPERTIES TIMEOUT 120)
		endif()
	else()
		message(WARNING "SAFC_ENABLE_SYNCORE is ON, but SYNCore is unavailable; run git submodule update --init --recursive")
	endif()
endif()

if(SAFC_BUILD_IMGUI)
	add_subdirectory(imgui)
endif()