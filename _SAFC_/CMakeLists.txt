cmake_minimum_required(VERSION 4.1)
project(_SAFC_ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SAFC_ENABLE_SYNCORE "Enable the embedded SAFSYNCore output" ON)
option(SAFC_BUILD_APP_TESTS "Build application worker lifecycle regression tests" OFF)
option(SAFC_BUILD_PROCESSOR_TESTS "Build streaming MIDI processor regression tests" OFF)
option(SAFC_BUILD_VIDEO_EXPORT_TESTS "Build the Windows MP4 exporter regression test" OFF)
if(SAFC_BUILD_APP_TESTS OR SAFC_BUILD_PROCESSOR_TESTS OR SAFC_BUILD_VIDEO_EXPORT_TESTS)
	include(CTest)
endif()

add_compile_definitions(UNICODE _UNICODE)

include_directories(.)
include_directories(btree)
include_directories(JSON)
include_directories(SAFC_InnerModules)
include_directories(SAFCGUIF_Local)
include_directories(SAFGUIF)
include_directories(../utility)
include_directories(../utility/include)

if(POLICY CMP0167)
	cmake_policy(SET CMP0167 OLD)
endif()
find_package(Boost REQUIRED)
find_package(GLUT REQUIRED)
find_package(LibArchive REQUIRED)
find_package(zstd CONFIG REQUIRED)

if(SAFC_BUILD_APP_TESTS OR SAFC_BUILD_PROCESSOR_TESTS)
	add_subdirectory(tests)
endif()

add_executable(_SAFC_
	SAFC_InnerModules/compressed_midi_event_source.cpp
	SAFC_InnerModules/midi_editor_tools.cpp
	SAFC_InnerModules/syncore_output.cpp
	SAFC_InnerModules/simple_player_video_export.cpp
	SAFCGUIF_Local/midi_editor_tools_ui.cpp
	SAFCGUIF_Local/player_video_render_ui.cpp
	JSON/JSON.cpp
	JSON/JSONValue.cpp
	app/app_state.cpp
	app/cli_runtime.cpp
	app/dialogs.cpp
	app/editor.cpp
	app/file_actions.cpp
	app/file_properties.cpp
	app/gui_runtime.cpp
	app/merger.cpp
	app/midi_analysis.cpp
	app/playback_source.cpp
	app/player_controls.cpp
	app/settings.cpp
	app/syncore_settings.cpp
	app/ui.cpp
	app/update.cpp
	app/window_layout.cpp
	_SAFC_.cpp)
target_link_libraries(_SAFC_ PRIVATE Boost::headers GLUT::GLUT
	LibArchive::LibArchive zstd::libzstd)
if(WIN32)
	target_link_libraries(_SAFC_ PRIVATE mfplat mfreadwrite mfuuid mf)
endif()
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
		if(WIN32 AND SAFC_BUILD_VIDEO_EXPORT_TESTS)
			add_executable(safc-video-export-tests
				tests/player_video_export_tests.cpp
				SAFC_InnerModules/compressed_midi_event_source.cpp
				SAFC_InnerModules/simple_player_video_export.cpp
				SAFC_InnerModules/syncore_output.cpp)
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
