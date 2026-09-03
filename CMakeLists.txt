cmake_minimum_required(VERSION 3.30)
project(SAFC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SAFC_ENABLE_SYNCORE "Enable the embedded SAFSYNCore output" ON)
option(SAFC_BUILD_VIDEO_EXPORT_TESTS "Build the Windows MP4 exporter regression test" OFF)
if(SAFC_BUILD_VIDEO_EXPORT_TESTS)
	include(CTest)
endif()

include_directories(_SAFC_)
include_directories(_SAFC_/JSON)
include_directories(utility)
include_directories(utility/include)

set(ADDITIONAL_INCLUDES "" CACHE STRING "Include paths")

include_directories(${ADDITIONAL_INCLUDES})

add_definitions(-DUNICODE -D_UNICODE)

if(POLICY CMP0167)
	cmake_policy(SET CMP0167 OLD)
endif()
find_package(Boost REQUIRED)
find_package(GLUT REQUIRED)
find_package(LibArchive REQUIRED)
find_package(zstd CONFIG REQUIRED)

add_executable(SAFC
	_SAFC_/SAFC_InnerModules/compressed_midi_event_source.cpp
	_SAFC_/SAFC_InnerModules/midi_editor_tools.cpp
	_SAFC_/SAFC_InnerModules/syncore_output.cpp
	_SAFC_/SAFC_InnerModules/simple_player_video_export.cpp
	_SAFC_/SAFCGUIF_Local/midi_editor_tools_ui.cpp
	_SAFC_/SAFCGUIF_Local/player_video_render_ui.cpp
	_SAFC_/_SAFC_.cpp)
target_link_libraries(SAFC PRIVATE Boost::headers GLUT::GLUT
	LibArchive::LibArchive zstd::libzstd)
if(WIN32)
	target_link_libraries(SAFC PRIVATE mfplat mfreadwrite mfuuid mf)
endif()
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
		if(WIN32 AND SAFC_BUILD_VIDEO_EXPORT_TESTS)
			add_executable(safc-video-export-tests
				_SAFC_/tests/player_video_export_tests.cpp
				_SAFC_/SAFC_InnerModules/compressed_midi_event_source.cpp
				_SAFC_/SAFC_InnerModules/simple_player_video_export.cpp
				_SAFC_/SAFC_InnerModules/syncore_output.cpp)
			target_link_libraries(safc-video-export-tests PRIVATE
				Boost::headers GLUT::GLUT LibArchive::LibArchive zstd::libzstd
				safsyn-windows mfplat mfreadwrite mfuuid mf)
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
